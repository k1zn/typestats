#pragma once

#include <QString>

// Layout-independent keyboard helpers (US reference layout, scan code set 1).
namespace Keyboard {
quint8 vkToScan(quint8 vk);
}
