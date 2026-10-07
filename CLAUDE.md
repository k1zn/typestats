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
- видео (прикреплённый AVI, синхронный с клавограммой) было сделано через QtMultimedia и **убрано** (2026-10-05):
  кнопки 20/21 на месте и всегда выключены; поля `AttachedVideo`/`VideoTimeShift` `.tsf` читаются и пишутся как есть;
- сознательно выкидываем: «Запускать Ts на одном ядре». Регистрацию `.tsf` (оригинал писал её в реестр сам) порт
  делает по согласию: при первом запуске спрашивает один раз (`platform/FileAssociation`, все три ОС).
- **метки времени записи** (2026-10-06, своё): по желанию (настройка `StampRecording`, выкл.); устройство — в
  `re/stamps.md` (только локально, в git его нет). Кнопка состояния — слева от кнопки темы; `tsstat --verify`.

`TypeStats.exe` в корне — оригинал. Не трогать. В git его нет (2026-10-05 убран из всей истории `filter-branch`, локально —
`.git/info/exclude`; старая история — локальная ветка `backup/with-original`, не пушить): нужен только стенду и Ghidra.
Репозиторий: https://github.com/k1zn/typestats (публичный), CI — GitHub Actions.

## Сборка

Тулчейн установлен через aqtinstall в `D:\Qt` (перенесён с C: 2026-10-07, как и проект — `D:\typestats`): Qt 6.8.3
mingw_64 + QtMultimedia (с FFmpeg), MinGW 13.1, CMake, Ninja, NASM 3.02 (`D:\Qt\Tools\nasm-3.02`, нужен libaom).
Исходники зависимостей (libaom, opus) скачаны в `D:\Qt\src\deps`. `download.qt.io` отсюда недоступен (сброс соединения); работает зеркало
`https://mirrors.ocf.berkeley.edu/qt/` (`aqt ... --base <зеркало>`), но на нём нет `.sha256`, которые aqt требует, —
QtMultimedia поставлен вручную: архив из `Updates.xml` зеркала + сверка его `.sha1`, распаковка в `6.8.3/mingw_64`.

```bash
source env.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$QTDIR
cmake --build build
cd build && ctest
```

Опции CMake: `TS_LRELEASE` (путь к `lrelease.exe`, если у Qt нет LinguistTools — статический Qt), `TS_SOFT_EXT80` (OFF; ON —
программная 80-битная арифметика и на x86, чтобы проверить её golden-тестами).

**Linux и macOS** — сделано, ждёт ручной проверки: `re/crossplatform.md` (решения, устройство бэкендов, сборка под
Linux в Docker, список ручных проверок). Linux: нужны `xkbcommon` (обязательно), `xkbregistry`, `xkbcommon-x11` + `xcb-xkb`,
`Qt6::DBus` (по возможности); `cmake --install` ставит exe, `.desktop`, иконки и udev-правило (`TS_UDEV_RULES_DIR`).
macOS: `.app` с bundle id `org.typingstatistics.TypingStatistics`, минимум 12.0; здесь не собрать — только CI.
**CI** — `.github/workflows/ci.yml` (remote у репозитория пока нет): Windows (mingw: тесты на обычном Qt; артефакт — один exe на статическом Qt, который `ci/windows/static-qt.sh` собирает
~40 мин один раз и кладёт в кэш Actions — кэш живёт 7 дней без обращений), Ubuntu
(GCC + AppImage через `ci/linux/appimage.sh`; Clang с `TS_SOFT_EXT80=ON`), macOS arm64 и x86_64 (`.dmg`, подпись
ad-hoc, без нотаризации). AppImage проверяется локально: `ci/linux/appimage.sh <build>` в образе `ci/linux/Dockerfile`.
**Релиз** — тег `v*` (после всех сборок CI публикует GitHub Release: exe, AppImage, два `.dmg`, без zip). Заголовок
«Typing statistics (remake vX.Y.Z)», описание — только список изменений, из сообщения аннотированного тега:
`git tag -a v1.0.2 -m "- изменение"` (у простого тега описание пустое). Без инструкций по установке (решение
пользователя). Версия в заголовке окна остаётся `1.43c`; версия самого ремейка — `TS_REMAKE_VERSION` в
CMakeLists (окно «О программе»), теги релизов — по ней.
QXlsx требует `Qt6::GuiPrivate`: на дистрибутивах нужен пакет приватных заголовков (`qt6-base-private-dev` в
Debian/Ubuntu, `qt6-qtbase-private-devel` в Fedora). `lrelease` ищется и в `bin`/`libexec` Qt (у Debian — вне PATH).

Нюансы:
- В Git Bash тесты ничего не печатают в консоль. Чтобы увидеть результат по кейсам:
  `./build/tst_tsf.exe -o /c/Users/kizn/AppData/Local/Temp/tst.txt,txt`.
- После добавления новых заголовков с `Q_OBJECT` может понадобиться `cmake -S . -B build`
  и удаление `build/TypingStatistics_autogen` (устаревший кэш AUTOMOC).
- Каждый вызов Bash — новый шелл, поэтому `source env.sh` нужен каждый раз.

**Дистрибутив** (`dist/`, `build-release/` — в `.gitignore`):
```bash
source env.sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$QTDIR
cmake --build build-release --target TypingStatistics
mkdir -p dist/TypingStatistics && cp build-release/TypingStatistics.exe dist/TypingStatistics/
windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-quick-import --compiler-runtime dist/TypingStatistics/TypingStatistics.exe
```
Затем удалить лишнее: `generic/`, `tls/`, `networkinformation/`, `iconengines/`, `imageformats/{qgif,qjpeg,qsvg}.dll`,
`Qt6Svg.dll`, `Qt6Network.dll` (были нужны QtMultimedia). С видео было ~54 МБ, zip ~24 МБ; без него не перемерено.
Проверено запуском с `PATH` без Qt/MinGW.

