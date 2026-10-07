#include "PulseSimple.h"

#include <dlfcn.h>

namespace Pulse {

const Api &Api::get()
{
    static const Api api = [] {
        Api a;
        void *lib = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!lib)
            return a;
        a.open = reinterpret_cast<decltype(a.open)>(dlsym(lib, "pa_simple_new"));
        a.read = reinterpret_cast<decltype(a.read)>(dlsym(lib, "pa_simple_read"));
        a.write = reinterpret_cast<decltype(a.write)>(dlsym(lib, "pa_simple_write"));
        a.flush = reinterpret_cast<decltype(a.flush)>(dlsym(lib, "pa_simple_flush"));
        a.free = reinterpret_cast<decltype(a.free)>(dlsym(lib, "pa_simple_free"));
        a.ok = a.open && a.read && a.write && a.flush && a.free;
        return a;
    }();
    return api;
}

} // namespace Pulse
