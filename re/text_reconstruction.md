# Recalculate (0x40ce40): текст, элементы, паузы, клавограмма

Вызывается при любом изменении записей или опций (`g_needRecalc`). Шаги по порядку.

## 1. Нормализация записей
1. Пустой вектор → ничего не делать (кроме очистки).
2. Удалить все ведущие отпускания (`0x2000`); первой записи поставить `dt = 60 000 000`.
3. Сжатие (битовая карта «нажата» по **VK**, 256 бит):
   - отпускание — **всегда остаётся**, бит VK сбрасывается;
   - нажатие — остаётся, если VK ещё не нажат **или** это не модификатор;
     удаляется только автоповтор модификатора. `IsModifierKey` (0x40cd94): VK ∈ {5B, 5C, A0..A5}.
     Бит VK ставится в любом случае;
   - dt удалённых записей прибавляется к следующей оставшейся (сумма u64, в запись пишется u32).

   (Поправка к прежней заметке: отпускания ненажатых клавиш тут **не** удаляются,
   их отсеивает клавограмма — см. п. 5.)

## 2. Разметка стёртых символов (флаг 0x100)
У всех записей снимаются `0x100` и `0x40000000`. Затем проход **с конца** по нажатиям — алгоритм
в `re/metrics.md`, раздел «Разметка стёртых символов». «Символ» здесь — нажатие, у которого
`KeyDisplayName` после перевода в cp1251 имеет длину 1 (`\r` тоже символ). BackSpace определяется
по `VK == 8`; с Ctrl (`0x800`) — Ctrl+BackSpace.

## 3. KeyDisplayName(flags, ch) (0x405fe0)
```
vk = (flags >> 16) & 0xFF
if !(flags & 0x8000):            // есть символ
    s = {8:"BackSpace", 9:"Tab", 0x0D:"\r", 0x1B:"Esc"}.get(vk, "")
else:
    s = таблица по vk, иначе "Unrecognized key":
      13 Pause, 14 Caps Lock, 21 Page Up, 22 Page Down, 23 End, 24 Home, 25 Left, 26 Up,
      27 Right, 28 Down, 2C Print Screen, 2D Insert, 2E Delete, 5D SysMenu, 70..7B F1..F12,
      90 Num Lock, 91 Scroll Lock, A0 LShift, A1 RShift
      5B → return "[LWin]", 5C "[RWin]", A2 "[LCtrl]", A3 "[RCtrl]", A4 "[LAlt]", A5 "[RAlt]"
      (return — сразу, без префиксов и скобок)
if s == "": s = QString(QChar(ch))            // всегда 1 символ
if flags & 0x400: s = "Alt+" + s
if flags & 0x800: s = "Ctrl+" + s
if len(s) > 1:    s = "[" + s + "]"
```
Пример: Ctrl+BackSpace → `[Ctrl+BackSpace]`, Shift → `[LShift]`, Enter → `\r`.

## 4. Проход по записям: элементы и текст
Опции (читает `FUN_0041ab08` через `TRegIniFile("Software")`, секция `TypingStatistics` или
`TypingStatistics\<Profile>`; значения в DFM перекрываются, эффективные умолчания — из кода):
- `split = UpDown1 · 1000` мкс. UpDown1: 200..10000 мс, ключ `Pause`, по умолчанию **2000** (в DFM 500);
- `onlyText` = CheckBox2 «Только текст», ключ `TextOnly`, по умолчанию **1**;
- `byPauses` = CheckBox3 «Разбивать по паузам», ключ `SplitOnEnter`, по умолчанию 0;
- `onlyInjected` = CheckBox6 «Только PCmo» (скрыт, false).

Состояние:
- `pos` — позиция в тексте;
- `elem` — число элементов;
- `klavIdx` — индекс записи клавограммы;
- `acc` — dt с прошлого элемента, мкс, u32;
- `sinceKlav` — dt с прошлой записи клавограммы, double;
- `pendingSplit` — флаг «ждём начала фрагмента»;
- `splitMapIdx` — индекс в маппинге для отложенного начала фрагмента;
- `segStart` — индекс в `pauses`, с которого начался текущий фрагмент.