**Один exe** (`dist/TypingStatistics-single.exe`, ~17 МБ): статический qtbase 6.8.3 собран из исходников
(`D:\Qt\src`; установка `6.8.3\mingw_64_static_net` — `ci/windows/static-qt.sh`, с network для меток
времени; прежняя `mingw_64_static_min` — без network). Конфигурация qtbase:
`-DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DFEATURE_optimize_size=ON -DFEATURE_static_runtime=ON`, выключены
`opengl dynamicgl dbus sql printsupport concurrent xml jpeg gif freetype textodfwriter textmarkdownreader
textmarkdownwriter pdf vulkan colordialog fontdialog wizard mdiarea calendarwidget dockwidget undoview columnview
fontcombobox ssl openssl schannel dtls ocsp libproxy brotli zstd networklistmanager networkdiskcache localserver
udpsocket sctp` (`-DFEATURE_x=OFF`; network остаётся, без TLS — службы меток по HTTP). Грабли: без `dynamicgl=OFF` при `opengl=OFF` не собирается плагин windows;
`graphicsview` нужен стилю windows11; LTO невозможно (slim-LTO не дружит с `-Wa,-mbig-obj`, а без него GCC 13.1 падает
с ICE). Программа (PATH — только MinGW/CMake/Ninja, без динамического Qt):
```bash
cmake -S . -B build-static-o2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/mingw_64_static_net -DTS_LRELEASE=D:/Qt/6.8.3/mingw_64/bin/lrelease.exe "-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections" "-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections" "-DCMAKE_EXE_LINKER_FLAGS=-s -Wl,--gc-sections"
cmake --build build-static-o2 --target TypingStatistics
```
CI собирает так же (`ci/windows/static-qt.sh` — та же конфигурация qtbase плюс явные `FEATURE_system_*=OFF`).
Предупреждения `-Wall -Wextra` включает сам CMakeLists — только для своих целей, не для QXlsx. В exe только системные DLL Windows; плагины — qwindows, qmodernwindowsstyle, qico. Вес: Qt Gui/Widgets/Core ~4 МБ
каждый, libstdc++ 0,9, HarfBuzz 0,7, QXlsx 0,7.

**Сторонние библиотеки (vendored):**
- `third_party/QXlsx` (MIT). В его `CMakeLists.txt` закомментирован `include(CPackConfig)`.

## Структура

