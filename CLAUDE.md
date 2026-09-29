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
                      клавограмма KlavRecord)
  MainStats.*         17 параметров ListView2 (compute/format/range/speedAndHold/formatTime) — ГОТОВО,
                      СВЕРЕНО с оригиналом (tst_orig)
  Graphs.*            серии графиков (FUN_00403868 + сглаживание 0x43d4cc), `re/graphs.md` — ГОТОВО, сверено побитно
  IniFile.*           INI как у TIniFile (cp1251 или UTF-8, регистронезависимо); FingerZones.ini, .lng
  FingerZones.*       зоны пальцев: FingerZones (встроенная «Стандарт», разбор/запись Finger0..7, редактор Tkbd,
                      равенство), FingerZoneSchemes (FingerZones.ini + adopt() для LoadTsf), fingerSeries() — ГОТОВО,
                      серия сверена с оригиналом побитно (tst_orig, series.finger)
  KeyList.*           ListView1 «Пауза/Длительность/Клавиша» (0x437d98 + 0x404c44, scrollForPosition 0x414500) —
                      ГОТОВО, сверено
src/cli/tsstat.cpp  консольная утилита: `tsstat [--split MS] [--only-text] [--by-pauses] [--sel S L] [--text|--runs] f.tsf`
                    печатает «Параметр\tЗначение» как ListView2 (для дифф-стенда)
