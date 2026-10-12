#pragma once

#include <functional>

class QWidget;

// The texts of the interface follow the language (Texts::setLanguage): `set` gives them now, and again whenever the
// window `owner` gets QEvent::LanguageChange. It lives as long as the window. Controls placed by hand (not in a layout)
// keep the size of their first text: `set` resizes them.
void translated(QWidget *owner, std::function<void()> set);
