#include "Recorder.h"

#include "KeyName.h"

#include <algorithm>
#include <limits>

void Recorder::lap(qint64 timeUs)
{
    const qint64 dt = m_last ? timeUs - *m_last : 0;
    m_dtUs = quint32(std::clamp<qint64>(dt, 0, std::numeric_limits<quint32>::max()));
    m_prev = m_last;
    m_last = timeUs;
}

void Recorder::resetLive()
{
    m_live = {};
    m_livePauses = m_liveErrorRuns = 0;
    m_liveInBackspaces = false;
}

void Recorder::updateLive(const HookEvent &e, const RecorderSettings &s)
{
    const bool backspace = ((e.flags & KeyRecord::VkMask) >> 16) == Vk::Back;
    const bool character = !backspace && keyDisplayName(e.flags, e.ch).size() == 1;
    if (backspace || character) {
        const bool pause = !m_liveLast || e.timeUs - *m_liveLast > qint64(s.splitMs) * 1000;
        if (!pause)
            m_live.timeUs += quint64(e.timeUs - *m_liveLast);
        else if (s.byPauses)
            resetLive();
        else if (m_live.timeUs != 0)
            ++m_livePauses;
        m_liveLast = e.timeUs;
    }
    if (character) {
        m_liveInBackspaces = false;
        ++m_live.count;
    } else if (backspace && m_live.count != 0) {
        --m_live.count;
        if (!m_liveInBackspaces) {
            m_liveInBackspaces = true;
            ++m_liveErrorRuns;
        }
    }
    m_live.speed = m_live.errorPercent = 0.0f;
    if (m_live.count > 1) {
        if (m_live.timeUs > 1000)
            m_live.speed = float(quint32(m_live.count - 1 - m_livePauses)) * 6e7f / float(m_live.timeUs);
        m_live.errorPercent = float(m_liveErrorRuns) * 100.0f / float(m_live.count);
    }
}

Recorder::Outcome Recorder::handle(const HookEvent &e, const RecorderSettings &s, const Context &c, KeyRecords &records)
{
    Outcome out;
    lap(e.timeUs);
    quint32 flags = e.flags;
    const quint8 vk = quint8((flags & KeyRecord::VkMask) >> 16);
    const quint8 scan = quint8(flags & KeyRecord::ScanMask);
    const bool down = !(flags & KeyRecord::KeyUp);

    // What the hook does with the dead key it remembers: a press that gave one character is marked,
    // one that gave two hands the first of them back to the record of the dead key.
    if (down && !(flags & KeyRecord::Packet)) {
        if (e.chars != 0 && m_deadKeyRecord >= 0) {
            if (e.chars < 2) {
                flags |= KeyRecord::SingleChar;
            } else if (m_deadKeyRecord < records.size()) {
                records[m_deadKeyRecord].flags &= ~quint32(KeyRecord::DeadKey);
                records[m_deadKeyRecord].ch = e.firstCh;
            }
        }
        m_deadKeyRecord = -1;
    }

    // Hotkeys.
    if (vk == Vk::LControl)
        m_lctrl = down;
    if (vk == Vk::F8) {
        m_f8 = down;
        if (s.globalOnOff && down && m_f9) {
            out.setCapture = false;
            return out;
        }
    }
    if (vk == Vk::F9) {
        m_f9 = down;
        if (s.globalOnOff && down && m_f8) {
            out.setCapture = true;
            return out;
        }
    }
    if (s.globalClear && m_lctrl && vk == Vk::LWin && down) {
        out.clear = true;
        return out;
    }
    if (m_lctrl && vk == Vk::RShift) {
        resetLive();
        out.liveReset = out.liveChanged = true;
    }
    if (down && held(0x38) && held(0x1D) && scan == 0x18) // Ctrl+Alt+O
        out.toggleLive = true;

    QString comment;
    if ((s.autoComments && (m_dtUs > 10'000'000 || c.foregroundWindow != m_foreground))
        || (m_lctrl && vk == Vk::RControl)) {
        m_foreground = c.foregroundWindow;
        if (c.comment)
            comment = c.comment();
    }

    // The release of a recorded press always gets through; everything else only while capturing.
    if (down || !held(scan)) {
        if (!s.capture || c.ownWindow) {
            undoLap();
            return out;
        }
        if (flags & KeyRecord::DeadKey)
            m_deadKeyRecord = int(records.size());
        if (down && s.liveVisible) {
            updateLive(e, s);
            out.liveChanged = true;
        }
    }
    if (down) {
        m_down[scan >> 3] |= quint8(1 << (scan & 7));
    } else {
        if (!held(scan)) {
            undoLap();
            return out;
        }
        m_down[scan >> 3] &= quint8(~(1 << (scan & 7)));
    }

    KeyRecord r;
    r.dtUs = m_dtUs;
    r.flags = flags;
    r.ch = e.ch;
    r.comment = comment;
    records.append(r);
    out.recorded = true;
    return out;
}
