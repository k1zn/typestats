#pragma once

#include "core/MainStats.h"

// Translated captions for the core, which itself speaks the language of the original.
namespace Texts {
StatsUnits units();
QStringList statsRowNames(); // one per MainStats row

// Values of the "Language" key. The source texts are Russian; every other language is a translation.
QStringList languages();
// The stored language or, without the key, the one of the system.
QString currentLanguage();
}
