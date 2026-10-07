# Кроссплатформенность: Linux (X11 и Wayland), macOS (Intel и arm64)

Цель: на Linux и macOS порт собирается, запускается и даёт те же числа, тексты и вид, что на Windows.
Аудит и первые исправления — 2026-10-01. Этот файл — план и состояние работ; сделанное отмечается здесь же.

## Решения пользователя

- Порядок и объём работ выбирает Claude; macOS проверяется вручную (машины нет) и сборкой в CI.
- Linux: **свой бэкенд evdev** (`/dev/input/event*`) вместо libuiohook — для X11 и Wayland одним путём.
  Доступ — **udev-правило с тегом `uaccess`** (root один раз при установке, доступ получает только
  пользователь, который сейчас вошёл в систему; группа `input` — нет).
- Раскладка на Linux: отслеживать **X11, KDE, GNOME** (sway/Hyprland — по возможности, необязательно).
- macOS: **свой listen-only `CGEventTap`** вместо libuiohook. libuiohook из проекта убрать совсем.
- CI на GitHub Actions — делать (remote у репозитория пока нет, файл просто лежит в `.github/`).
- **Видео из проекта убрано** (2026-10-05): QtMultimedia больше не нужна, кнопки 20/21 всегда выключены
  (`re/video.md`). Пункты аудита про видео сняты.
- Раскладка на sway/Hyprland — желательно сразу; если долго — заложить так, чтобы потом только добавить источник.

## Как проверять под Linux (Docker)

WSL нет; Docker Desktop есть (`docker desktop start`). Образ Debian trixie (Qt 6.8.2, GCC 14, Clang 19):
`ci/linux/Dockerfile`.

Сборка и тесты (репозиторий подключается только на чтение, сборка — в `/work` на томе):
```bash
docker build -t tsport-linux ci/linux
MSYS_NO_PATHCONV=1 docker run --rm -v "D:\typestats:/src:ro" -v "<scratch>\work:/work" tsport-linux bash -c '
  cmake -S /src -B /work/build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-Wall -Wextra" &&
  cmake --build /work/build-gcc -- -k 0 && cd /work/build-gcc && QT_QPA_PLATFORM=offscreen ctest --output-on-failure'
```
- `cmake --build ... -k 0` — неправильно (это опция ninja: `-- -k 0`).
- Сборка на томе с Windows медленная (~5 мин полная); долгие прогоны — в фоне.
- `xvfb-run -a ./tst_ui` — проверка с настоящим X-сервером.
- Проверка «как на arm64» без arm64: `-DTS_SOFT_EXT80=ON` (см. ниже) и `tst_orig`.

Результат аудита (до исправлений): GCC и Clang собирают всё; `-Wall -Wextra` дают только
`tests/tst_recorder.cpp:41` (enum и не-enum в `?:`, GCC) и `src/ui/GraphPanels.cpp:247` (неиспользуемый захват
`this`, Clang). ctest: 9/11; `tst_ui` — все 21 кейс прошли, но код выхода 1 (libuiohook, см. №3);
`tst_video::paths` падал (№14; видео потом убрано).

## Сделано

**Ext80** (коммит bf707a4): `src/core/Ext80.h` — программная 80-битная x87-арифметика (64-битная мантисса,
округление к чётному), `using Ext = long double` там, где он x87 (GCC/Clang на x86), иначе `Ext80`. Опция CMake
`TS_SOFT_EXT80=ON` включает `Ext80` везде (для проверки). Все `long double`/`0.001L` в ядре и окнах заменены на
`Ext`/`kExtMilli`/`kExtCenti`. `tst_ext80` сверяет с аппаратурой 3,4 млн операций; с `TS_SOFT_EXT80` `tst_orig`
проходит побитно — допуск `sameFloats` удалён. `formatFixed`: целая и дробная части целочисленным делением,
NaN/Inf → «NAN»/«INF»/«-INF» (как FloatToStrF). `-ffp-contract=off` для GCC/Clang (на arm64 GCC по умолчанию
склеивает a*b+c в fma). Нужен `unsigned __int128` (GCC/Clang; MSVC — только clang-cl).
Цена на arm64 (замер с `TS_SOFT_EXT80` на x86, Release, 500k записей): `graphs.compute` 6 → 104 мс,
`hist.build` 1,6 → 5–7 мс, `keylist.rows all` 184 → 195 мс, `recalc.run` почти без изменений. Деление ~40 нс,
умножение ~13, сложение ~20 (ветвления округления уже убраны). Дальше ускорять можно (Graphs: одиночные
операции над float корректно округляются и в double — теорема о двойном округлении, p ≥ 2q+2), но не нужно,
пока никто не жалуется. Записать в `re/perf.md`.

## План (по шагу на коммит)

### 1. Мелочи — сделано
- Предупреждения: `tests/tst_recorder.cpp:41` (`(down ? 0u : quint32(KeyRecord::KeyUp))`),
  `src/ui/GraphPanels.cpp:247` (убрать `this` из захвата, если `tr` не нужен — проверить).
- `CMakeLists.txt`, поиск `lrelease`: `HINTS` на `${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}` и `.../libexec`
  (на Debian `/usr/lib/qt6/bin` не в PATH).
- QXlsx требует `Qt6::GuiPrivate`: на дистрибутивах нужен `qt6-base-private-dev` (Fedora
  `qt6-qtbase-private-devel`) — записать в CLAUDE.md, раздел «Сборка».