```
src/core/      только QtCore, тестируемо
  KeyRecord.h         запись события + биты flags (см. re/tsf_format.md)
  Cp1251.*            кодек cp1251 (в Qt 6 нет переносимого)
  TsfFile.*           чтение/запись .tsf — ГОТОВО, проверено на реальных файлах
  TsfSignature.*      MD5-подпись — ГОТОВО
  Keyboard.*          vkToScan (US-таблица, для tsfVersion=0)
  KeyName.*           keyDisplayName(flags, ch) — порт 0x405fe0 — ГОТОВО; keyDisplayChar — «имя из одного символа?»
                      без построения строки (горячие пути)
  Recalc.*            порт Recalculate: `Recalc::run(const KeyRecords &, opt)` → TextModel; документ не меняется.
                      `TextModel::serial` — новый на каждый run: окна кэшируют посчитанное по модели (Form3, Form4).
                      TextModel: `records` (нормализованная копия; все индексы записей — в ней), `recErased`,
                      элементы (names/flags/recIndex/pauses), `fragmentStarts` + `startsFragment()/fragmentAt()`,
                      текст+стили, клавограмма KlavRecord (`erased`, `fragmentStart`), якоря `anchors`
                      {pos, elem, klav, rec} + `elementAt(pos)`, `klavAt(pos)`, `klavOfElement`, `recordOfElement`,
                      `elementOfRecord`
  Journal.*           журнал .tsj (`re/journal.md`): fileName, encode/decode, read, JournalWriter — ГОТОВО,
                      оригинал открывает журнал порта (стенд `--journal`)
  MainStats.*         17 параметров ListView2 (compute/format/range/speedAndHold/formatTime) — ГОТОВО,
                      СВЕРЕНО с оригиналом (tst_orig)
  Graphs.*            серии графиков (FUN_00403868 + сглаживание 0x43d4cc), `re/graphs.md` — ГОТОВО, сверено побитно
  IniFile.*           INI как у TIniFile (cp1251 или UTF-8, регистронезависимо); FingerZones.ini, .lng
  FingerZones.*       зоны пальцев: FingerZones (встроенная «Стандарт», разбор/запись Finger0..7, редактор Tkbd,
                      равенство), FingerZoneSchemes (FingerZones.ini + adopt() для LoadTsf), fingerSeries() — ГОТОВО,
                      серия сверена с оригиналом побитно (tst_orig, series.finger)
  KeyList.*           ListView1 «Пауза/Длительность/Клавиша» (0x437d98 + 0x404c44, scrollForPosition 0x414500):
                      `rows(klav, loc, fromUs, toUs, limit, digits)` (начало окна — бинарным поиском по `tDraw`) —
                      ГОТОВО, сверено. Окно времени считает UI; формула
                      оригинала (float!) — в `tst_orig.cpp::portLv1`: `start = scrollMs·1000 − 10`,
                      `end = start + widthPx·1000/zoom`, zoom px/мс (0.25 по умолч., 0.04..300)
  ExtraStats.*        Form3 «Дополнительная статистика» (0x43ff3c): parseTemplate, CharFilter, collect → вхождения,
                      rows (средние, сортировки 0–3), occurrences, Sort (клики по столбцам, заголовки ▲▼),
                      formatSpeed, toText, TemplateList (ExStats.ini) — ГОТОВО, сверено побитно
  Histograms.*        Form4 «Статистические гистограммы»: Node (страница) → build() → Page{title, bars}, drill(),
                      fromExtra(), hint(), labelsFromRecords() (подписи клавиш из записи, а не из раскладки окна:
                      самый частый символ без Shift) — ГОТОВО, сверено побитно (`re/histograms.md`)
  Recorder.*          порт OnKeyEvent 0x40a7a4 (`re/recording.md`): `handle(HookEvent, RecorderSettings, Context, records)`
                      → Outcome {recorded, setCapture, clear, toggleLive, liveReset, liveChanged}; хоткеи, автокомментарии,
                      фильтр отпусканий, мёртвые клавиши, таймер с отменой круга, оперативная статистика `live()` — ГОТОВО,
                      тесты на синтетике (руками на живом вводе НЕ проверялось)
  Editing.*           правка и копирование (`re/editing.md`): recordRange (FUN_004254b4), deleteRange, removeNonText,
                      labelStart/markRange/removeLabel, copyText, copyTagged, convertLayout — ГОТОВО, тесты на синтетике
  NumberFormat.*      formatFixed(v, decimals, loc): округление половинок от нуля, как FloatToStrF оригинала
                      (QLocale округляет к чётному), NaN/Inf как FloatToStrF — используется всеми списками
  Der.*, Rsa.*        разбор/запись DER; проверка подписи RSA PKCS#1 v1.5 (Монтгомери) — для меток времени
  TimeStamp.*         RFC 3161 (`re/stamps.md`)
  Stamps.*            метки записи (`re/stamps.md`)
  Ext80.h             80-битная x87-арифметика: `Ext` = `long double` там, где он x87 (GCC/Clang на x86), иначе
                      программный `Ext80` (arm64, MSVC); `kExtMilli` = 0.001L, `extFloor`/`extFabs`. Всё ядро и окна
                      считают «как оригинал» через `Ext`
src/cli/tsstat.cpp  консольная утилита: `tsstat [--split MS] [--only-text] [--by-pauses] [--sel S L] [--text|--runs] f.tsf`
                    печатает «Параметр\tЗначение» как ListView2 (для дифф-стенда);
                    `--extra KIND [--avg] [--sort N] [--desc] [--pattern P] [--only S] [--any S] [--exclude S]` — список Form3;
                    `--to-journal out.tsj f.tsf` — записи файла журналом; `--verify f.tsf` — метки времени
src/platform/       библиотека `tsplatform`. `KeyboardHook.h`: сигналы `key(HookEvent{timeUs, flags, ch, chars, firstCh,
                    window, ownWindow})` в потоке GUI, `failed(причина)`, `started()`; статические `toUnicode`/`clearDeadKey`/
                    `capsLock` (преобразование раскладки), `layoutKeyName` (Tkbd), `foregroundWindow()/foregroundTitle()/
                    windowTitle(id)` (автокомментарии); `UsLayout` — запасная US-раскладка. Реализации:
  KeyboardHookWin.cpp  свой WH_KEYBOARD_LL **в своём потоке** (цикл сообщений, `THREAD_PRIORITY_TIME_CRITICAL`; отметка
                    `hookNowUs()` = steady_clock при входе в колбэк, занятость GUI на неё не влияет), событие — в GUI
                    очередью (`Impl::deliver`, события прежнего `start()` отбрасываются). В событии — окно переднего плана
                    и «окно этой программы» на момент нажатия (`keyEvent` решает по ним, а не по активному окну сейчас).
                    Флаги как у 0x404598 (ToUnicodeEx, восстановление мёртвой клавиши через ToAsciiEx); вызовы
                    ToUnicodeEx/ToAsciiEx хука и статических функций — под одним мьютексом (состояние мёртвой клавиши общее)
  linux/            evdev + xkbcommon (`re/crossplatform.md`, шаг 5): `EvdevReader` (поток, `/dev/input/event*`, inotify),
                    `XkbKeyboard` (evdev-событие → HookEvent как у Windows-хука), `LayoutSource` (раскладка и окно:
                    X11, sway, Hyprland, KDE, GNOME, системная), `KeyboardHookLinux.cpp`
  mac/KeyboardHookMac.cpp  listen-only CGEventTap в своём потоке, символы — `UCKeyTranslate` текущего источника
                    ввода, окно — `CGWindowListCopyWindowInfo` (шаг 6; проверяет только CI и ручной запуск)
  DesktopParsers.*  разбор ответов окружений Linux (только QtCore, тест на всех ОС — `tst_platform`)
  FileAssociation.* открытие .tsf двойным щелчком, всё в профиле пользователя: Windows — HKCU\Software\Classes
                    (ProgId `TypingStatistics.tsf`; выбор «Открыть с помощью» (UserChoice) программа сменить не может —
                    `Overridden`, показывает, как сделать руками), Linux — MIME `application/x-typing-statistics`
                    (resources/linux/*.xml) + свой .desktop для AppImage + mimeapps.list, macOS — тип из Info.plist +
                    Launch Services. `tsfState`/`associateTsf`; `MainWindow::offerFileAssociation` спрашивает один раз
                    (`TsfAssociationAsked`), перенесённый exe/AppImage чинит молча. Файл из Finder — `QFileOpenEvent` (main)
src/main.cpp        QApplication: светлая схема, стиль windowsvista и шрифт 8 pt на Windows (вид оригинала), переводчик
                    (ключ `Language` = Russian/English, иначе язык системы; `--lang ru|en`), MainWindow, файл из аргумента
src/ui/
  MainWindow.*        Form1: тулбар по координатам DFM (иконки оригинала; все кнопки подключены, кроме 20/21 «Видео» —
                      всегда выключены); заголовок «Ts: ON|OFF - Typing statistics v… - файл» (`updateTitle`),
                      сплиттеры, ListView2 (высота по числу строк + 1, как FUN_00429bbc) / ListView1, «Файл повреждён»,
                      открытие .tsf/.tsj, сохранение (подпись по `m_clean` = g_fileClean; расстановка пальцев в файл и из
                      файла), очистка, «Открыть журнал», опции пересчёта, выделение в тексте → статистика и список клавиш.
                      Правка: удалить / удалить нетекстовые / отменить (один уровень, `m_undo`) / пометить и метки
                      (подсказка и меню по записи под мышью) / копировать (обычно, без ошибок, с тегами) / сохранить блок;
                      перед правкой записи документа заменяются нормализованными (`normalizeRecords`).
                      Запись: `startCapture()` (из main) ставит хук; `keyEvent` (Recorder + JournalWriter при `JournalOn`), `tick` 100 мс (пересчёт, когда окно
                      активно; показ оперативной статистики раз в 5 тиков). Связка графика с клавограммой:
                      `klavogramMoved` / `graphMoved` / `syncGraphScrollBar`, меню оси `showAxisMenu`.
                      Ключи QSettings = ключи пресета оригинала (Vgr*, Vhs*, SpeedYmin…, Legend*, TextWinHeight,
                      KlavWinHeight, RightPanelWidth, DlitCol*Width, FingerZonesName, op*, GlobalOnOff, GlobalClear,
                      AutoComments, JournalOn, StatWin*, TextWin*, UserName…). `applySettings()` — FUN_00429bbc (шрифты,
                      DlitDigits → `m_keyDigits`, трей, пересчёт). Окна Form3/Form4 получают источник в `updateStats()`
                      (`updateExtraStats`/`updateHistograms`, только видимые). Пресеты (`selectPreset`/`createPreset`/
                      `deletePreset`, правый клик по SpeedButton8; после них и после Form8 всегда `applySettings()` —
                      он же кэширует GlobalOnOff/GlobalClear/AutoComments/JournalOn/MainOption* для `keyEvent` и
                      `updateStats`), расстановки (SpeedButton9 создать/правый — удалить,
                      SpeedButton2 — Tkbd), трей (`m_tray`, меню, сворачивание в трей), `captureToggled` (отпускания
                      зажатых клавиш, `Ts: ON/OFF`), экспорт (`keyTable`/`extraTable`/`exportTable`), свёрнутый график
                      (`graphPaneResized`, `m_graphFolded`). `showForm(name)` — для `--show NAME` (снимки форм):
                      settings, extra, hist, hist-fingers, hist-extra, kbd, about, input.
                      «Преобразовать в текущую раскладку» — `convertLayout` (`m_toUnicode` подменяется в тестах).
                      Панели: `showAxisPanel`/`showLegend` только открывают, `updatePanelButtons`, `m_legendOpen`
  GraphWidget.*       PaintBox1 по `re/graph_paint.md`: ось Y (три подписи на линию), 8 серий, строка текста, линейка
                      (двойной правый клик), курсор (двойной клик), мышь (ЛКМ — сдвиг, ПКМ — масштаб, СКМ — быстрый сдвиг,
                      ЛКМ+ПКМ — масштаб клавограммы), `setKlavogramRange`/`pullKlavogramRange` (FUN_0043a918),
                      `scrollParams` (FUN_0043c170), масштабы осей `rescale` (FUN_00405b34). Сверено с эталоном на глаз;
                      мышь руками не проверялась. Сетка — перо PS_DOT как в GDI (штрих 3/3)
  GraphPanels.*       FloatingPanel (заголовок-перетаскивание, красная кнопка), LegendPanel (Panel2), AxisPanel (Panel9)
  FilePropertiesDialog.*  Form2 «Свойства файла» (автор, дата, описание) — перед сохранением
  LiveStatsWindow.*   Form10 «Оперативная статистика»: скорость (цвет — радуга FUN_0042a17c), % ошибок, строка состояния
  Look.*              вид оригинала везде: Windows — родной стиль и 8 pt; иначе Fusion со светлой палитрой и шрифтом того
                      же размера в пикселях (`pointFont`: macOS считает 72 dpi). Тёмная тема (своя): `setDark`/`isDark`,
                      ключ `DarkTheme`, кнопка в углу тулбара, `--theme dark|light` на один запуск; цвета самописных
                      панелей — `Look::colors()` (светлые = оригинала), `re/settings.md` «Порт»
  AppPaths.*          каталог файлов программы (ini, журнал): папка exe на Windows, если в неё можно писать, иначе —
                      каталог данных пользователя
  Texts.*             переводимые подписи для ядра (StatsUnits, названия строк ListView2, `histogramNames()`), список
                      языков и `currentLanguage()`
  SettingsDialog.*    Form8 «Настройки» (`re/settings.md`): читает/пишет QSettings; язык — после перезапуска
  ExtraStatsWindow.*  Form3: виды, средние + нижний список вхождений, «Заблокировать», фильтр, шаблоны (ExStats.ini),
                      сортировка по клику заголовка, копировать/сохранить; строка → клавограмма (`elementSelected`).
                      После расчёта выбирает строку 0, как оригинал. `StringTableModel.h` — модель списков (ячейки
                      верхнего списка — по запросу). Кэш: `collect()`/сортировка не повторяются для того же источника
  HistogramWindow.*   Form4: `HistogramWidget` (столбики, ось, подписи, мышь — `re/histograms.md`) + стек страниц,
                      drill по двойному клику, подсказка при зажатой кнопке. Столбики всегда влезают по высоте (отличие)
  FingerZonesDialog.* Tkbd: клавиатура + палитра пальцев; подписи клавиш — `KeyboardHook::layoutKeyName`
  Presets.*           пресеты = группы `Presets/<имя>` в QSettings, текущий — `Profile`; `importFromOriginal()` —
                      однократный перенос реестра оригинала (флаг `RegistryImported`, вызывается из main)
  TextInputWindow.*   Form9 «Ввод текста» (F4; Esc — скрыть, F2 — очистить); набор в нём записывается
  AboutDialog.*       Form7 «О программе» (меню кнопки «Справка»)
  StampRecorder.*     метки времени во время записи: когда брать, отправка (QtNetwork; `setSend` — подмена в
                      тестах), приём в документ; MainWindow: `beginEdit`/`endEdit` (метки следуют правке),
                      `updateProof`/`showProof` (кнопка слева от кнопки темы и окно подробностей)
  TextView.*          Memo4: QTextEdit, Arial 16 px, стили TextRun (красный/синий/зелёный/подчёркивание), `setModel`
                      вставляет текст кусками с форматом (не `setPlainText` + `mergeCharFormat`: медленно и берёт формат
                      под курсором), `setVisibleRange` — жёлтая подсветка участка, видимого на клавограмме;
                      клавиши Del/Ins/Ctrl+C, контекстное меню, `hovered(pos)` для подсказок
  KlavogramWidget.*   PaintBox3 по `re/klavogram.md`: 9 дорожек по пальцам, цвета по числу зажатых клавиш, шкала
                      времени, рамки стёртых/injected, мышь (ЛКМ — прокрутка, ПКМ — масштаб, СКМ — быстрая
                      прокрутка, колесо, двойной клик — режим курсора, ЛКМ+ПКМ — измерение с плашкой),
                      `visibleSpanUs()` для списка клавиш, `visibleRecords()`. Сверено с эталоном на глаз (снимки);
                      режим курсора и измерение руками не проверялись. `setScrollMs`/`setZoom` — для графика
src/export/TableExport.*  таблица → .xlsx (QXlsx, с диаграммой) или .csv (UTF-8 BOM, «;» при десятичной запятой)
i18n/               `typestats_en.ts` (исходные строки русские), `en.json` (словарь), `update.py`: lupdate + заполнение .ts
                    из словаря, печатает непереведённое. После новых `tr()`: `source env.sh && python i18n/update.py`
resources/icons/    оригинальные иконки кнопок (<Form>_<SpeedButtonN>.png) + app.ico/png; resources.qrc
resources/linux/    udev-правило (uaccess), `.desktop`, иконка 256 px; resources/macos/ — Info.plist.in, app.icns
ci/linux/           Dockerfile (Debian trixie: сборка и тесты под Linux), appimage.sh; .github/workflows/ci.yml — CI
tests/tst_tsf.cpp   юнит-тесты + golden: подпись и побайтовый round-trip 4 реальных файлов; крайние случаи разбора
                    (семантика sscanf и TStrings::Values)
tests/tst_recalc.cpp  KeyName, разметка BS/Ctrl+BS, нормализация, текст/фрагменты, статистика на синтетике, golden-прогон
tests/tst_zones.cpp FingerZones, FingerZoneSchemes, IniFile
tests/tst_extra.cpp ExtraStats на синтетике
tests/tst_hist.cpp  Histograms и formatFixed на синтетике
tests/tst_journal.cpp  Journal: формат записи, битый хвост, JournalWriter, golden-записи через журнал
tests/tst_editing.cpp  Editing на синтетике
tests/tst_ui.cpp    главное окно без экрана (ctest ставит QT_QPA_PLATFORM=offscreen; настройки — во временный INI):
                    открытие и отрисовка, удалить/отменить/копировать, метки, связка графика с клавограммой и мышь на
                    графике, запись через `keyEvent`, настройки, Form3, Form4, Tkbd, пресеты, выключение перехвата,
                    экспорт, свёрнутый график, преобразование раскладки, кнопки панелей (и выключенные кнопки видео),
                    заголовок. Тест — друг окон (`friend class TstUi`). Окна собраны в
                    библиотеку `tsui` (src/ui + src/platform + src/export), ресурсы — в самих exe
tests/tst_recorder.cpp Recorder: записи и dt, игнорируемые события, хоткеи, автокомментарии, мёртвые клавиши, оперативная
                    статистика
tests/tst_orig.cpp  ядро против записанного вывода оригинала (tests/golden/orig/*.json): ListView2, текст, стили,
                    выделения, ListView1 — 4 файла × 5 наборов опций; серии графиков — 824; Form3 (ключ `extra`) и
                    Form4 (ключ `hist`) — 824 и обыка (обыка — при «Только текст» = 1 и = 0).
                    Сверка float — побитная на всех платформах (через `Ext`)
tests/tst_stamps.cpp ядро меток времени (`re/stamps.md`) на `tests/golden/stamps/*.tsf` (настоящие метки служб,
                    `re/scripts/make_stamped.py`); `tst_ui::stampLive` — настоящие
                    службы через сетевой код программы, только с `TS_STAMP_LIVE=1`
tests/tst_platform.cpp  все ОС: таблицы кодов (scan↔VK, evdev, Mac), разбор ответов sway/Hyprland/GNOME/KDE, i3-IPC
tests/tst_evdev.cpp только Linux: XkbKeyboard (символы, флаги, группы, мёртвые клавиши, golden-записи через перевод),
                    EvdevReader на FIFO с поддельным sysfs
tests/tst_ext80.cpp Ext80 против аппаратного x87: 3,4 млн операций побитно (на x86; иначе только самопроверки)
tests/tst_perf.cpp  стенд производительности (не в ctest; `re/perf.md`)
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
python re/scripts/diffstand.py файл --extra                                   # только Form3, ~10 с, дописывает ключ extra
python re/scripts/diffstand.py файл --hist                                    # только Form4, ~10 с, дописывает ключ hist
python re/scripts/diffstand.py файл --extra --hist --variant N               # то же для VARIANTS[N-1] (1 = «Только текст» выкл)
python re/scripts/diffstand.py файл --journal                                 # порт пишет журнал, оригинал открывает, ~10 с
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
  - `one:0xADDR[+0xADDR..]` — отдельные функции с большим таймаутом → `re/decomp/one_0xADDR.c`. Разделитель — `+`
    (запятую cmd.exe режет на аргументы). Если по адресу нет функции (вызывается только через vtable) — создаётся.
- **Запуск:**
  ```
  cmd //c "tools\\ghidra_12.1.4_PUBLIC\\support\\analyzeHeadless.bat re\\ghidra_proj TypeStats -process TypeStats.exe -noanalysis -scriptPath re\\scripts -postScript TsImportAndDecompile.java D:\\typestats <mode>"
  ```
  Для `one:` добавлять `-readOnly`. Прогон занимает минуты, запускать в фоне.
  Завершение проверять по логу (`grep "Decompiled\|one "`), а не через `ps`: он не видит `java.exe`,
  используйте `tasklist`.
- **После декомпиляции:** `python re/scripts/clean_decomp.py` → `re/decomp_clean/`.
  Скрипт схлопывает инлайненный `vector::push_back` (в т.ч. векторы-члены объектов `obj+0x28 == obj+0x34`)
  и убирает exception-счётчики. Читать удобнее очищенную версию. Recalculate там — `range_400000.c`, ~стр. 5470–7015.
- **Хелперы:**
  - `re/scripts/disasm.py <va> <va|+len>` — capstone-дизасм с именами;
  - `re/scripts/str_at.py <va>...` — строки по адресам (ANSI); `re/scripts/wstr_at.py <va>...` — UTF-16-литералы
    (`FUN_005643b4`); `wstr_at.py -f <начало> <конец>` — все литералы `mov edx, imm` диапазона по порядку;
  - `re/scripts/func_map.py` → `re/func_map.txt`: карта функций (размер, строки, вызовы).
- **Прочее в `re/`:**
  - `forms_dfm.txt` — все 11 форм в тексте (UI 1:1); `forms_geometry.txt` — их координаты, шрифты, выравнивание;
  - `strings_data.txt` — строки из .data;
  - `vmt_methods.json` — адреса обработчиков событий форм;
  - `tsf_format.md` — **спецификация формата и хука**;
  - `finger_zones.md` — **зоны пальцев**: объект 0x5b1078, встроенная схема, FingerZones.ini, .tsf, Tkbd (геометрия, цвета);
  - `extra_stats.md` — **Form3**: n-граммы, слова, предложения, шаблоны, фильтр, сортировки, формат, ExStats.ini, стенд;
  - `klavogram.md` — **клавограмма**: состояние объекта, алгоритм отрисовки, цвета, шкала, мышь;
  - `graph_paint.md` — **график PaintBox1**: панели (ось, строка текста, график, легенда) и их vtable, серии и масштабы,
    алгоритм отрисовки, мышь, ScrollBar1 и синхронизация с клавограммой, легенда, «Настройка оси Y»;
  - `editing.md` — **правка и копирование**: диапазон записей выделения, удаление, нетекстовые клавиши, отмена, метки,
    копирование с тегами, свойства файла при сохранении;
  - `settings.md` — **Form8, пресеты и реестр, трей, Form9, Form7, экспорт в Excel**;
  - `recording.md` — **запись**: хук 0x404598, OnKeyEvent 0x40a7a4 по шагам, таймер, оперативная статистика (Form10);
  - `journal.md` — **журнал `.tsj`**: формат записи, имя файла, чтение/запись, отличия порта, стенд;
  - `perf.md` — **производительность**: стенд (`gen_big.py`, `tst_perf`, снимки `tst_ui_snap.cpp`), цифры до/после,
    что оставлено, архитектурные предложения;
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
| Расчёт серий графиков для фрагмента (`re/graphs.md`) | 0x403868 |
| Панели графика: отрисовка графика / оси / строки текста / легенды | 0x43ad84 / 0x43cb0c / 0x43ca8c / 0x43cf64 |
| Серии: линия / гистограмма аритмии / гистограмма длительностей / границы фрагментов | 0x439ef4 / 0x439a5c / 0x439920 / 0x439dc0 |
| График: мышь нажатие / движение, элемент под мышью, синхронизация, параметры ScrollBar1 | 0x43c0c8 / 0x43c264 / 0x43ad40 / 0x43a918 / 0x43c170 |
| Масштабы осей Y (авто-пределы 0x439c5c) | 0x405b34 |
| Применение настроек Form8 (шрифты, высота ListView2, Recalculate) | 0x429bbc |
| Оперативная статистика: показ / цвет скорости / сброс | 0x42a37c / 0x42a17c / 0x402c70 |
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
- **Флаги, выставляемые в памяти оригиналом:** `0x100` Erased (символ потом стёрт BS/Ctrl+BS), `0x200` Marked («Пометить»,
  сохраняется в файл), `0x1000` = LLKHF_INJECTED (стиль «PCmo», синий), `0x40000000` SegmentStart (первое нажатие фрагмента; оригинал сохраняет его в файл, при чтении снимается).
- **Recalculate** (подробно — `re/text_reconstruction.md`):
  - удаляет ведущие отпускания, первой записи dt = 60 000 000;
  - сжатие: удаляется только автоповтор модификаторов (VK 5B,5C,A0–A5), dt → следующей записи.
    Отпускания НЕ удаляются (их отсеивает клавограмма);
  - разметка стёртых проходом с конца; Ctrl+BS съедает пробелы перед курсором и одно слово
    ИЛИ прогон пунктуации, пробел перед словом остаётся (подтверждено пользователем);
  - фрагмент начинается, если с прошлой записи клавограммы прошло > UpDown1 мс (200..10000, по умолч. 2000; в DFM 500)
    и нет зажатых клавиш; в тексте — «‡» (синий) или, при «Разбивать по паузам» (CheckBox3), строка из 8 «—»;
  - «Только текст» = CheckBox2: из длинных имён остаются только [LShift],[RShift],[BackSpace],[Ctrl+BackSpace];
  - комментарий записи: «(…)» — в строку, иначе отдельным абзацем, зелёный; Enter — новый абзац;
  - стили: стёртое — красный (стёртый пробел → «█»), помеченное — подчёркнуто.
- **Основная статистика** (подробно — `re/metrics.md`): 17 параметров из `Form8.CheckListBox1`,
  показываются только отмеченные (`MainOption0..16`). Пары «A (B)»: A — вариант с (n+1), B — с n.
  wpm = нетто·0.2; аритмия = среднее |интервал−среднее| в % от среднего.
- **LoadLanguage 0x41c794:** `<exe>\<Language>.lng`, INI, секция `Main`, ключи-номера
  (i+0x12f, i+0x352, i+0x456); задаёт также «мс» (подпись Label2) и суффиксы времени ч/м/с.
- **Хоткеи:** F8+F9 (вкл/выкл), LCtrl+LWin (очистить), LCtrl+RShift (сброс оперативной статистики), LCtrl+RCtrl
  (автокомментарий), Ctrl+Alt+O (оперативная статистика). Порядок обработки и таймер — `re/recording.md`.
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
   - ~~Form3 «Дополнительная статистика»~~ — готово (`re/extra_stats.md`, `src/core/ExtraStats`, `tsstat --extra`,
     стенд `--extra`; там же грабли стенда: CN_NOTIFY вместо WM_NOTIFY, `id & 0xFFFF` в WM_COMMAND). Сверено на
     824.tsf (цифры) и обыка.tsf (текст, «Только текст» = 1 и 0; русские шаблоны и фильтры). В обыке нет стёртых
     символов — «Слова с ошибками» на тексте проверены только пустым списком;
   - ~~Form4 «Статистические гистограммы»~~ — готово (`re/histograms.md`, `src/core/Histograms`, стенд `--hist`),
     сверено на 824 и обыке; отрисовка столбиков и подсказка — на UI-этапе;
   - ~~журнал `.tsj`~~ — готово (`re/journal.md`, `src/core/Journal`, стенд `--journal`). Оригинал создаёт пустой
     `<год>_<месяц>.tsj` рядом с exe при каждом старте (стенд его удаляет); порт создаёт файл при первой записи.
   Для сверки новых модулей стенд дополняется: открыть форму оригинала (кнопка/меню через WM_COMMAND или
   BM_CLICK, TSpeedButton — кликом по родителю) и считать её ListView, либо читать структуры из памяти
   (`win32remote.read_process`).
5. UI по `re/forms_dfm.txt` (свойства) + `re/forms_geometry.txt` (координаты всех контролов всех 11 форм, пишет
   `re/scripts/dfm_geometry.py`). Эталон вида — `re/ui_reference/main_legend.png`; снимки:
   `python re/scripts/screenshot_orig.py <tsf> <каталог> [SpeedButtonN…]` (оригинал) и
   `python re/scripts/screenshot_port.py <tsf> <out.png>` (порт). Раскладка главного окна:
   - тулбар 57 px в две строки кнопок 23 px;
   - слева сверху вниз: RichEdit (текст, 120), PaintBox1 (график + скроллбар), PaintBox3 (клавограмма, 200);
   - справа панель 218 px: ListView2 (Параметр/Значение, высота 318) и ListView1 (Пауза/Длительность/Клавиша);
   - плавающие панели: «Настройка оси Y» (Panel9) и «Легенда» (Panel2).

   Сделано: главное окно, клавограмма, график с легендой и «Настройкой оси Y», запись с хука, оперативная
   статистика, база i18n (см. `src/ui/` в «Структуре»); числа и вид совпадают с оригиналом.
   Ключа zoom клавограммы в пресете оригинала НЕТ (`FUN_0040687c` — это параметры ScrollBar1), сохранять нечего.
   **Не проверено руками** (нужен живой ввод/мышь, стенд этого не покрывает): запись в чужих окнах (флаги хука,
   мёртвые клавиши, хоткеи), мышь на графике, перетаскивание панелей. Попросить пользователя попробовать.
   Правка, копирование, «Сохранить блок», «Свойства файла», «Преобразовать в текущую раскладку» — сделаны
   (`re/editing.md`).
   Сделано также: Form8 «Настройки», Form3, Form4, Tkbd, Form9, Form7 и меню справки, пресеты, трей, импорт реестра
   оригинала (при первом запуске порта на Windows), экспорт xlsx/csv, свёрнутый график (все — `re/settings.md`,
   `re/extra_stats.md`, `re/histograms.md`, `re/finger_zones.md`, `re/graph_paint.md`). Эталоны вида вспомогательных
   форм — `re/ui_reference/form3*.png`, `form4_*.png` (`python re/scripts/screenshot_forms.py <tsf> re/ui_reference`,
   PrintWindow, фокус не отбирает). Снимок формы порта: `screenshot_port.py <tsf> out.png --window <часть заголовка>
   --show <имя>`.
   Сознательные отличия: смена языка — после перезапуска (у оригинала так для возврата к встроенному); столбики Form4
   не обрезаются по высоте; кнопка «Дополнительная статистика» в Form4 открывает и Form3; кнопка на панели задач
   прячется только у свёрнутого в трей окна; экспорт пишет файл (xlsx/csv) и открывает его, а не управляет Excel по OLE;
   подписи Tkbd вне Windows — по US-раскладке; «Преобразовать в текущую раскладку» не трогает VK_PACKET; видео
   (Form5, Form6) нет — кнопки 20/21 всегда выключены (`re/video.md` — справка по оригиналу).
   Ремейк помечен (2026-10-05): иконка и баннер Form7 — цвета оригинала с переставленными красным и синим каналами
   (синие тона); вместо ссылок Form7 (сайт, форум, письма…) — текст об авторах оригинала и ремейка. Версия — `1.43c`, как
   у оригинала (`TS_VERSION` в CMakeLists; `project()` — 1.43); заголовок окна тот же, что у оригинала.
   Все обработчики Form1 (`re/vmt_methods.json`) сверены с портом: кнопки и пункты меню перенесены. Кнопки 16/17
   (и N7/N8) только открывают панели и выключены, пока панель открыта; кнопка 4 только показывает Form10
   (переключает — хоткей Ctrl+Alt+O). «Только PCmo» (CheckBox6 Form1) скрыт и в оригинале — не переносится.
   **Сравнение записи с оригиналом** (2026-10-01, обе программы писали один ввод, 842 записи; подробно —
   `re/tsf_format.md`, «Что пишет оригинал»): флаги, символы, текст, n-граммы совпали; отличия файла — первая `dt`
   (60 с у оригинала) и бит `0x40000000`; `dt` расходятся на 0,2–2,7 мс — у программы, чей хук стоит в цепочке вторым
   (запущенной раньше), отметка позже.
   **СЛЕДУЮЩЕЕ**:
   - **кроссплатформенность** (Linux X11/Wayland, macOS) — все шаги `re/crossplatform.md` сделаны (2026-10-05);
     осталось: ручные проверки пользователя (список там же), первый прогон CI (когда появится remote) и правки по ним;
   - ~~хук в отдельном потоке~~ — сделан (2026-10-06, `KeyboardHookWin.cpp`, `re/recording.md` «Порт»): время нажатий не
     зависит от загрузки GUI, долгий пересчёт или экспорт не превышают `LowLevelHooksTimeout`. Тесты — жизненный цикл
     потока и окно события (`tst_ui::hookThread`, `windowOfTheKey`); **руками не проверено** (список в `re/recording.md`);
   - ~~аудит производительности~~ — сделан и проверен (2026-10-01, `re/perf.md`; проверка: эквивалентность коммитов,
     снимки окон до/после побайтно, доделки — запас записей после правки, кэш пальцев Form3).
     На 500k записей: нажатие 273 → 5 мкс (QSettings
     на каждое событие), открыть `.tsf` 712 → 260 мс, пересчёт 358 → 174 мс, Form3 при открытом окне 246 → 8 мс,
     память 170 → ~95 МБ. Стенд:
     `python re/scripts/gen_big.py $TEMP/tsperf` → `cmake --build build-release --target tst_perf` →
     `TS_PERF_DIR=$TEMP/tsperf QT_QPA_PLATFORM=offscreen ./build-release/tst_perf.exe [функция]` (результат —
     `$TEMP/tsperf/perf.txt`). Предложены пользователю (не сделаны): инкрементальный
     пересчёт при наборе, фоновый пересчёт. Осторожно: QTextDocument с разными форматами участков, собранный вне
     редактора, строится 33 с на 500k (патология Qt) — вставлять прямо в документ редактора.
     Третий проход (2026-10-06, `re/perf.md`): разделитель дробной части — один раз на таблицу/кадр (Qt спрашивает ОС
     на каждый `decimalPoint()`; экспорт списка клавиш на macOS 1,9 → 0,13 с), список клавиш не перерисовывается без
     изменений, программная Ext80 быстрее и `Graphs` в double, где доказано (arm64: `graphs.compute` 106 → ~14 мс).
     Стенд: `scrollFrames` (кадр при любом `QT_SCALE_FACTOR`, `TS_PERF_STYLE=Fusion`; на Windows offscreen — только с
     `QT_QPA_FONTDIR=C:/Windows/Fonts`), `numbers`, `macPath`; статический Qt — `tst_perf` на `minimal:enable_fonts`.
     Хук Windows в потоке — сделан (выше); один exe собирается с `-O2` (Release; +0,45 МБ, ядро быстрее на 25–45 %);
   - не проверено руками (пользователь пока не пробовал): запись в чужих окнах, мышь графика и клавограммы, панели,
     трей, Form9 (набор в нём должен записываться), импорт настроек оригинала; «Преобразовать в текущую раскладку» на
     настоящей раскладке (ToUnicodeEx).

## Технический долг: артефакты оригинала в ядре

Осталось (результаты верные, но внутри — перенос устройства оригинала):

1. `Recalc::erasedRecords` переводит символ клавиши в cp1251 (`Cp1251::fromUnicode`), чтобы сравнить с набором
   пунктуации (у оригинала ANSI). Символ вне cp1251 превращается в `?` и считается пунктуацией — поведение оригинала;
   при переписывании на QChar сохранить.
2. Мелочи: режим сортировки Form3 — `int` 0..3 (сделать enum). Окна берут подписи из `Texts`/`tr()`, но в ядре
   остались русские литералы: `Stats::rowNames` (tsstat, тесты), умолчания подписей `ExtraStats::Sort::headers` и
   `ExtraStats::toText` (окна передают переведённые).

Уже убрано:
- `long double` как 80-битный x87: на arm64/MSVC он уже, и последние биты float расходились — теперь `Ext`
  (`src/core/Ext80.h`), программная x87-арифметика там, где её нет в железе; `tst_orig` побитный везде;
- Recalc не трогает документ: нормализованная копия записей, «стёрто» и «начало фрагмента» — в `TextModel`
  (`KeyRecord::Erased` больше нет; бит 0x100 в файле — `Transient`). Оригинал нормализует записи на месте, т.е.
  сохраняет файл уже без автоповтора модификаторов; порт сохраняет документ как есть;
- маркер фрагмента −2³¹ в паузах и сериях → `TextModel::fragmentStarts`. Пауза первого элемента фрагмента теперь
  настоящая (разрыв между фрагментами) и ни в какие суммы не входит; в сериях графиков на этих элементах 0 —
  UI рисует по списку фрагментов. `tst_orig` подставляет маркер перед сравнением с памятью оригинала;
- четыре вектора `map*` и `Recalc::at` → `TextModel::anchors` и методы поиска («последний + 1» за концом сохранён,
  для элементов ограничен `size()`);
- `KeyList::rows` без пикселей и магического `maxRows`;
- дословный порт `FloatToStrF` → `formatFixed`; выбор максимума в цикле в гистограммах → сортировка; `std::list`
  нажатых клавиш → вектор.

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
- Поведение окон проверять `tests/tst_ui.cpp` (offscreen, без экрана и без кликов по рабочему столу пользователя);
  новые обработчики — туда же.
- UI проверять снимками: `screenshot_port.py <tsf> <out.png> [--lang en]` после сборки (снимает окно через
  PrintWindow — фокус у пользователя не отбирается) и сравнение с `re/ui_reference/` (или свежим `screenshot_orig.py`).
  Синтетический ввод (SendInput) для проверки хука НЕ слать: он попадёт в окно, где работает пользователь. Для временных файлов стенда с подписью: файл без строки `signature=` оригинал показывает
  как «Файл повреждён» — это нормально, пользователя предупреждать.
- Коммитить после каждого законченного куска (пункт долга, модуль, веха UI), прямо в `master`.
- Инструментальная ловушка: в Bash-хередоках через инструмент `\\` схлопывается в `\` и `"\n"` превращается в
  перевод строки, а апострофы внутри `python - <<'EOF'` ломают разбор команды. Правки с обратными слэшами и большие
  наборы замен — через Edit/Write (скрипт замен класть Write-ом во временный файл и запускать).
- Строки интерфейса — только через `tr()` (исходный текст русский); подписи, которые формирует ядро, — через
  `ui/Texts`. После добавления строк — `python i18n/update.py`, перевод дописать в `i18n/en.json`.