```
for r in records (index ri):
    acc += r.dt; sinceKlav += r.dt
    if r is release:
        if klav.addRelease(r):  sinceKlav = 0; klavIdx++      // только если VK в списке нажатых
        continue
    // комментарий берётся с первого нажатия, у которого он есть и нет 0x200
    if !(r.flags & 0x200) and !haveComment and r.comment != "":
        haveComment = true; comment = r.comment
        map.push(pos, elem, klavIdx, ri)
    name = KeyDisplayName(r)
    if len(name) > 1 and onlyText and name ∉ {[LShift],[RShift],[BackSpace],[Ctrl+BackSpace]}:
        continue                                                // не попадает даже в клавограмму
    map.push(pos, elem, klavIdx, ri)
    if sinceKlav <= split or klav.hasPressedKeys():
        klav.addPress(r)
    else:
        klav.addSegmentStart(r)        // запись клавограммы с флагом 0x40000000
        pendingSplit = true; splitMapIdx = map.size() − 1
    sinceKlav = 0
    style = (r.flags&0x100 ? 1 : 0) | (r.flags&0x200 ? 2 : 0) | (r.flags&0x1000 ? 8 : 0)
    if (r.flags & 0x100) and name == " ": name = "█"          // U+2588
    if (!onlyText or len(name)==1) and !(r.flags & 0x20000000) and (!onlyInjected or style&8):
        if pendingSplit:
            pendingSplit = false; r.flags |= 0x40000000
            if pos != 0:
                if !byPauses: segs.add("‡", 4); pos += 1        // U+2021, в строку
                else: flushPara(); segs.add("————————", 4); pos = flushPara()   // 8 × U+2014
                for k in [splitMapIdx, map.size()): map.pos[k] = pos
                map.push(pos, elem, klavIdx, ri)
        if haveComment:
            haveComment = false
            if comment == "(…)"   (первый символ '(' и последний ')'):
                segs.add(comment, 0x10); pos += len(comment)    // в строку
            else:
                flushPara(); segs.add(comment, 0x10); pos = flushPara()   // отдельный абзац
            for k in [lastElemMapIdx, map.size()): map.pos[k] = pos
            map.push(pos, elem, klavIdx, ri)
        if name != "\r": segs.add(name, style); pos += len(name)
        else:            pos = flushPara()                     // Enter = новый абзац
        lastElemMapIdx = map.size()
        fingerSeries.push(float(scanToZone[r.flags & 0x7F]))   // DAT_005b1404, таблица DAT_005b1278
        if !(r.flags & 0x40000000): pauses.push(float(0.001 · acc))
        else:
            computeGraphs(segStart)                            // FUN_00403868 — графики прошлого фрагмента
            pauses.push(−2147483648.0)   // и в остальные серии графиков тоже
            segStart = pauses.size()
        textModel.add(name, erased = r.flags&0x100, injected = r.flags&0x1000)
        elem++; acc = 0
    klavIdx++
computeGraphs(segStart); flushPara()
```

`map` — четыре параллельных вектора:
- `DAT_005b1018` — позиция в тексте;
- `DAT_005b1030` — номер элемента;
- `DAT_005b1048` — номер записи клавограммы;
- `DAT_005b1064` — номер сырой записи.

По ним выделение в тексте переводится в элементы и записи (lower_bound).
Позиции `pos` — это смещения RichEdit. TRichEdit у BCB6 — RichEdit 1.0 (класс `RICHEDIT`): разрыв абзаца
хранится как настоящие CR LF, это 2 позиции и в тексте, и в выделении (`EM_GETTEXTLENGTHEX` с
`GTL_NUMCHARS` = длине `WM_GETTEXT`). Поэтому маппинг и стили у оригинала согласованы.
Порт считает разрыв за 1 символ (LF); при сверке позиция порта `q` соответствует
`q + (число разрывов до q)` у оригинала (так делает `re/scripts/diffstand.py`).

**Отображение.** Модель текста — WideString, но RichEdit ANSI (cp1251): символы вне cp1251
заменяются при выводе по best-fit `WideCharToMultiByte`. Стёртый пробел `█` (U+2588, литерал @0x58ea80,
глобальная `DAT_0058e190`) на экране у оригинала выглядит как `-`. Порт показывает `█`, как задумано.
Разделитель `‡` (U+2021, @0x58ea7c, `DAT_0058e18c`) и `—` в cp1251 есть.

**Абзацы (`flushPara`, 0x40c5d0).** Накопленные сегменты склеиваются в строку и дописываются
в конец RichEdit. Если там уже есть текст, перед строкой ставится `\r\n`. Функция возвращает новую
длину текста. Стили сегментов (флаги `style`):

| бит | когда | вид |
|---|---|---|
| 4 или 8 | разделитель фрагментов / injected-ввод | цвет `#0000FF` (синий) |
| 1 (если нет 4/8) | стёртый символ | цвет `#FF0000` (красный) |
| 0x10 | комментарий | цвет `#008000` (зелёный) |
| 2 | помечен (0x200, кнопка «Пометить (Ins)») | подчёркнут |

Остальной текст — чёрный, без подчёркивания.

## 5. Клавограмма (объект `DAT_005b1348`)
Формат записи — в `re/metrics.md`.
- `addPress(r)` (0x4377cc): добавить запись `isDown = 1` с абсолютным временем `t` и VK
  положить в список нажатых.