### 2. Файлы программы не рядом с exe — сделано
Сделано так: `src/ui/AppPaths` (`dataDir()`, `file(name)` с переносом старой копии из папки exe, `setDataDir` для
тестов); на Windows папка exe проверяется пробным `QTemporaryFile`. Журнал не переносится. Ошибка записи журнала —
немодальное сообщение один раз (`m_journalFailed`). `IniFile::save` пишет cp1251, если текст в нём представим, иначе
UTF-8. Тесты: `tst_ui::dataFiles`, `tst_ui::journalFailure`, `tst_zones::iniSavedAsAnsi`.

`FingerZones.ini` (`MainWindow.cpp:117`), `ExStats.ini` (`ExtraStatsWindow.cpp:66`), журнал (`MainWindow.cpp:118`,
`:1421`) пишутся в `applicationDirPath`: на macOS это внутри `.app` (только чтение, ломает подпись, App
Translocation), на Linux `/usr/bin` или AppImage. Журнал при этом молча не пишется (`append` → false не
проверяется, `MainWindow.cpp:917`). `.lng` порт не читает — не касается.
- Новый `src/ui/AppPaths.*` (или в `Presets`): каталог данных = папка exe, если там уже есть файл или папка
  доступна на запись и ОС — Windows (портативно, совместимо с оригиналом); иначе
  `QStandardPaths::AppDataLocation` (создать). При первом обращении скопировать туда старый файл из папки exe.
- Журнал: тот же каталог; «Открыть журнал» смотрит туда же. Ошибку записи журнала показать один раз.
- Тест в `tst_ui`: каталог берётся из переменной окружения/сеттера, чтобы не писать в профиль.
- По пути (не ОС): `IniFile::save` пишет UTF-8, а оригинал читает INI как ANSI — кириллические имена схем в общем
  `FingerZones.ini` у оригинала станут кракозябрами. Решить: писать cp1251, если всё кодируется.

### 3. Вид вне Windows (`src/main.cpp:35`) — сделано
Сделано так: `src/ui/Look` — `apply()` (Windows: windowsvista + MS Sans Serif 8 pt, как было; иначе Fusion, светлая
палитра системных цветов Windows, шрифт 11 px из списка семейств) и `pointFont`/`pointsToPixels` для всех шрифтов в
пунктах. Снимки Windows до/после совпали (кроме подсветки кнопки под курсором). Снимки под Xvfb в Docker — тест-снимок
`tst_ui_lsnap.cpp` (как `re/scripts/tst_ui_snap.cpp`, но с `Look::apply()`, формами settings/about/kbd/input и
`QT_QPA_PLATFORM=xcb xvfb-run`): формы не обрезаны, шрифты Liberation. «Сворачивать в трей» выключено, если трея нет.
Ограничения Wayland (свёрнутое состояние, `raise`, позиции окон, «поверх всех») — не лечатся, только записаны здесь.

Геометрия форм — абсолютные пиксели из DFM (`setGeometry`/`setFixedSize` в About, Form2, Form8, панелях,
тулбаре, Form3) под шрифт MS Sans Serif 8 pt @96 dpi; на Linux/macOS стиль платформы и шрифт 10–13 pt обрезают
подписи, тёмная тема KDE (Breeze) игнорирует `setColorScheme(Light)`.
- Вне Windows: `QApplication::setStyle("Fusion")`, явная светлая `QPalette` (цвета классической Windows),
  шрифт приложения `setPixelSize(11)`, семейство: «Microsoft Sans Serif» если есть (есть на macOS), иначе
  «Liberation Sans»/«Arial»/«Helvetica»/«DejaVu Sans».
- Шрифты в пунктах → пиксели (`round(pt·96/72)`, как уже в `TextView`/`TextInputWindow`): `GraphWidget.cpp:93,213,589`
  (Courier New k+8 pt), `HistogramWindow.cpp:60-62`, `KlavogramWidget.cpp:64,73` (Arial 9 pt). На macOS логический
  DPI 72 — сейчас текст там на 25 % мельче. На Windows пиксели = пункты·96/72, вид не меняется (проверить снимком).
- Трей: в Form8 выключать «Сворачивать в трей», если `!QSystemTrayIcon::isSystemTrayAvailable()` (GNOME без
  AppIndicator). На Wayland состояние «свёрнуто» не приходит, `raise`, позиции окон и «поверх всех» не работают —
  записать как ограничения.
- Проверка: снимки под Xvfb в Docker (`QT_QPA_PLATFORM=xcb`, `xvfb-run`, `grab()` окна → PNG) против
  `re/ui_reference/` и снимков Windows-версии. На Linux нужен `fonts-liberation` (иначе DejaVu шире).

### 4. Ошибка запуска хука видна — сделано
Сделано так: у `KeyboardHook` сигналы `failed(причина для пользователя)` и `started()` (хук может запуститься позже сам,
когда дадут доступ — это делают бэкенды шагов 5–6). `MainWindow::hookFailed`: «Вкл» снимается (заголовок и трей —
«Ts: OFF»), причина — немодальным сообщением с выделяемым текстом; попытка включить «Вкл» вручную снова показывает его
и оставляет выключенным; `hookStarted` включает обратно, только если выключила ошибка. Тест `tst_ui::hookFailure`.

`KeyboardHook::failed` ни к чему не подключён (`MainWindow.cpp:690`), а `m_running = true` ставится до результата.
Нужно: при ошибке — «Ts: OFF» в заголовке и трее, кнопка перехвата отжата, одно сообщение с причиной и что делать
(Linux: нет доступа к `/dev/input` → как поставить udev-правило; macOS: разрешение «Мониторинг ввода»); на macOS —
повторная попытка по таймеру, пока разрешение не дано. Тест в `tst_ui` (подставной хук/сигнал).

