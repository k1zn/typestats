#pragma once

#include "MediaClip.h"

// The webcam recording as a WebM file (AV1 + Opus) that browsers and players open (re/webcam.md). Packets are copied:
// a cut clip starts at its key frame, before the start of the block.
namespace Webm {
QByteArray write(const MediaClip &clip);

// av1C (AV1CodecConfigurationRecord) from a key frame: the sequence header OBU with its profile and level. Empty
// when the packet has no sequence header.
QByteArray av1Config(QByteArrayView keyFrame);
}
