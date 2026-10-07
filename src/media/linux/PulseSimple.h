#pragma once

// PulseAudio's simple API (PipeWire answers it too), loaded at run time: the program starts without libpulse, the
// sound is then off. Only the declarations used, as libpulse-simple.so.0 has them (its ABI is stable).

#include <cstddef>
#include <cstdint>

namespace Pulse {

struct Simple; // pa_simple

enum Format : int { S16LE = 3, Float32LE = 5 };  // pa_sample_format
enum Direction : int { Playback = 1, Record = 2 }; // pa_stream_direction

struct SampleSpec // pa_sample_spec
{
    int format;
    uint32_t rate;
    uint8_t channels;
};

struct BufferAttr // pa_buffer_attr
{
    uint32_t maxlength, tlength, prebuf, minreq, fragsize;
};

struct Api
{
    Simple *(*open)(const char *server, const char *name, int dir, const char *dev, const char *stream,
                    const SampleSpec *spec, const void *map, const BufferAttr *attr, int *error) = nullptr;
    int (*read)(Simple *s, void *data, size_t bytes, int *error) = nullptr;
    int (*write)(Simple *s, const void *data, size_t bytes, int *error) = nullptr;
    int (*flush)(Simple *s, int *error) = nullptr;
    void (*free)(Simple *s) = nullptr;
    bool ok = false;

    static const Api &get();
};

} // namespace Pulse
