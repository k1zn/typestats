#pragma once

#include <QString>

struct TsfDocument;

// Integrity signature stored as "signature=" in .tsf files.
namespace TsfSignature {
QString compute(const TsfDocument &doc);
}
