#pragma once

#include "Recalc.h"

// The video attached to a recording (re/video.md): which moment of it goes with the klavogram.
namespace Video {

// Position of the video, ms, for the klavogram scrolled to `scrollMs` (drawing time of its left edge):
// the time of the left edge since the first record, plus `shiftMs`; never negative. In the first
// fragment it is the original's trunc(scroll) + shift; after a split pause the real time is used,
// not the gap squeezed to 200 ms on the klavogram.
qint64 positionMs(const TextModel &m, float scrollMs, int shiftMs);

// Start of the frame the moment `ms` falls into (AVIStreamTimeToSample, clamped to the last frame), ms.
// A player showing the frame nearest to a position shows exactly that frame there. Without a frame
// rate the moment itself is returned; a duration of 0 is unknown.
qint64 frameStartMs(qint64 ms, double fps, qint64 durationMs);

// The file of an attached video: a relative name is taken from the folder of the recording.
QString resolvePath(const QString &recordingDir, const QString &name);

}
