# TypeStats → кроссплатформенный порт на Qt 6

## Цель

Переписать `TypeStats.exe` на Qt 6 Widgets + C++ (Win, macOS, Linux) с тем же функционалом.
Оригинал — «Typing statistics v1.43c» Игоря В. Филимонова (C++Builder 6 + VCL + TNT Unicode, 2016).
Это анализатор клавиатурного набора: глобальный хук, клавограмма, графики скорости и ритма,
n-граммы и слова, зоны пальцев, оперативная статистика, журнал.

Полный план: `C:\Users\kizn\.claude\plans\purring-conjuring-liskov.md`.

**Решения пользователя:**
- стек — Qt 6 Widgets + C++;
- старые `.tsf`/`.tsj` должны открываться (бинарная совместимость);
- делаем Excel-экспорт (xlsx/csv), копирование с тегами, мультиязычность (ru/en);
- видео (прикреплённый AVI, синхронный с клавограммой) — последний, необязательный этап через QtMultimedia;
- сознательно выкидываем: «Запускать Ts на одном ядре» и регистрацию `.tsf` в реестре.

`TypeStats.exe` в корне репозитория — оригинал. Не трогать.

## Сборка

Тулчейн установлен через aqtinstall в `C:\Users\kizn\Qt`: Qt 6.8.3 mingw_64, MinGW 13.1, CMake, Ninja.

```bash
source env.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$QTDIR
cmake --build build
cd build && ctest
```

Нюансы:
- В Git Bash тесты ничего не печатают в консоль. Чтобы увидеть результат по кейсам:
  `./build/tst_tsf.exe -o /c/Users/kizn/AppData/Local/Temp/tst.txt,txt`.
- После добавления новых заголовков с `Q_OBJECT` может понадобиться `cmake -S . -B build`
  и удаление `build/TypingStatistics_autogen` (устаревший кэш AUTOMOC).
- Каждый вызов Bash — новый шелл, поэтому `source env.sh` нужен каждый раз.

**Сторонние библиотеки (vendored):**
- `third_party/libuiohook` (LGPL-3) — глобальный хук;
- `third_party/QXlsx` (MIT). В его `CMakeLists.txt` закомментирован `include(CPackConfig)`.

## Структура

```
src/core/      только QtCore, тестируемо
  KeyRecord.h         запись события + биты flags (см. re/tsf_format.md)
  Cp1251.*            кодек cp1251 (в Qt 6 нет переносимого)
  TsfFile.*           чтение/запись .tsf — ГОТОВО, проверено на реальных файлах
  TsfSignature.*      MD5-подпись — ГОТОВО
  Keyboard.*          vkToScan (US-таблица, для tsfVersion=0)
src/platform/KeyboardHook.*   обёртка libuiohook → сигнал HookKey{vk, scan, kind Press/Release/Typed, timeUs, ch}
src/main.cpp        пока заглушка (QLabel)
src/ui/, src/export/, i18n/   пусто
resources/icons/    оригинальные иконки кнопок (<Form>_<SpeedButtonN>.png) + app.ico/png; resources.qrc
tests/tst_tsf.cpp   юнит-тесты + golden: подпись и побайтовый round-trip 4 реальных файлов
tests/golden/       реальные .tsf пользователя с рабочего стола (801, 824, обыка, цифры13зн)
```

## Реверс-инжиниринг (`re/`)

- **Ghidra 12.1.4:** `tools/ghidra_12.1.4_PUBLIC` (в `.gitignore`). Проект `re/ghidra_proj/TypeStats`
  уже проанализирован, имена и типы применены.
- **Скрипт** `re/scripts/TsImportAndDecompile.java`, режимы (аргументы: `<корень репо> <mode>`):
  - `names` — имена из `re/vcl_symbols.json`, структуры форм;
  - `cconv` — классификация cdecl/fastcall + param-id;
  - `types` — `re/rtl_names.json`, `re/globals.json`, структура KeyRec;
  - `decompile` — пишет `re/decomp/range_XX0000.c` для диапазона 0x401000–0x456000;
  - `one:0xADDR[,..]` — одна функция с большим таймаутом → `re/decomp/one_0xADDR.c`.
- **Запуск:**
  ```
  cmd //c "tools\\ghidra_12.1.4_PUBLIC\\support\\analyzeHeadless.bat re\\ghidra_proj TypeStats -process TypeStats.exe -noanalysis -scriptPath re\\scripts -postScript TsImportAndDecompile.java C:\\Users\\kizn\\Desktop\\typestats <mode>"
  ```
  Для `one:` добавлять `-readOnly`. Прогон занимает минуты, запускать в фоне.
  Завершение проверять по логу (`grep "Decompiled\|one "`), а не через `ps`: он не видит `java.exe`,
  используйте `tasklist`.
- **После декомпиляции:** `python re/scripts/clean_decomp.py` → `re/decomp_clean/`.
  Скрипт схлопывает инлайненный `vector::push_back` и убирает exception-счётчики.
  Читать удобнее очищенную версию.
- **Хелперы:**
  - `re/scripts/disasm.py <va> <va|+len>` — capstone-дизасм с именами;
  - `re/scripts/str_at.py <va>...` — строки по адресам;
  - `re/scripts/func_map.py` → `re/func_map.txt`: карта функций (размер, строки, вызовы).
- **Прочее в `re/`:**
  - `forms_dfm.txt` — все 11 форм в тексте (UI 1:1);
  - `strings_data.txt` — строки из .data;
  - `vmt_methods.json` — адреса обработчиков событий форм;
  - `tsf_format.md` — **спецификация формата и хука (основной результат)**.

