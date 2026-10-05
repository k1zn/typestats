# Запись: хук, обработка события, оперативная статистика

Порт — `src/platform/KeyboardHook` (хук и сборка `flags`), `src/core/Recorder` (логика `OnKeyEvent`),
`src/ui/LiveStatsWindow` (Form10). Биты `flags` — в `tsf_format.md`, журнал — в `journal.md`.

## Хук `KeyboardHookProc` (0x404598, WH_KEYBOARD_LL, ставится в `FormCreate`)

Только `nCode == 0` и WM_KEYDOWN/KEYUP/SYSKEYDOWN/SYSKEYUP. По порядку:

1. `dt = Timer_LapUs(&g_hookTimer)` (0x43ef48): мкс с прошлого «круга»; таймер помнит и позапрошлую отметку
   (`prev = last; last = now`), чтобы круг можно было отменить (см. ниже).
2. `g_ownWindowFocused` = активное окно — Form1, Form3 или Form10; для Form9 («Ввод текста») — только на Esc и F2.
3. `flags`: `LLKHF_EXTENDED` → 0x4000, `LLKHF_INJECTED` → 0x1000, отпускание → 0x2000.
4. `vkCode == 0xE7` (VK_PACKET): `flags |= 0x10000000`, `ch = scanCode`, **ни VK, ни скан-кода в flags нет**;
   сразу `OnKeyEvent`.
5. Иначе `flags |= scanCode & 0xFF`. Раскладка — окна с фокусом в потоке активного окна (`GetGUIThreadInfo` →
   `GetKeyboardLayout`). Состояние клавиатуры: `GetKeyboardState`, затем Shift/Ctrl/Alt — по `GetAsyncKeyState`
   (и биты 0x4000000 / 0x800 / 0x400 в `flags`), CapsLock — по `GetKeyState`, обе Win — сброшены.
6. На нажатии `n = ToUnicodeEx(vk, scan, state, buf[2], 0, hkl)`:
   - `n ≠ 0`: `n = min(n, 2)`; `flags |= 0x1000000`; `ch = buf[n − 1]` (при `n < 0` — `buf[0]`). Если запомнена запись
     мёртвой клавиши (`g_deadKeyIndex ≥ 0`): `n < 2` → `flags |= 0x8000000`; иначе у той записи снимается 0x20000000
     и её `ch = buf[0]` (мёртвая клавиша не сложилась с этой буквой: два отдельных символа);
   - `g_deadKeyIndex = −1`;
   - `n > 0` и перед этим была мёртвая клавиша — `ToAsciiEx` с её кодами: состояние мёртвой клавиши в очереди ввода
     восстанавливается (его съел `ToUnicodeEx`), приложение под хуком получит составной символ;
   - `n < 0` (мёртвая клавиша): `ToAsciiEx` с теми же кодами (сбросить состояние), запомнить её коды и Shift/Alt/Ctrl,
     `flags |= 0x20000000`.
7. `n == 0` (и любое отпускание) → `flags |= 0x8000`. Зажата любая Win → `flags |= 0x80000000`. `flags |= vk << 16`.
8. `OnKeyEvent(Form1, dt, flags, ch)`, затем `CallNextHookEx`.

## `OnKeyEvent` (0x40a7a4)

`vk = flags >> 16 & 0xFF`, `scan = flags & 0xFF`, `down = !(flags & 0x2000)`. По порядку:

1. LCtrl (0xA2): `g_lctrlDown = down`.
2. F8 (0x77): `g_f8Down = down`; если «GlobalOnOff», `down` и зажата F9 → «Вкл» **снимается**; выход.
   F9 (0x78): `g_f9Down = down`; если «GlobalOnOff», `down` и зажата F8 → «Вкл» **ставится**; выход.
   (Круг таймера при этом не отменяется.)
3. «GlobalClear», зажат LCtrl, LWin (0x5B), `down` → «Очистить» (`Button2Click`); выход.
4. Зажат LCtrl и RShift (0xA1) → сброс оперативной статистики (`FUN_00402c70(1)`); событие обрабатывается дальше.
5. `down`, в карте нажатых есть скан-коды 0x38 (Alt) и 0x1D (Ctrl), `scan == 0x18` (O) → показать/скрыть Form10;
   событие обрабатывается дальше.
6. Автокомментарий: («AutoComments» и (`dt > 10 с` или сменилось активное окно)) или (зажат LCtrl и RCtrl (0xA3)):
   запоминается активное окно; комментарий = `DateTimeToStr(Now) + " " + заголовок` окна верхнего уровня
   (`GetParent` до конца, `GetWindowTextA`, 80 байт).