src/platform/KeyboardHook.*   обёртка libuiohook → сигнал HookKey{vk, scan, kind Press/Release/Typed, timeUs, ch}
src/main.cpp        пока заглушка (QLabel)
src/ui/, src/export/, i18n/   пусто
resources/icons/    оригинальные иконки кнопок (<Form>_<SpeedButtonN>.png) + app.ico/png; resources.qrc
tests/tst_tsf.cpp   юнит-тесты + golden: подпись и побайтовый round-trip 4 реальных файлов
tests/tst_recalc.cpp  KeyName, разметка BS/Ctrl+BS, нормализация, текст/фрагменты, статистика на синтетике, golden-прогон
tests/tst_zones.cpp FingerZones, FingerZoneSchemes, IniFile
tests/tst_orig.cpp  ядро против записанного вывода оригинала (tests/golden/orig/*.json): ListView2, текст, стили,
                    выделения, ListView1 — 4 файла × 5 наборов опций
tests/golden/       реальные .tsf пользователя с рабочего стола (801, 824, обыка, цифры13зн)
tests/golden/orig/  что показывал оригинал (пишет re/scripts/diffstand.py)
```

**Эффективные умолчания оригинала** (ключи пресета, `FUN_0041ab08`): `Pause` = 2000 (не 500 из DFM),
`TextOnly` = 1, `SplitOnEnter` = 0, `MainOption*` = 1. У пользователя в реестре их нет — работают умолчания.
`RecalcOptions` в порту по умолчанию такие же.

## Дифференциальный стенд

```bash
python re/scripts/diffstand.py [файлы] [--variants] [--sel N] [--no-styles]   # запуск оригинала, ~1.5 мин/файл
python re/scripts/diffstand.py [файлы] --offline                              # по записанным JSON, секунды
```
- Запускает `TypeStats.exe <файл>`, читает ListView2/ListView1/текст/стили RichEdit, выделения и курсор.
  64-битный Python против 32-битного процесса: структуры LVITEM/CHARFORMAT собираются вручную
  (`re/scripts/win32remote.py`), pywinauto — только поиск окон.
- `--variants` переключает опции в окне оригинала. Оригинал **сразу пишет пресет в реестр**, поэтому стенд
  снимает снимок `HKCU\Software\TypingStatistics` и восстанавливает его после каждого файла.
- Каждый онлайн-прогон перезаписывает `tests/golden/orig/<имя>.json`; для итераций по ядру — `--offline` или `ctest`.
- RichEdit оригинала — RichEdit 1.0: разрыв абзаца = CR LF = 2 позиции (в порту 1). `█` на экране у оригинала —
  `-` (ANSI best-fit), стенд сравнивает через эту проекцию.

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
  - `finger_zones.md` — **зоны пальцев**: объект 0x5b1078, встроенная схема, FingerZones.ini, .tsf, Tkbd (геометрия, цвета);
  - `extra_stats.md` — **Form3**: n-граммы, слова, предложения, шаблоны, фильтр, сортировки, формат, ExStats.ini, стенд;
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
| Form3 — доп. статистика (расчёт; фильтр 0x43fcac, преобразование имени 0x43f6e8, вывод 0x44559c, сортировка 0x445d38/0x445ac0) | 0x43ff3c |
| Зоны пальцев: разбор/строка/home/равенство/редактор/встроенная | 0x449fb4 / 0x44b028 / 0x44b1bc / 0x44b2b0 / 0x44a298 / 0x44a1fc |
| Tkbd: отрисовка клавиатуры / палитры | 0x44910c / 0x449854 |
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
3. ~~Дифференциальный стенд~~ — готово, ядро совпадает с оригиналом полностью (см. «Дифференциальный стенд»).
3a. ~~ListView1, серии графиков~~ — готово (`src/core/KeyList`, `src/core/Graphs`, `re/graphs.md`). Серии стенд читает
   из памяти оригинала; записаны пока только для 824.tsf. Отрисовка графиков (PaintBox1Paint → 0x43c6bc,
   оси/легенда/гистограммы) — на UI-этапе, современными средствами, но с тем же видом.
4. Остальные модули ядра, каждый — сначала `re/*.md`, потом `src/core` + сверка стендом:
   - ~~зоны пальцев~~ — готово (`re/finger_zones.md`, `src/core/FingerZones`, серия сверена);
   - **СЛЕДУЮЩЕЕ: Form3** — реверс ЗАВЕРШЁН, всё в `re/extra_stats.md`. Осталось:
     a) `src/core/ExtraStats.{h,cpp}`: parseTemplate, CharFilter, collect(model, fingers, b, e, kind, pattern, filter)
        → вхождения (speed, pos, text); rows(вхождения, averages, sortMode, descending); occurrences(text) для ListView2;
        formatSpeed = FloatToStrF(ffFixed, 8, 2) с округлением Delphi (половина от нуля, разделитель локали, без групп);
        список шаблонов ExStats.ini (UTF-16 LE BOM / UTF-8 / cp1251);
     b) юнит-тесты на синтетике (tst_extra.cpp);
     c) стенд: открыть Form3 кликом по SpeedButton12 (WM_LBUTTONDOWN/UP панели), для каждого типа RadioGroup1 (BM_CLICK)
        и режима «Средние» прочитать вектор строк `DAT_005b1544..1548` из памяти + текст TntListView1
        (LVM_GETITEMTEXTW) → `tests/golden/orig/<файл>.json` ключ `extra`; сверка в tst_orig. Запускать на одном файле
        (824.tsf), в фоне; шаблоны проверить на синтетике и 1–2 шаблонах в стенде;
     d) tsstat: режим `--extra KIND` для дифф-стенда.
   - Form4 «Дополнительные гистограммы» (0x451b88; функции 0x44d124, 0x44da90, 0x44e57c, 0x44ed0c, 0x44f52c,
     0x44fe88 используют `DAT_005b1278`; строки «Длительности сочетаний», «Двойное нажатие на клавишу»,
     «Та же рука (другой палец)», «Другая рука», «Все клавиши», «Все пальцы» — 0x58ecbe…);
   - журнал `.tsj` (запись с `dt ^ 0x554973`, `FUN_0040b288`); оригинал создаёт пустой `<год>_<месяц>.tsj`
     рядом с exe при каждом старте (стенд его удаляет).
   Для сверки новых модулей стенд дополняется: открыть форму оригинала (кнопка/меню через WM_COMMAND или
   BM_CLICK, TSpeedButton — кликом по родителю) и считать её ListView, либо читать структуры из памяти
   (`win32remote.read_process`).
5. UI по `re/forms_dfm.txt`. Раскладка главного окна:
   - тулбар 57 px в две строки кнопок 23 px;
   - слева сверху вниз: RichEdit (текст, 120), PaintBox1 (график + скроллбар), PaintBox3 (клавограмма, 200);
   - справа панель 218 px: ListView2 (Параметр/Значение, высота 318) и ListView1 (Пауза/Длительность/Клавиша);
   - плавающие панели: «Настройка оси Y» (Panel9) и «Легенда» (Panel2).

## Правила работы

- Общение с пользователем — на русском. Коммиты оканчивать `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Сначала выписывать находки в `re/*.md`, потом писать код в `src/core` с тестом против golden-файлов.
- **Совместимость — по интерфейсу и формулам, не по внутренностям.** Числа, тексты, вид окон и графиков — как в
  оригинале (сверка стендом). Реализация — современная, идиоматичная для Qt; костыли и артефакты оригинала
  (ANSI-RichEdit, ручные вектора, x87-хаки в отрисовке) не переносить. Для графиков — готовые быстрые средства
  (Qt Graphs / QtCharts / свой QPainter-виджет — выбрать на UI-этапе), но с тем же видом.
- **Быстрые итерации.** Пользователя раздражают долгие прогоны. Ядро проверять `ctest` / `diffstand.py --offline`
  (секунды); оригинал запускать редко, на одном файле (самом проблемном), в фоне.
- Декомпиляция Ghidra врёт в x87 (пороги, аргументы fabs/FloatToStrF): любые формулы с плавающей точкой
  подтверждать `disasm.py` и константами из exe. Точность оригинала воспроизводится `long double` (MinGW = 80 бит).
- Инструментальная ловушка: в Bash-хередоках через инструмент `\\` схлопывается в `\` и `"\n"` превращается в
  перевод строки. Правки с обратными слэшами в Python/C++/md делать через Edit/Write, а не `python - <<EOF`.
