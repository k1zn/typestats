# Видео (Form5 «Видео», Form6 «Свойства видео», SpeedButton20/21)

К записи можно прикрепить AVI (например, запись экрана во время набора). Окно «Видео» показывает кадр, соответствующий
левому краю клавограммы; при прокрутке клавограммы кадр меняется. Своего проигрывания нет: только кадр по времени.

## Состояние

| адрес | имя | смысл |
|---|---|---|
| `0x58e200` | `g_videoAttached` | видео прикреплено и открылось |
| `0x5b145c` | `g_attachedVideo` | имя файла (AnsiString), как в `.tsf` — обычно относительно папки `.tsf` |
| `0x5b1454` | `g_videoTimeShift` | сдвиг, мс (int) |
| `0x5b1460` | — | `PAVISTREAM` потока `vids` |
| `0x5b1458` | — | число кадров (`AVIStreamLength`) |
| `0x58e201` | — | `AVIFileInit` уже вызван (`AVIFileExit` — в `FormDestroy` главного окна) |
| `Form5 + 0x2f4` | — | текущий кадр |
| `Form5 + 0x2f8` | — | `PGETFRAME` (`AVIStreamGetFrameOpen(stream, NULL)`) |
| `g_fileDir` | — | папка последнего открытого `.tsf` (`ExtractFilePath` в LoadTsf, с `\` на конце); до первого открытия пусто |

## Открытие (`FUN_00402cd8(path)`)

`AVIStreamOpenFromFileA(&stream, path, 'vids', 0, OF_READ=0, NULL)`; ошибка → `false`. Иначе `AVIStreamInfoA` →
клиентская область Form5 = `rcFrame` (ширина × высота кадра), `length = AVIStreamLength`, `AVIStreamGetFrameOpen`;
`false`, если он вернул NULL. Предыдущий поток не закрывается (утечка).

## Кадр по времени (`FUN_00402e20(ms)`)

Только при `g_videoAttached`: `t = max(0, ms + g_videoTimeShift)`, `frame = AVIStreamTimeToSample(stream, t)`,
`frame ≤ length − 1`. Если кадр изменился — запомнить, вернуть `true`.

Вызывается из `PaintBox3Paint` (0x424d70, каждая перерисовка клавограммы) с `ms = trunc(scroll)` — **время рисования
левого края клавограммы** (`tDraw`, мс, `klavogram.md`); при `true` и видимой Form5 — `TForm5::PaintBox1Paint`.

## Отрисовка (`TForm5::PaintBox1Paint` 0x453b98)

`AVIStreamGetFrame(pgf, frame)` → упакованный DIB; `StretchDIBits` на весь PaintBox1 (он `alClient`, а клиентская
область = размер кадра, так что масштаб 1:1).

## Form5

DFM: `bsToolWindow` (не меняет размер), `fsStayOnTop`, `biSystemMenu | biMinimize`, `Left=380, Top=0`, клиент 534×334
(заменяется размером кадра). Единственный контрол — PaintBox1 (`alClient`).

## Form6 (SpeedButton21Click 0x4297fc)

DFM: `bsSingle`, без системных кнопок (`BorderIcons=[]`), клиент 140×142:
`CheckBox1` «Прикрепить видео» (8,8,121,17), `LabeledEdit1` «Имя файла» (8,48,121,21), `LabeledEdit2`
«Сдвиг времени, мс» (8,88,121,21), `BitBtn1` OK (32,112,75,25). Кнопки «Отмена» нет: OK применяет всегда.

1. Поля ← `g_videoAttached`, `g_attachedVideo`, `IntToStr(g_videoTimeShift)`. Хук снимается на время `ShowModal`.
2. `g_videoAttached = CheckBox1.Checked`. Если да: имя ← Edit1, сдвиг ← `StrToIntDef(Edit2, 0)`,
   `g_videoAttached = open(g_fileDir + имя)`; при успехе — кадр по `trunc(scroll)`, и если он изменился —
   показать Form5 и нарисовать.
3. Не прикреплено → скрыть Form5.

## Остальное

- SpeedButton20Click (0x4297e4): при `g_videoAttached` — показать Form5.
- LoadTsf: текущий кадр = 0; `g_videoAttached = false`; ключ `AttachedVideo` непуст → `VideoTimeShift`
  (`StrToIntDef`, 0), открыть `g_fileDir + имя`, при успехе — показать Form5. Если видео нет, Form5 не скрывается
  (но и не обновляется).
- SaveTsf (не блок): при `g_videoAttached` пишет `AttachedVideo` и, если сдвиг ≠ 0, `VideoTimeShift`. В подпись не входят.
- «Очистить» (Button2Click 0x413ed8): скрыть Form5, `g_videoAttached = false`.
- Язык (`LoadLanguage`): подписи Form5, Form6, CheckBox1, EditLabel обоих полей.

## Порт

`QtMultimedia` вместо VfW (кроссплатформенно; FFmpeg-бэкенд Qt читает AVI с распространёнными кодеками):
`QMediaPlayer` на паузе + `QVideoSink`, кадр рисуется своим виджетом. Ядро — `src/core/Video`: время видео по
положению клавограммы. Отличия:
- **время видео** — абсолютное время левого края клавограммы, отсчитанное от первой записи: в первом фрагменте это
  ровно `tDraw` оригинала (сдвиги из старых `.tsf` подходят), но после паузы разбиения видео не уезжает на
  «сжатую» паузу (у оригинала паузы между фрагментами на клавограмме сжаты до 200 мс, а видео шло по этому сжатому
  времени и расходилось с набором после первой паузы);
- имя файла: относительное — от папки `.tsf` (или от папки, куда запись сохранена), абсолютное — как есть;
- при открытии файла без видео окно «Видео» скрывается; поток предыдущего видео закрывается;
- видео, которое не открылось (нет файла, не тот формат), не прикрепляется, как у оригинала, но имя и сдвиг
  остаются в полях Form6, чтобы их можно было поправить.