**Особенности декомпиляции:**
- Функции приложения — `__cdecl` (параметры в `[ebp+8]`).
- VCL и AnsiString — Borland register (EAX, EDX, ECX). У вызовов `AnsiString_ctor`/`FUN_005643b4` (WideString)
  часто теряется второй аргумент — строковый литерал. Смотреть его в `disasm.py` по адресу вызова.
- Локальная переменная Ghidra `local_N` = `[ebp-(N-4)]`.

## Ключевые найденные адреса

| Функция | Адрес |
|---|---|
| KeyboardHookProc (WH_KEYBOARD_LL) | 0x404598 |
| OnKeyEvent (хоткеи, автокомментарии, журнал, оперативная статистика, добавление записи) | 0x40a7a4 |
| Recalculate (нормализация записей, построение текста из сегментов, клавограмма, графики; ~2200 строк) | 0x40ce40 |
| LoadTsf / SaveTsf | 0x4160ac / 0x415480 |
| Загрузка настроек/пресета из реестра | 0x41ab08 |
| MD5Init/Update/FinalHex | 0x43e25c / 0x43e2a4 / 0x43e358 |
| Timer_LapUs (QPC, мкс) | 0x43ef48 |
| KeyDisplayName (имена клавиш «BackSpace», «[LCtrl]»…) | 0x405fe0 |
| LoadLanguage: `<exe dir>\<name>.lng`, секция `Main` → подписи контролов (`re/decomp/one_0x41c794.c`) | 0x41c794 |
| Основная статистика ListView2 (формулы — `re/metrics.md`) | 0x4255a8 |
| spm + среднее удержание по клавограмме | 0x438304 |
| FormatTime (ч/м/с) | 0x40332c |
| Form3 — доп. статистика | 0x43ff3c |

Вектор записей: `g_recBegin` / `g_recEnd` @ 0x5b0fe8 / 0x5b0fec, элемент 24 байта `KeyRec`.

## Уже установлено (кратко; подробности в `re/tsf_format.md`)

- **Формат `.tsf`:** текст cp1251. Строки данных `DDDDDDDD CCCCFFFFFFFF[\t;комментарий]`:
  - dt — мкс от предыдущего события;
  - затем `ch<<32 | flags` в 12 hex-цифрах;
  - заголовок `key=value` в конце.
- **Подпись:** MD5(для каждой записи dt, flags&~0x100, ch как 3×u32 LE; + author + date + "TypingStatistics"), hex в нижнем регистре.
- **Recalculate:**
  - в начале удаляет ведущие отпускания и ставит первой записи dt = 60 000 000;
  - сжимает поток: отпускания ненажатых клавиш и повторные нажатия модификаторов (`IsModifierKey` 0x40cd94) удаляются, их dt прибавляется к следующей записи;
  - пауза разбиения = UpDown1 (мс) × 1000;
  - «Только текст» = CheckBox2.
- **Хоткеи:** F8+F9 (вкл/выкл), LCtrl+LWin (очистить), LCtrl+RShift, LCtrl+RCtrl (автокомментарий), Ctrl+Alt+O (оперативная статистика).
- **Оперативная статистика:**
  - `speed = (n−1−pauses)·6e7 / Σdt_µs`;
  - `err% = серии BackSpace ·100 / n`.
- **Настройки:** реестр `HKCU\Software\TypingStatistics` (есть у пользователя, значения совпадают с ключами из строк бинарника). Порт хранит их в QSettings с теми же ключами + импорт.

## Следующие шаги

1. ~~Формулы основной статистики~~ → `re/metrics.md` (готово). Дальше: `src/core/MainStats` по псевдокоду оттуда.
   Сначала нужна текстовая модель (элементы, паузы, флаг «стёрт» 0x100) и клавограмма (40-байтные записи) из Recalculate.
   Проверка — на дифф-стенде (шаг 3).
2. Дочитать Recalculate: построение текста (сегменты WideString+стиль, `FUN_004129b8` / `FUN_0040c5d0`), «Разбивать по паузам», комментарии «(…)», стили ошибок → `re/text_reconstruction.md`.
3. Дифференциальный стенд (pywinauto ещё не установлен): запустить оригинал с путём `.tsf` в аргументе, считать ListView2, RichEdit и ListView1, сравнить с ядром.
   - Программа уже запускалась у пользователя, так что лишних следов в реестре не будет.
   - Оригинал ставит глобальный хук, пока запущен.
4. Остальные модули: Form3 (n-граммы, шаблоны, `ExStats.ini`), Form4 (гистограммы), Tkbd и FingerZones (`FingerZones.ini`), журнал `.tsj` (запись с `dt ^ 0x554973`, `FUN_0040b288`; пустой пример — `Desktop\2026_9.tsj`).
5. UI по `re/forms_dfm.txt`. Раскладка главного окна:
   - тулбар 57 px в две строки кнопок 23 px;
   - слева сверху вниз: RichEdit (текст, 120), PaintBox1 (график + скроллбар), PaintBox3 (клавограмма, 200);
   - справа панель 218 px: ListView2 (Параметр/Значение, высота 318) и ListView1 (Пауза/Длительность/Клавиша);
   - плавающие панели: «Настройка оси Y» (Panel9) и «Легенда» (Panel2).

## Правила работы

- Общение с пользователем — на русском. Коммиты оканчивать `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Сначала выписывать находки в `re/*.md`, потом писать код в `src/core` с тестом против golden-файлов.
