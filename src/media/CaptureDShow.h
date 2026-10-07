#pragma once

// Windows: the cameras Media Foundation does not see - DirectShow source filters with no device behind them, such
// as OBS Virtual Camera (re/webcam.md, "Захват"). Their ids start with "dshow:".

#include "Capture.h"

#include <atomic>

namespace DShow {

bool isId(const QString &id);
QList<CaptureDevice> cameras();
// Captures from the camera until `stop`; frames, started() and failed() are emitted from `camera`.
void run(Camera *camera, const QString &id, QSize want, int fps, const std::atomic<bool> &stop);

} // namespace DShow