- `addSegmentStart(r)` (0x4379c0): то же с флагом `0x40000000`. Время для рисования (`+0x10`)
  сдвигается так, чтобы пауза между фрагментами на клавограмме выглядела как 200 мс.
- `addRelease(r)` (0x43720c): если VK есть в списке нажатых — убрать его оттуда и добавить
  запись `isDown = 0`. Иначе запись игнорируется, `false`.

Абсолютное время `t` = `trunc(Σdt)` по всем записям, в мкс (`__ftol` от double-суммы).

## 6. Обновление статистики при выделении
`Memo4SelectionChange` (0x418e2c) → `FUN_00414500`: ставит позицию клавограммы и флаг `DAT_0058e1d6`.
Сам пересчёт — в `ApplicationEvents1Idle` (0x42a2xx): `FUN_00424e64`, `PaintBox3Paint`, ListView1
(`FUN_00404c44`) и, если `FUN_00405188` (синхронизация клавограмма → текст) вернула 0, основная
статистика `FUN_004255a8`. Изменение опций (`UpDown1Click`, `CheckBox2Click`, Enter в Edit1 —
`Edit1KeyPress` 0x4286b4) сразу сохраняет пресет в реестр (`FUN_00406a90`) и зовёт `Recalculate`.

## 7. После прохода
- `FUN_0043ad1c(textModel)` — финализация модели (слова для Form3 и т.п., не разобрано);
- `FUN_00405b34`, `FUN_0040687c` — не разобраны (графики/прочее);
- восстановить курсор RichEdit;
- `PaintBox3Paint` (клавограмма), `FUN_00404c44` (ListView1), `PaintBox1Paint` (график);
- пересчитать Form3/Form4, если они открыты.

## 8. ListView1 «Пауза / Длительность / Клавиша» (0x404c44 + 0x437d98)
Это список нажатий, **видимых на клавограмме**, а не элементов текста. Порт: `src/core/KeyList`.

Состояние клавограммы (`DAT_005b1348`):
- `+0x68` — scroll, float, мс времени рисования (`tDraw`);
- `+0x6c` — масштаб, float, px/мс: по умолчанию 0.25, ограничен 0.04..300;
- `+0x28` — объект холста, у него `+0x28` → TBitmap, ширина `+0x1c` (652 px у пользователя).

При смене выделения `FUN_00414500`: `k = mapKlav[lower_bound(mapPos, SelStart)]`,
`scroll = float(0.001 · (klav[k].tDraw − 100))`. Если `k > size`, берётся последняя запись; при
`k == size` оригинал читает запись за концом вектора.

`FUN_00404c44`: `n = LVM_GETCOUNTPERPAGE` (12 при высоте по умолчанию), затем `FUN_00437d98(klav, …, n)`:
```
start = float(scroll·1000f − 10f); end = float(float(width·1000) / zoom + start)   // мкс tDraw
row[scan] = −1 (256 шт., индекс — СКАН-код, flags & 0xFF); prev = 0; count = 0
for r in klav:
    if n < 0x7fffffff:                    // для «все строки» окно не проверяется
        if tDraw < start: continue
        if tDraw > end: stop
    if release: if row[scan] ≥ 0: duration[row[scan]] = Fixed3((r.t − pressT[scan]) · 0.001L); row[scan] = −1
    else:
        row[scan] = rows; pressT[scan] = r.t
        p = float((r.t − prev) · 0.001L);  pause = p ≤ 59000 ? Fixed3(p) : ""
        key = KeyDisplayName(flags, ch), "\r" → "[Enter]"; duration = ""
        if ++count ≥ n: stop              // у последней строки длительность остаётся пустой
        prev = r.t
pause[0] = ""                             // FUN_00404c44
```
Время (`r.t`) — абсолютное (`+0x00`), окно — по `tDraw` (`+0x10`). Длительность считается в
extended, пауза округляется до float.

## Сверка с оригиналом
`re/scripts/diffstand.py` (pywinauto + свои 32-битные структуры, `re/scripts/win32remote.py`)
запускает оригинал с файлом, читает ListView2, ListView1, текст и стили RichEdit, выделяет
случайные диапазоны и ставит курсор. С `--variants` переключает опции в окне (оригинал сразу
пишет пресет в реестр, стенд снимает снимок `HKCU\Software\TypingStatistics` и восстанавливает его).
Результат пишется в `tests/golden/orig/<имя>.json`; `--offline` сравнивает с ним без запуска
оригинала. `tests/tst_orig.cpp` проверяет по этим JSON ядро: основная статистика, текст, стили,
выделения и ListView1 на 4 golden-файлах × 5 наборов опций совпадают полностью.