### 4a. Горячие клавиши по-платформенному — сделано
Просьба пользователя (2026-10-05): подписи сочетаний не должны быть виндовыми везде. `src/ui/Hotkeys`: имена глобальных
(«Очистить» — LCtrl+LWin / LCtrl+LSuper / L⌃+L⌘, F8+F9) и оконных сочетаний (`QKeySequence::NativeText`: на macOS ⌘C, ⌘Z;
Ctrl в Qt на Mac — это Command) для подсказок тулбара и Form8; `textAction()` — клавиши Memo4. На macOS нет Insert:
«Пометить» — ⌘I, удаление выделения — и ⌫. Глобальные хоткеи — те же физические клавиши (по scan-коду; Command и
Super приходят как Win): evdev и CGEventTap видят их раньше окружения, но окружение тоже может отреагировать
(GNOME/KDE — на одиночный Super; на Mac F8/F9 — медиаклавиши без Fn, если не включено «F1, F2… как стандартные»).
Настраиваемые хоткеи — не делались (можно позже). Тест `tst_ui::hotkeyNames`.

### 5. Linux: бэкенд evdev (`src/platform/`) — сделано
Сделано так (без машины с Linux — проверено тестами в Docker; руками — список в конце файла):
- **Структура:** библиотека `tsplatform` (`src/platform`): `KeyboardHook.h` (pimpl; `UsLayout` — запасная US-раскладка),
  `KeyboardHookWin.cpp`, `linux/` (`KeyboardHookLinux.cpp`, `EvdevReader`, `XkbKeyboard`, `LayoutSource` +
  `LayoutSource.cpp`/`WaylandSources.cpp`/`X11Source.cpp`), `DesktopParsers` (чистый разбор ответов окружений, только
  QtCore, тестируется на всех ОС), `HookClock.h`. Таблицы кодов — в ядре: `Keyboard::scanToVk`, `evdevToScan`,
  `scanToEvdev`.
- **`EvdevReader`:** клавиатуры — по `capabilities` в sysfs (буквы, пробел, Enter), не открывая их; `/dev/input/event*`
  только читаются (без `EVIOCGRAB`), `EVIOCSCLOCKID` = `CLOCK_MONOTONIC`; поток: `poll` по устройствам + inotify
  (`IN_CREATE`/`IN_ATTRIB`/`IN_DELETE`: новые клавиатуры, появившийся доступ после udev-правила, потерянный доступ —
  устройство закрывается) + eventfd (остановка). `SYN_DROPPED` — события до `SYN_REPORT` пропускаются. `EV_LED` от
  окружения синхронизирует Caps/NumLock. Injected — `BUS_VIRTUAL` или устройство в `/devices/virtual/` (uinput).
- **`XkbKeyboard`:** флаги модификаторов — по зажатым до события (как Windows); символ — `xkb_state_key_get_one_sym`
  с группой, которую сказало окружение (`setGroup`; −1 — переключает сама keymap); как ToUnicodeEx: Delete → нет
  символа, Ctrl+буква → 01–1A по VK в любой раскладке, Ctrl+BS → 7F, Ctrl+Enter → 0A, Ctrl+Alt — только символ
  третьего уровня; мёртвые клавиши и Compose — `xkb_compose` (локаль `LC_ALL`/`LC_CTYPE`/`LANG`, запасная
  `en_US.UTF-8`): ожидание → DeadKey, chars = −1, символ — то, что раскладка даёт с пробелом; не сложилось → два
  символа. Автоповтор (value 2) — повторное нажатие.
- **Источники раскладки и окна** (`LayoutSource::create`, первый ответивший): X11-сессия — xkbcommon-x11 (keymap и
  группа с сервера, `XkbStateNotify`), окно — `_NET_ACTIVE_WINDOW`/`_NET_WM_NAME`; **sway** — i3-IPC (`GET_INPUTS`,
  подписка на `input`/`window`; имена раскладок — описания, в XKB-имена через libxkbregistry), окно — `GET_TREE` и
  события фокуса; **Hyprland** — `j/devices` (RMLVO + `active_keymap`), события `activelayout`/`configreloaded`/
  `activewindow(v2)` из `.socket2.sock`; **KDE** (Wayland) — D-Bus `org.kde.keyboard /Layouts` (`getLayout`,
  `layoutChanged`, `layoutListChanged`) + список из `kxkbrc`; **GNOME** (Wayland) — `gsettings get/monitor
  org.gnome.desktop.input-sources` (`sources`, `xkb-options`, первая из `mru-sources`); иначе **системный** —
  systemd-localed (D-Bus), `/etc/default/keyboard`, `/etc/vconsole.conf`, `XKB_DEFAULT_*`, `us`. У источников, где
  переключает окружение, опции `grp:` из keymap убираются, а группа ставится перед каждым нажатием — своё
  переключение не спорит с окружением. Новый композитор — подкласс `LayoutSource` + строка в `create()`.
  Окно на KDE/GNOME Wayland неизвестно (0, пустой заголовок): автокомментарий только по паузе.
- **Права:** нет доступа → `failed` с готовыми командами (правило `resources/linux/70-typingstatistics.rules`); поток
  продолжает ждать, и после `udevadm trigger` запись начинается сама (`started`).
- **Зависимости:** `xkbcommon` (обязательно), `xkbregistry`, `xkbcommon-x11`+`xcb`+`xcb-xkb`, `Qt6::DBus` — по
  возможности (`TS_HAVE_XKBREGISTRY`, `TS_HAVE_X11`, `TS_HAVE_DBUS`). `xcb/xkb.h` не C++ (поле `explicit`) —
  `#define explicit explicit_` вокруг включения.
