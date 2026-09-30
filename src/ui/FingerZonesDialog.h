#pragma once

#include "core/FingerZones.h"

#include <QDialog>

// "Клавиатура" (Tkbd): the editor of a finger layout, see re/finger_zones.md. A click on a finger of
// the palette picks it, the left button gives a key to that finger, the right one makes the key the
// finger's home position. The built-in layout is shown but cannot be changed.
class FingerZonesDialog : public QDialog
{
    Q_OBJECT
public:
    FingerZonesDialog(const QString &name, const FingerZones &zones, QWidget *parent = nullptr);

    const FingerZones &zones() const { return m_zones; }

    static QColor fingerColor(int finger);

private:
    friend class TstUi;
    class Keyboard;
    class Palette;

    FingerZones m_zones;
    int m_finger = 0; // the finger picked in the palette
    Keyboard *m_keyboard;
    Palette *m_palette;
};
