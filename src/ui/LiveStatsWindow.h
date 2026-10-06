#pragma once

#include "core/Recorder.h"

#include <QWidget>

class QLabel;
class QSplitter;

// "Оперативная статистика" (Form10): speed and error rate of what is being typed right now,
// in a small window that stays on top. See re/recording.md.
class LiveStatsWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LiveStatsWindow(QWidget *parent = nullptr);

    void setStats(const LiveStats &s);
    // Speeds painted with the first and the last colour of the rainbow (opLoSpeed, opHiSpeed).
    void setSpeedRange(int lo, int hi);
    static QColor speedColor(double t);
    // The backgrounds of the theme (Look::colors).
    void updateColors();

    void loadSettings();
    void saveSettings() const;

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    QLabel *m_speed, *m_errors, *m_status;
    QSplitter *m_split;
    int m_lo = 200, m_hi = 500;
    LiveStats m_stats;
};