- **libuiohook** на Linux не собирается (на macOS — до шага 6); `tst_ui` на Linux теперь завершается с кодом 0 (№3).
- **Тесты:** `tst_platform` (все ОС: таблицы кодов, разбор sway/Hyprland/GNOME/KDE/`/etc/default/keyboard`, i3-IPC),
  `tst_evdev` (Linux: буквы и группы, Caps, управляющие символы, расширенные и цифровой блок с NumLock и без,
  автоповтор, injected, мёртвые клавиши us(intl), помощники `toUnicode`/`keyName`; **все записи 4 golden-файлов**
  через перевод дают те же флаги и символы; читатель на FIFO с поддельным sysfs: события, `SYN_DROPPED`, `EV_LED`,
  горячее подключение виртуальной клавиатуры, ожидание первой клавиатуры).
- **Найденная разница Windows/XKB:** второе нажатие CapsLock в XKB снимает Caps при *отпускании*, Windows — при
  нажатии; символ, набранный с зажатым CapsLock, на Linux будет в другом регистре (так его и получит программа).
- **Ограничения:** AltGr — без поддельного LCtrl, который вставляет Windows (у символов AltGr только бит Alt); VK букв
  — по позиции US (AZERTY/QWERTZ дадут VK по месту, символы верные); если композитор выключил автоповтор ядра —
  автоповторов в записи нет; keyd/kanata с `EVIOCGRAB` — все нажатия «injected»; GNOME `mru-sources` — проверить на
  настоящем GNOME.

<details><summary>План шага 5 (как задумывалось)</summary>

Почему: libuiohook на X11 (XRecord) не видит ввод в родных Wayland-приложениях; без XWayland не работает вовсе;
у него же найдены проблемы №3, №5–№10 (таблица ниже). Wayland глобальный перехват запрещает протоколом; порталы
(GlobalShortcuts, InputCapture) для записи всех клавиш не годятся.
- **Устройства:** `/dev/input/event*` с `EV_KEY` и клавишами букв/пробела (`EVIOCGBIT`), без мышей. Горячее
  подключение — inotify на `/dev/input` (`IN_CREATE`, `IN_ATTRIB` — udev выставляет ACL после создания). Без libudev.
- **Поток:** один поток чтения, `poll` по устройствам + inotify + eventfd для остановки. `EVIOCSCLOCKID`
  `CLOCK_MONOTONIC` → `input_event.time` в той же шкале, что `steady_clock` (= `nowUs()`). Событие собирается целиком в
  потоке (никакой пары press/typed) и отправляется в поток GUI очередью.
- **value:** 1 — нажатие, 0 — отпускание, 2 — автоповтор → как повторное нажатие (так пишет Windows; автоповтор
  модификаторов потом убирает `Recalc`).
- **Коды:** evdev `KEY_*` основного блока = scan-коды set-1 (`KEY_ESC`=1 … `KEY_KPDOT`=83); расширенные —
  таблицей (KPENTER 96→1C ext, RIGHTCTRL 97→1D ext, KPSLASH 98→35 ext, SYSRQ 99→37 ext, RIGHTALT 100→38 ext,
  HOME 102→47 ext, UP 103→48 ext, PAGEUP 104→49 ext, LEFT 105→4B ext, RIGHT 106→4D ext, END 107→4F ext,
  DOWN 108→50 ext, PAGEDOWN 109→51 ext, INSERT 110→52 ext, DELETE 111→53 ext, LEFTMETA 125→5B ext,
  RIGHTMETA 126→5C ext, COMPOSE 127→5D ext, PAUSE 119→45, NUMLOCK 69→45 ext как у Windows, 102ND 86→56).
  VK — по scan-коду и состоянию NumLock (цифровой блок без NumLock → VK_END и т.п., не extended), как Windows.
- **Флаги как у Windows-хука** (сверено по golden-файлам: у нажатия LShift нет бита Shift, у отпускания есть):
  Shift/Ctrl/Alt/Win — по зажатым клавишам **до** учёта текущего события; Extended — из таблицы; Injected —
  устройство виртуальное (`EVIOCGID` bustype `BUS_VIRTUAL`, uinput: ydotool, keyd, kanata — оговорить, что при
  перехватчике раскладки с `EVIOCGRAB` все нажатия будут «injected»); KeyUp; NoChar/HasChar; DeadKey.
- **Символы:** xkbcommon. `xkb_state_key_get_utf32` (keycode+8) по состоянию **до** `xkb_state_update_key`.
  Приведение к `ToUnicodeEx` (проверить по golden-файлам, что писал оригинал для Ctrl+BS, Ctrl+Enter, Ctrl+Tab):
  Delete/KP_Delete → без символа (xkb даёт 0x7F, иначе в тексте управляющий символ); BS 08, Tab 09, Enter 0D,
  Esc 1B; Ctrl+буква → управляющие 01–1A (xkb делает так же); Ctrl+BS → 7F (у Windows), xkb даёт 08 — поправить.
  Мёртвые клавиши — `xkb_compose` (локаль из `LC_ALL`/`LC_CTYPE`/`LANG`): `COMPOSING` → DeadKey, chars = −1;
  `COMPOSED` → составной символ; `CANCELLED` → два символа (firstCh — «пробельный» вариант мёртвой клавиши,
  ch — символ клавиши), как Windows.
