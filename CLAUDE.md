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
  KeyName.*           keyDisplayName(flags, ch) — порт 0x405fe0 — ГОТОВО
  Recalc.*            порт Recalculate: normalize / markErased / build → TextModel
                      (элементы, паузы, текст+стили, маппинги позиция→элемент/клавограмма/запись,
                      клавограмма KlavRecord). Графики (FUN_00403868) и зоны пальцев ещё НЕ портированы
  MainStats.*         17 параметров ListView2 (compute/format/range/speedAndHold/formatTime) — ГОТОВО,
                      но НЕ сверено с оригиналом
src/cli/tsstat.cpp  консольная утилита: `tsstat [--split MS] [--only-text] [--by-pauses] [--sel S L] [--text] f.tsf`
                    печатает «Параметр\tЗначение» как ListView2 (для дифф-стенда)
src/platform/KeyboardHook.*   обёртка libuiohook → сигнал HookKey{vk, scan, kind Press/Release/Typed, timeUs, ch}
src/main.cpp        пока заглушка (QLabel)
src/ui/, src/export/, i18n/   пусто
resources/icons/    оригинальные иконки кнопок (<Form>_<SpeedButtonN>.png) + app.ico/png; resources.qrc
tests/tst_tsf.cpp   юнит-тесты + golden: подпись и побайтовый round-trip 4 реальных файлов
tests/tst_recalc.cpp  KeyName, разметка BS/Ctrl+BS, нормализация, текст/фрагменты, статистика на синтетике, golden-прогон
tests/golden/       реальные .tsf пользователя с рабочего стола (801, 824, обыка, цифры13зн)
```

Вывод `tsstat` на golden (для сравнения при сверке): 801.tsf → нетто 800,72 (798,06), spm 800,72;
824.tsf → spm 827,41, нетто 824,66; обыка → 915,15; цифры13зн → 18 исправлений, 9 серий.
Имена 801/824, вероятно, — скорость, которую показывал оригинал (гипотеза, проверить стендом).

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
  Скрипт схлопывает инлайненный `vector::push_back` (в т.ч. векторы-члены объектов `obj+0x28 == obj+0x34`)
  и убирает exception-счётчики. Читать удобнее очищенную версию. Recalculate там — `range_400000.c`, ~стр. 5470–7015.
- **Хелперы:**
  - `re/scripts/disasm.py <va> <va|+len>` — capstone-дизасм с именами;
  - `re/scripts/str_at.py <va>...` — строки по адресам;
  - `re/scripts/func_map.py` → `re/func_map.txt`: карта функций (размер, строки, вызовы).
- **Прочее в `re/`:**
  - `forms_dfm.txt` — все 11 форм в тексте (UI 1:1);
  - `strings_data.txt` — строки из .data;
  - `vmt_methods.json` — адреса обработчиков событий форм;
  - `tsf_format.md` — **спецификация формата и хука**;
  - `metrics.md` — **формулы основной статистики** (0x4255a8, 0x438304), форматы строк, разметка стёртых;
  - `text_reconstruction.md` — **Recalculate**: нормализация, KeyDisplayName, построение текста/абзацев/стилей,
    фрагменты, комментарии, маппинги, клавограмма.

**Особенности декомпиляции:**
- Функции приложения — `__cdecl` (параметры в `[ebp+8]`).
- VCL и AnsiString — Borland register (EAX, EDX, ECX). У вызовов `AnsiString_ctor`/`FUN_005643b4` (WideString)
  часто теряется второй аргумент — строковый литерал. Смотреть его в `disasm.py` по адресу вызова.
- Локальная переменная Ghidra `local_N` = `[ebp-(N-4)]`.
- x87-код Ghidra декомпилирует плохо (теряет аргументы `FloatToStrF` 0x4b7f40, путает float/double) —
  формулы с плавающей точкой проверять по `disasm.py`. Константы: `0x4281xx` (float/double/extended 80-бит).
- Функции-хелперы: 0x55c3d8 = fabs, 0x55c41c = `__ftol` (усечение), 0x4b7f40 = FloatToStrF(val, ffFixed=2, 15, digits),
  0x4b61dc = IntToStr, 0x40382c = lower_bound, 0x40373c = `v[i]` или `last+1`, 0x51fcd8 = SendMessage-обёртка.
- ITextFont (TOM) vtable: +0x64 SetForeColor, +0xec SetUnderline; ITextRange +0x70 SetRange.
- Глобальные объекты: `DAT_005b1410` — текстовая модель (+0x40 имена, +0x58 bitset «стёрт», +0x78 bitset injected);
  `DAT_005b1408` (+0x24) — паузы элементов; `DAT_005b1348` — клавограмма (записи 40 Б @ +0x30);
  `DAT_005b1404`, `DAT_005b13ec…140c` — серии графиков; маппинги `DAT_005b1018/1030/1048/1060`;
  `PTR__Form8_005b0a20` — форма настроек (CheckListBox1 @ +0x340 — какие параметры показывать).

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
| Диапазон элементов для статистики (выделение / весь текст / фрагмент у курсора) | 0x4252fc |
| Перестроить строки ListView2 по галочкам Form8 / очистить значения | 0x404a78 / 0x40cdcc |
| Сохранение пресета (`MainOption<i>` и др.) | 0x406a90 |
| Абзац в RichEdit + стили сегментов (flushParagraph) | 0x40c5d0 |
| Добавить элемент в текстовую модель | 0x412c18 |
| Клавограмма: press / segment start / release | 0x4377cc / 0x4379c0 / 0x43720c |
| Расчёт серий графиков для фрагмента (НЕ разобрано) | 0x403868 |
| «Пометить (Ins)» — ставит флаг 0x200 | 0x419ed4 (SpeedButton13Click) |
| ListView1 (Пауза/Длительность/Клавиша) заполнение | 0x404c44 (данные через 0x437d98) |
| Form3 — доп. статистика | 0x43ff3c |
| Form4 — гистограммы | 0x451b88 |

Вектор записей: `g_recBegin` / `g_recEnd` @ 0x5b0fe8 / 0x5b0fec, элемент 24 байта `KeyRec`.

## Уже установлено (кратко; подробности в `re/tsf_format.md`)

- **Формат `.tsf`:** текст cp1251. Строки данных `DDDDDDDD CCCCFFFFFFFF[\t;комментарий]`:
  - dt — мкс от предыдущего события;
  - затем `ch<<32 | flags` в 12 hex-цифрах;
  - заголовок `key=value` в конце.
- **Подпись:** MD5(для каждой записи dt, flags&~0x100, ch как 3×u32 LE; + author + date + "TypingStatistics"), hex в нижнем регистре.
- **Флаги, выставляемые в памяти:** `0x100` Erased (символ потом стёрт BS/Ctrl+BS), `0x200` Marked («Пометить»,
  сохраняется в файл), `0x1000` = LLKHF_INJECTED (стиль «PCmo», синий), `0x40000000` SegmentStart (первое нажатие фрагмента).
- **Recalculate** (подробно — `re/text_reconstruction.md`):
  - удаляет ведущие отпускания, первой записи dt = 60 000 000;
  - сжатие: удаляется только автоповтор модификаторов (VK 5B,5C,A0–A5), dt → следующей записи.
    Отпускания НЕ удаляются (их отсеивает клавограмма);
  - разметка стёртых проходом с конца; Ctrl+BS съедает пробелы перед курсором и одно слово
    ИЛИ прогон пунктуации, пробел перед словом остаётся (подтверждено пользователем);
  - фрагмент начинается, если с прошлой записи клавограммы прошло > UpDown1 мс (200..10000, по умолч. 500)
    и нет зажатых клавиш; в тексте — «‡» (синий) или, при «Разбивать по паузам» (CheckBox3), строка из 8 «—»;
  - «Только текст» = CheckBox2: из длинных имён остаются только [LShift],[RShift],[BackSpace],[Ctrl+BackSpace];
  - комментарий записи: «(…)» — в строку, иначе отдельным абзацем, зелёный; Enter — новый абзац;
  - стили: стёртое — красный (стёртый пробел → «█»), помеченное — подчёркнуто.
- **Основная статистика** (подробно — `re/metrics.md`): 17 параметров из `Form8.CheckListBox1`,
  показываются только отмеченные (`MainOption0..16`). Пары «A (B)»: A — вариант с (n+1), B — с n.
  wpm = нетто·0.2; аритмия = среднее |интервал−среднее| в % от среднего.
- **LoadLanguage 0x41c794:** `<exe>\<Language>.lng`, INI, секция `Main`, ключи-номера
  (i+0x12f, i+0x352, i+0x456); задаёт также «мс» (подпись Label2) и суффиксы времени ч/м/с.
- **Хоткеи:** F8+F9 (вкл/выкл), LCtrl+LWin (очистить), LCtrl+RShift, LCtrl+RCtrl (автокомментарий), Ctrl+Alt+O (оперативная статистика).
- **Оперативная статистика:**
  - `speed = (n−1−pauses)·6e7 / Σdt_µs`;
  - `err% = серии BackSpace ·100 / n`.
- **Настройки:** реестр `HKCU\Software\TypingStatistics` (есть у пользователя, значения совпадают с ключами из строк бинарника). Порт хранит их в QSettings с теми же ключами + импорт.

## Следующие шаги

1. ~~Формулы основной статистики~~ — готово (`re/metrics.md`, `src/core/MainStats`).
2. ~~Recalculate: текст, фрагменты, комментарии, стили~~ — готово (`re/text_reconstruction.md`, `src/core/Recalc`).
3. **СЛЕДУЮЩЕЕ: дифференциальный стенд** (`re/scripts/diffstand.py`, ещё не написан; pywinauto не установлен —
   `pip install pywinauto`):
   - запустить `TypeStats.exe <путь к .tsf>`, дождаться окна, считать ListView2 (Параметр/Значение), текст RichEdit
     (Memo4), ListView1; закрыть процесс;
   - сравнить с `build/tsstat.exe <тот же файл>` (и `--text`) построчно; прогнать 4 golden-файла;
   - если в ListView2 не все 17 строк — какие видны, задаётся галочками `MainOption*` в пресете (реестр);
   - учесть: у оригинала текущие опции (UpDown1, CheckBox2/3) берутся из реестра `HKCU\Software\TypingStatistics`,
     их надо прочитать и передать в tsstat; десятичный разделитель — системный (у пользователя «,»);
   - программа уже запускалась у пользователя, лишних следов в реестре не будет; пока запущена — ставит глобальный хук.
   Расхождения чинить в `src/core` и дописывать в `re/*.md`.
3a. Портировать FUN_00403868 (серии графиков: мгновенная/средняя/классическая/приведённая скорость, ритмичность —
   см. строки «Мгновенная скорость…» в `strings_data.txt` ~0x18f778) и ListView1 (0x404c44/0x437d98).
4. Остальные модули: Form3 (n-граммы, шаблоны, `ExStats.ini`), Form4 (гистограммы), Tkbd и FingerZones (`FingerZones.ini`), журнал `.tsj` (запись с `dt ^ 0x554973`, `FUN_0040b288`; пустой пример — `Desktop\2026_9.tsj`).
5. UI по `re/forms_dfm.txt`. Раскладка главного окна:
   - тулбар 57 px в две строки кнопок 23 px;
   - слева сверху вниз: RichEdit (текст, 120), PaintBox1 (график + скроллбар), PaintBox3 (клавограмма, 200);
   - справа панель 218 px: ListView2 (Параметр/Значение, высота 318) и ListView1 (Пауза/Длительность/Клавиша);
   - плавающие панели: «Настройка оси Y» (Panel9) и «Легенда» (Panel2).

## Правила работы

- Общение с пользователем — на русском. Коммиты оканчивать `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Сначала выписывать находки в `re/*.md`, потом писать код в `src/core` с тестом против golden-файлов.