7. Если это **не** отпускание клавиши, отмеченной в карте нажатых (`g_keyDownBitmap`, по скан-коду):
   - «Вкл» снято или своё окно в фокусе → круг таймера отменяется (`last = prev`: время копится до следующего
     записанного события), выход;
   - `flags & 0x20000000` → `g_deadKeyIndex` = номер будущей записи;
   - `down` и Form10 видима → оперативная статистика (ниже).

   Отпускание клавиши, нажатие которой записано, записывается всегда — даже если перехват уже выключен.
8. Карта нажатых: нажатие ставит бит (автоповтор записывается как есть); отпускание клавиши без бита — круг таймера
   отменяется, выход; иначе бит снимается.
9. «JournalOn» → запись в журнал. Запись `{dt, flags, ch, comment}` — в вектор. `g_lastKeyTick = GetTickCount()`,
   `g_needRecalc = true`.

`Timer1` (100 мс, `Timer1Timer` 0x4145f0): если `g_needRecalc` и активно окно Form1 (или Form9 и с последней клавиши
прошло > 800 мс) — `Recalculate(this, 1)`. То есть текст и графики строятся, когда пользователь возвращается в окно
программы. Там же `FUN_0042a37c(this, 1)` — отложенное обновление Form10.

## Оперативная статистика (Form10)

На нажатии, если Form10 видима: `bs = vk == 8`; `chr = !bs && длина KeyDisplayName == 1`.
```
если bs или chr:  d = Timer_LapUs(&opTimer)                   // мкс с прошлого символа/BackSpace
    d > «Пауза разбиения» · 1000:  «Разбивать по паузам» → сброс всего (FUN_00402c70(0))
                                   иначе, если sumTime ≠ 0 → pauses += 1
    иначе sumTime += d
chr:  inBsSeries = 0; count += 1
bs:   если count ≠ 0: count −= 1; если !inBsSeries: inBsSeries = 1; errSeries += 1
speed = 0; err = 0
count > 1:  sumTime > 1000 → speed = (count − 1 − pauses) · 6e7 / sumTime   (float)
            err = errSeries · 100 / count
```
Показ (`FUN_0042a37c`): не чаще одного раза в 5 тиков Timer1 (сброс по LCtrl+RShift показывается сразу).
- Label1 (Arial 37 px, жирный, фон clSilver): `trunc(speed)`; цвет текста — радуга по
  `t = (speed − opLoSpeed) / (opHiSpeed − opLoSpeed)` (умолчания 200 и 500; при равных — чёрный), `FUN_0042a17c`,
  `p(x) = trunc(255 · x^0.4)`:

  | t | R | G | B |
  |---|---|---|---|
  | ≤ 0.2 (t < 0 → 0) | p((0.2 − t)·5) | 0 | 255 |
  | ≤ 0.4 | 0 | p(1 − (0.4 − t)·5) | 255 |
  | ≤ 0.6 | 0 | 255 | p((0.6 − t)·5) |
  | ≤ 0.8 | p(1 − (0.8 − t)·5) | 255 | 0 |
  | > 0.8 (t > 1 → 1) | 255 | p((1 − t)·5) | 0 |
- Label2: `FloatToStrF(err, ffFixed, 15, 2) + "%"`.
- StatusBar: `count + " " + <суффикс из .lng, по умолчанию пусто> + " " + FormatTime(sumTime / 1000, 1)`.

Окно: bsSizeToolWin, поверх всех окон, 168×157, белое; ключи пресета `opWinLeft/Top/Width/Height`, `opSect2Height`
(высота Label2), `opVisible` (0), `opLoSpeed` (200), `opHiSpeed` (500).

## Порт

- Windows: собственный WH_KEYBOARD_LL в потоке GUI, сборка `flags` как выше (включая восстановление мёртвой клавиши
  через `ToAsciiEx`). Linux — evdev + xkbcommon, macOS — listen-only CGEventTap + `UCKeyTranslate`
  (`re/crossplatform.md`, шаги 5–6): те же флаги (модификаторы — по зажатым до события, extended, injected,
  мёртвые клавиши), символы — как дал бы ToUnicodeEx.
- `dt` — 32 бита мкс: пауза длиннее 71 минуты у оригинала переполняется, порт ограничивает её `0xFFFFFFFF`.
- «Своё окно» — любое активное окно самого приложения.