- **Раскладка** (под Wayland её знает только композитор). Одна keymap из списка раскладок (группы), текущая группа
  — `xkb_state_update_mask(..., locked_group)` перед каждым нажатием. Источники списка и текущей группы:
  - X11-сессия (`XDG_SESSION_TYPE=x11`): xkbcommon-x11 — keymap и состояние с сервера, события
    `XkbStateNotify`/`XkbNewKeyboardNotify`/`XkbMapNotify` (xcb-xkb) в том же `poll`;
  - KDE: D-Bus `org.kde.keyboard` `/Layouts`: `getLayoutsList()` → `a(sss)` (раскладка, вариант, имя),
    `getLayout()` → индекс, сигналы `layoutChanged(uint)`, `layoutListChanged()` (QtDBus, опционально);
  - GNOME: `gsettings get org.gnome.desktop.input-sources sources` (`[('xkb', 'us'), ('xkb', 'ru+phonetic')]`),
    `xkb-options`; текущая — первая в `mru-sources`, следить `gsettings monitor org.gnome.desktop.input-sources`
    (QProcess). **Проверить на настоящем GNOME**, что `mru-sources` меняется при каждом переключении;
  - иначе: `XKB_DEFAULT_LAYOUT/VARIANT/OPTIONS`, `/etc/default/keyboard`, `us`; переключение — только сочетанием из
    `grp:` опций keymap (его применит сам `xkb_state_update_key`).
  Обновление keymap/группы — в потоке GUI, передача в поток чтения под мьютексом/атомарно.
- **Права:** нет читаемых клавиатур → `failed` с инструкцией. udev-правило в `resources/linux/70-typingstatistics.rules`:
  `SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_KEYBOARD}=="1", TAG+="uaccess"`, ставится `install()` в
  `/usr/lib/udev/rules.d`, после установки — `udevadm control --reload && udevadm trigger` или перелогиниться.
- **Окно для автокомментариев:** X11 — `_NET_ACTIVE_WINDOW` + `_NET_WM_NAME` (xcb); Wayland — нельзя, 0 и пустой
  заголовок (автокомментарий только по паузе 10 с). Свои окна (`Recorder::Context::ownWindow`) —
  `QApplication::activeWindow()`, работает и на Wayland.
- **`layoutKeyName`/`toUnicode`/`capsLock`/`clearDeadKey`:** через ту же keymap и текущую группу (Caps — по LED
  состоянию xkb/`EVIOCGLED`); без бэкенда — US, как сейчас.
- **Зависимости:** `xkbcommon` (обязательно на Linux), `xkbcommon-x11` + `xcb-xkb` + `xcb` (опционально, X11),
  `Qt6::DBus` (опционально, KDE). В CMake — `pkg_check_modules`, макросы `TS_HAVE_X11`, `TS_HAVE_DBUS`.
- **Тесты без устройств** (не в чужих окнах, без SendInput): вынести перевод в чистый класс (вход —
  `code, value, timeUs, injected`, группа; выход — `HookEvent`), keymap из имён (`us,ru`, `us(intl)` — нужен
  `xkb-data`). `tests/tst_evdev.cpp` (только Linux): `a`, Shift+a, группа 1 → «ф», BS/Ctrl+BS/Delete/Enter, флаги
  модификаторов (нажатие без своего бита, отпускание с ним), цифровой блок с NumLock и без, расширенные,
  автоповтор, мёртвая клавиша → «é», injected. Плюс сверка с golden: прогнать записи `tests/golden/*.tsf` через
  перевод (scan → evdev-код → событие) и сравнить флаги/символы с файлом.
- После: удалить `third_party/libuiohook` и ветку libuiohook в `KeyboardHook.cpp` (на macOS — шаг 6).


</details>

### 5a. Запись показывается так, как её набрали — сделано
Просьба пользователя (2026-10-05): `.tsf` с Windows на Linux должен выглядеть так, как его набрали, при совместимости
со старыми файлами. Символы и так хранятся в записях готовыми (`ch`) — текст и статистика от ОС просмотра не зависят
(`tst_orig` на Linux побитный). От ОС зависели только имена клавиш без символа: теперь у документа есть платформа
записи (`TsfDocument::platform`, ключ заголовка `Platform=Linux|macOS`, на Windows не пишется; нет ключа — Windows), и
имена берутся по ней (`[LWin]`/`[LSuper]`/`[LCmd]`, `[LOption]`, `Option+`) — в тексте, списке клавиш, клавограмме,
подписях гистограмм. Новая запись и журнал — платформа, где запущен порт. Подробно — `re/tsf_format.md`. Что оригинал
пропускает строку `Platform=` — по разбору (`Values[]`, незнакомая строка не данные), запуском оригинала не проверено.
Тесты: `tst_tsf::platform`, `tst_recalc` (имена и текст), `tst_ui::opensAndPaints`.

### 6. macOS: свой бэкенд — сделано (собирает и проверяет только CI и ручной запуск)
Сделано так (`src/platform/mac/KeyboardHookMac.cpp`, только C API — без Objective-C):
- **Tap:** `CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, keyDown|keyUp|
  flagsChanged)` в своём потоке (QoS user-interactive) со своим `CFRunLoop`; отключённый системой tap
  (`DisabledByTimeout`/`ByUserInput`) включается снова. Остановка — флаг + `CFRunLoopStop`, цикл крутится короткими
  `CFRunLoopRunInMode` по 0,25 с (остановка до входа в цикл не теряется), `join`. Событие собирается целиком в потоке
  tap и уходит в поток GUI очередью — GUI не тормозит клавиатуру системы.
