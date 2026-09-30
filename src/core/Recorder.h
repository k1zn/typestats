#pragma once

#include "KeyRecord.h"

#include <functional>
#include <optional>

// Turns the events of the keyboard hook into records: hotkeys, auto comments, the filter of
// releases without a press, the running statistics. Port of OnKeyEvent (0x40a7a4), see re/recording.md.

// One event of the hook. `flags` are the record flags the hook can tell by itself.
struct HookEvent
{
    qint64 timeUs = 0;    // monotonic time
    quint32 flags = 0;
    char16_t ch = 0;      // the character of a press; with two characters the last one
    int chars = 0;        // characters the press produced: 0, 1, 2; below zero for a dead key
    char16_t firstCh = 0; // chars == 2: the dead key that did not combine with this one
};

struct RecorderSettings
{
    bool capture = true;       // CheckBox1 "Вкл"
    bool globalOnOff = true;   // F8+F9 / F9+F8 switch the capture
    bool globalClear = true;   // LCtrl+LWin clears the recording
    bool autoComments = false; // comment with the time and the window title after a pause or a window switch
    int splitMs = 2000;        // "Пауза разбиения"
    bool byPauses = false;     // "Разбивать по паузам"
    bool liveVisible = false;  // the running statistics window is shown
};

// "Оперативная статистика" (Form10).
struct LiveStats
{
    float speed = 0.0f;        // characters per minute
    float errorPercent = 0.0f; // runs of BackSpace per character
    quint32 count = 0;         // characters typed, less the erased ones
    quint64 timeUs = 0;        // time between them, pauses excluded
};

class Recorder
{
public:
    struct Context
    {
        bool ownWindow = false;        // the program's own window has the focus: nothing is recorded
        quint64 foregroundWindow = 0;  // identifies the active window
        std::function<QString()> comment; // text of an auto comment, asked for only when one is due
    };
    struct Outcome
    {
        bool recorded = false;           // a record was appended
        std::optional<bool> setCapture;  // F8+F9 hotkeys
        bool clear = false;              // LCtrl+LWin
        bool toggleLive = false;         // Ctrl+Alt+O
        bool liveReset = false;          // LCtrl+RShift: show the zeroed statistics at once
        bool liveChanged = false;
    };

    Outcome handle(const HookEvent &e, const RecorderSettings &s, const Context &c, KeyRecords &records);

    const LiveStats &live() const { return m_live; }
    void resetLive();

private:
    void lap(qint64 timeUs);
    void undoLap() { m_last = m_prev; }
    bool held(quint8 scan) const { return m_down[scan >> 3] & (1 << (scan & 7)); }
    void updateLive(const HookEvent &e, const RecorderSettings &s);

    std::optional<qint64> m_last, m_prev; // the hook timer: the last two events that counted
    quint32 m_dtUs = 0;
    bool m_lctrl = false, m_f8 = false, m_f9 = false;
    quint8 m_down[32] = {};               // pressed keys by scan code
    quint64 m_foreground = 0;
    int m_deadKeyRecord = -1;

    LiveStats m_live;
    std::optional<qint64> m_liveLast;
    quint32 m_livePauses = 0, m_liveErrorRuns = 0;
    bool m_liveInBackspaces = false;
};