- **Разрешение** «Мониторинг ввода»: `CGPreflightListenEventAccess`; нет — `CGRequestListenEventAccess` (диалог
  системы, один раз), `failed` с инструкцией и проверка раз в 2 с; разрешили — запись начинается сама (`started`).
- **Время** — `hookNowUs()` в колбэке (`steady_clock`).
- **Коды:** `Keyboard::macToScan`/`scanToMac` в ядре (тест на всех ОС — `tst_platform::macCodes`): Command → Win,
  Option → Alt, Help → Insert, Clear → NumLock, Fn — пропуск; ISO-клавиатура (`KBGetLayoutType`) — `§` и `` ` ``
  меняются местами. VK — `scanToVk(scan, numLock = true)` (цифровой блок Mac всегда печатает цифры).
- **Модификаторы** — `flagsChanged` по битам устройства (`NX_DEVICE*KEYMASK`); флаги — по зажатым до события, как
  Windows. CapsLock — одно событие на переключение → нажатие и отпускание сразу.
- **Injected:** `kCGEventSourceStateID` ≠ `kCGEventSourceStateHIDSystemState`.
- **Символы:** `UCKeyTranslate` по данным текущего источника ввода (`kTISPropertyUnicodeKeyLayoutData`); TIS читается
  в главном потоке (и тип клавиатуры `LMGetKbdType`), обновляется по `kTISNotifySelectedKeyboardInputSourceChanged`,
  в поток tap — копия под мьютексом. Option участвует в символе (å, мёртвые клавиши), Command — нет. С Ctrl — символ
  по VK, как ToUnicodeEx (как на Linux). Мёртвые клавиши — `deadKeyState`: ожидание → DeadKey, chars = −1, символ — с
  `kUCKeyTranslateNoDeadKeysMask`; не сложилось → два символа. Управляющие (стрелки, F-клавиши, Forward Delete 0x7F,
  Clear) — без символа, Enter цифрового блока 0x03 → 0x0D.
- **Окно:** `CGWindowListCopyWindowInfo`, первое окно слоя 0; заголовок — имя программы (+ `kCGWindowName`, если
  система его даёт: без «Записи экрана» не даёт, разрешение не просим); кэш 200 мс.
- **`layoutKeyName`/`toUnicode`/`capsLock`** — тем же `UCKeyTranslate`; Caps —
  `CGEventSourceFlagsState(HIDSystemState)`.
- **Сборка:** `CMAKE_OSX_DEPLOYMENT_TARGET` 12.0 (минимум Qt 6.8), bundle id `org.typingstatistics.TypingStatistics`
  (разрешение TCC привязано к нему), `resources/macos/Info.plist.in`, `app.icns` (из `app.png`). Фреймворки
  ApplicationServices, Carbon, CoreFoundation. **libuiohook удалён** из `third_party` вместе с
  `KeyboardHookUiohook.cpp`.
- **Не проверено ничем, кроме чтения кода:** что `CGEventTapCreate` без разрешения возвращает NULL (а не «немой» tap),
  значение `kCGEventSourceStateID` у событий других программ, перестановка ISO-клавиш, мёртвые клавиши, имя окна.

<details><summary>План шага 6 (как задумывалось)</summary>

Почему: у libuiohook на macOS активный tap (`kCGEventTapOptionDefault`, `darwin/input_hook.c:1179`) и
`dispatch_sync` в главный поток на каждый символ (`:277`) — пока GUI занят (пересчёт), тормозит клавиатура всей
системы, через ~1 с tap отключается по таймауту; `stop()` с `join` в главном потоке может зависнуть. Требует
Accessibility.
- Только C API (без Objective-C): `CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
  kCGEventTapOptionListenOnly, маска keyDown|keyUp|flagsChanged)` в своём потоке с `CFRunLoop`; разрешение —
  «Мониторинг ввода»: `CGPreflightListenEventAccess()` / `CGRequestListenEventAccess()` (10.15+).
  `kCGEventTapDisabledByTimeout` → `CGEventTapEnable(tap, true)`. Остановка — `CFRunLoopStop` + `join`.
- Время — `CGEventGetTimestamp` (нс с загрузки, mach) → перевести в шкалу `steady_clock` (на macOS это
  `mach_continuous_time`/`clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` — проверить, какая у libc++; при сомнении брать
  `nowUs()` в колбэке).
- Коды: `kCGKeyboardEventKeycode` (виртуальные коды Mac, `kVK_*`) → scan set-1 и VK таблицей (~110 строк;
  ANSI-раскладка, ISO-клавиша `kVK_ISO_Section` → 0x56; Command → Win, Option → Alt, Fn — пропуск).
- Модификаторы — события `flagsChanged`: нажатие/отпускание по биту устройства (`NX_DEVICELSHIFTKEYMASK` 0x2,
  RSHIFT 0x4, LCTL 0x1, RCTL 0x2000, LALT 0x20, RALT 0x40, LCMD 0x8, RCMD 0x10). CapsLock приходит одним событием на
  переключение — писать нажатие и сразу отпускание.
- Injected: `kCGEventSourceStateID` ≠ `kCGEventSourceStateHIDSystemState` (проверить вручную).
- Символы: `UCKeyTranslate` с данными раскладки (`kTISPropertyUnicodeKeyLayoutData`); TIS — только в главном
  потоке: кэшировать `CFDataRef` раскладки в главном потоке и обновлять по
  `kTISNotifySelectedKeyboardInputSourceChanged` (distributed notification center), в поток tap — под мьютексом.
  Мёртвые клавиши — `deadKeyState` `UCKeyTranslate`. Forward Delete → без символа.
- Окно: `CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly)`, первое окно слоя 0 → номер окна как id и
  `kCGWindowOwnerName` (заголовок окна `kCGWindowName` требует «Запись экрана» — не просить, брать имя программы).
- `layoutKeyName`/`toUnicode`/`capsLock` (`CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState) &
  kCGEventFlagMaskAlphaShift`) — через тот же кэш раскладки.
- Сборка: `MACOSX_BUNDLE_GUI_IDENTIFIER` (bundle id, иначе разрешения TCC сбрасываются при каждой пересборке),
  `MACOSX_BUNDLE_ICON_FILE` + `app.icns` (из `resources/app.png`, `iconutil` — в CI), Info.plist
  (`NSHumanReadableCopyright`, `LSMinimumSystemVersion` 11.0). Фреймворки: ApplicationServices, Carbon (TIS,
  UCKeyTranslate), CoreFoundation.
- Здесь macOS не собрать: код писать предельно аккуратно, собирает CI (`macos-14` arm64, `macos-13` Intel).

</details>

### 7. CI и упаковка — сделано (CI ни разу не запускался: remote нет)
Сделано так:
- **`.github/workflows/ci.yml`:** Windows (`windows-latest`, Qt 6.8.3 `win64_mingw` + `tools_mingw1310` из
  `jurplel/install-qt-action`, MinGW Qt — первым в PATH; Release, ctest; артефакт — один exe на статическом qtbase
  (`ci/windows/static-qt.sh`, ~40 мин при промахе кэша `actions/cache`, проверка `objdump`: только DLL Windows));
  Ubuntu 24.04 (Qt 6.8.3 `linux_gcc_64`; apt: xkbcommon, xkbregistry, xcb-xkb, GL, xkb-data, fonts-liberation): GCC +
  AppImage и Clang с `TS_SOFT_EXT80=ON`; macOS `macos-15` (arm64, программная Ext80) и `macos-15-intel` (x86_64,
  аппаратная) — Qt `clang_64`, ctest, `macdeployqt`, подпись ad-hoc, `.dmg`. Везде `-Wall -Wextra`, ctest с
  `QT_QPA_PLATFORM=offscreen`. Если раннер `macos-15-intel` уберут — заменить на сборку x86_64 на arm64-раннере
  (`CMAKE_OSX_ARCHITECTURES=x86_64`, тесты под Rosetta).
- **Linux `install()`:** exe → `bin`, `.desktop` (`org.typingstatistics.TypingStatistics`, он же
  `setDesktopFileName` — Wayland берёт по нему иконку окна), иконки 32 и 256 (увеличение без сглаживания: у оригинала
  только 32×32), udev-правило → `TS_UDEV_RULES_DIR` (`/usr/lib/udev/rules.d`). Пакетам дистрибутивов этого достаточно.
- **AppImage** (`ci/linux/appimage.sh <build>`): `cmake --install` в AppDir, linuxdeploy + linuxdeploy-plugin-qt.
  Правила udev в AppImage нет (нужен root): без доступа программа сама показывает команды установки. Платформенный
  плагин — xcb (на Wayland работает через XWayland; запись от этого не зависит — она через evdev).
- **macOS `.dmg`:** не нотаризован — первый запуск через «Открыть» в контекстном меню или
  `xattr -dr com.apple.quarantine`. Подпись ad-hoc меняется с каждой сборкой: macOS может снова спросить «Мониторинг
  ввода» после обновления (у разрешения TCC — подпись программы; постоянная нужна Developer ID).

<details><summary>План шага 7 (как задумывалось)</summary>

- `.github/workflows/ci.yml`: матрица `windows-latest` (Qt 6.8.3 mingw через `jurplel/install-qt-action`, MinGW
  из того же действия), `ubuntu-24.04` (Qt 6.8.3 gcc_64 из install-qt-action — в нём есть приватные заголовки и
  QtDBus; apt: `libxkbcommon-dev libxkbcommon-x11-dev libxcb-xkb-dev libgl-dev xkb-data fonts-liberation`),
  `macos-14` и `macos-13` (Qt 6.8.3 clang_64). Модули: qtmultimedia. `ctest` с `QT_QPA_PLATFORM=offscreen`,
  `-Wall -Wextra`. Отдельная задача Ubuntu с `-DTS_SOFT_EXT80=ON` (`tst_orig`, `tst_hist`, `tst_ext80`).
- Linux `install()`: exe, `.desktop`, иконка, udev-правило. Перенести Dockerfile из этого файла в `ci/`.
- Обновить CLAUDE.md: «Сборка» (Linux/macOS, зависимости), «Структура» (Ext80, AppPaths, бэкенды, tst_ext80,
  tst_evdev), «Технический долг» п. 1 — закрыт, этот файл — в «Следующие шаги».

</details>

## Таблица аудита (статус)

| № | Где | ОС | Что | Статус |
|---|---|---|---|---|
| 1 | `third_party/QXlsx/CMakeLists.txt:23` | Linux | `Qt6::GuiPrivate` → нужен `qt6-base-private-dev` | **сделано** (CLAUDE.md) |
| 2 | `CMakeLists.txt:62` | Linux | `lrelease` не находится вне PATH | **сделано** |
| 3 | libuiohook `x11/system_properties.c:476` | Linux | без дисплея деструктор `XtCloseDisplay(NULL)` → libXt `exit(1)` (код выхода `tst_ui` = 1); конструктор открывает X до `main` | **сделано** |
| 4 | `MainWindow.cpp:690`, `KeyboardHook.cpp:352` | Linux, macOS | `failed` не подключён: «Ts: ON», но ничего не пишется | **сделано** |
| 5 | `KeyboardHook.cpp:347` | X11, macOS | гонка press/typed — теряются символы | **сделано** (шаги 5, 6) |
| 6 | libuiohook `x11/input_helper.c:1644` | X11 | xkb-состояние не следит за сменой раскладки окружением — кириллица латиницей | **сделано** |
| 7 | `KeyboardHook.cpp:322` | X11, macOS | флаги модификаторов инвертированы относительно Windows | **сделано** (шаги 5, 6) |
| 8 | libuiohook `keycode_to_unicode` | X11, macOS | Delete → символ 0x7F в тексте | **сделано** (шаги 5, 6) |
| 9 | `KeyboardHook.cpp:245` `vcToVk` | X11, macOS | цифровой блок без NumLock, F13–F24 → vk 0; Pause/NumLock Extended перепутан | **сделано** (шаги 5, 6) |
| 10 | ветка libuiohook | X11, macOS | нет мёртвых клавиш/Compose, нет Injected | **сделано** (шаги 5, 6) |
| 11 | `KeyboardHook.cpp:379` | Linux, macOS | `foregroundWindow/Title` — заглушки | **сделано** (шаги 5, 6) |
| 12 | `KeyboardHook.cpp:389` | Linux, macOS | `layoutKeyName/toUnicode/capsLock` — только US | **сделано** (шаги 5, 6) |
| 13 | libuiohook `darwin/input_hook.c:1179,277` | macOS | активный tap + `dispatch_sync`: лаги системы, таймаут, зависание на выходе | **сделано** (шаг 6) |
| 14 | `Video.cpp:35` | Linux, macOS | пути видео из Windows-`.tsf` не находятся | снято (видео убрано) |
| 15 | `NumberFormat.cpp`, `long double` | arm64, MSVC | другие половинки, NaN → UB | **сделано** (Ext80) |
| 16 | `MainWindow.cpp:117,1421`, `ExtraStatsWindow.cpp:66` | Linux, macOS | ini и журнал в папке exe | **сделано** |
| 17 | `main.cpp:35` | Linux, macOS | стиль и шрифт платформы при абсолютной геометрии; тёмная тема KDE | **сделано** |
| 18 | `GraphWidget.cpp:93,213,589`, `HistogramWindow.cpp:60`, `KlavogramWidget.cpp:64,73` | macOS | шрифты в pt мельче на 25 % | **сделано** |
| 19 | Arial/Courier New | Linux | без `fonts-liberation` — DejaVu шире | **сделано** (шаг 3; AppImage шрифты не несёт — `fonts-liberation` в списке проверок) |
| 20 | `MainWindow.cpp:531`, `LiveStatsWindow.cpp:86` | Wayland, GNOME | трей, свёрнутое состояние, позиции, «поверх всех» | трей — **сделано**; остальное — ограничение Wayland |
| 21 | `CMakeLists.txt:49` | macOS | `.app` без bundle id, иконки, plist; нет `install()` для Linux | **сделано** (шаги 6, 7) |
| 22 | libuiohook `CMakeLists.txt:223` | macOS | `CMAKE_OSX_DEPLOYMENT_TARGET 10.5` | **снято** (libuiohook удалён) |
| 23 | `tst_recorder.cpp:41`, `GraphPanels.cpp:247` | все | предупреждения `-Wall -Wextra` | **сделано** |

Проверено и в порядке: `.tsf` пишется с явным CRLF через свой cp1251 в двоичном режиме, `.tsj` — двоичный; golden с
кириллицей в именах открываются на Linux; тесты проходят в локали C.UTF-8; импорт реестра закрыт `#ifdef`;
QSettings — 58 ключей-литералов без пар, различающихся регистром (реестр регистр не различает, INI/plist —
различают); `char` со знаком/без — ни на что не влияет; экспорт через `QDesktopServices` переносим; FFmpeg-бэкенд
самоотрисовываемые виджеты не зависят от
палитры и рисуют без промежуточных pixmap (на Retina чётко).

## Ручные проверки (для пользователя)

- **Установка на Linux:** AppImage из CI или `cmake --install`; без udev-правила — сообщение с командами, после них
  запись начинается без перезапуска; иконка в меню и на окне (Wayland); шрифты с `fonts-liberation` и без.
- **sway и Hyprland:** раскладки из конфига (`input * xkb_layout us,ru` / `kb_layout = us,ru`), переключение — символы
  в записи по текущей раскладке; автокомментарий при смене окна (заголовок окна приходит от композитора).
- **Linux X11 и Wayland (GNOME, KDE):** запись в терминале, браузере, родных Wayland-программах; RU/EN с
  переключением сочетанием окружения и мышью; быстрый набор 1–2 мин (не теряются ли символы); Delete, цифровой
  блок без NumLock, AltGr, мёртвые клавиши; хоткеи (Super может перехватить окружение); udev-правило и сообщение
  без него; закрытие во время набора; вид форм, тёмная тема, масштаб 125/150 %; трей; куда пишутся ini и журнал.
- **macOS (Intel и arm64):** первый запуск — запрос «Мониторинг ввода», работа после разрешения без перезапуска;
  RU/EN, символы с Option, Delete и Fn+Delete, CapsLock; нет ли лагов клавиатуры в других программах при
  пересчёте большого файла (`re/scripts/gen_big.py`); Cmd+Q во время набора; размеры шрифтов и чёткость; числа
  golden-файлов против Windows (ListView2, Form3, Form4); запуск из «Загрузок» (ini и журнал пишутся).
