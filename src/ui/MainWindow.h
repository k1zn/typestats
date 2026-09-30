#pragma once

#include "core/FingerZones.h"
#include "core/Recalc.h"
#include "core/TsfFile.h"

#include <QWidget>

class KlavogramWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QScrollBar;
class QSpinBox;
class QTableWidget;
class QToolButton;
class TextView;

// The main window (Form1); geometry from re/forms_geometry.txt.
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens a .tsf file or a .tsj journal.
    bool openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QWidget *createToolBar();
    QToolButton *toolButton(QWidget *panel, int n, int x, int y, int h, const QString &hint);
    void loadSettings();
    void saveSettings() const;

    RecalcOptions options() const;
    void recalculate();
    void updateStats();
    void updateKeyList();
    void setDocument(const TsfDocument &doc, const QString &title, bool damaged);

    void open();
    void openJournal();
    void save();
    void clear();

    TsfDocument m_doc;
    QString m_path; // empty: not saved yet
    TextModel m_model;
    FingerZoneSchemes m_schemes;

    TextView *m_text = nullptr;
    QWidget *m_graph = nullptr;
    QScrollBar *m_graphScroll = nullptr;
    KlavogramWidget *m_klav = nullptr;
    QTableWidget *m_stats = nullptr;
    QTableWidget *m_keys = nullptr;
    QLabel *m_damaged = nullptr;

    QCheckBox *m_capture = nullptr;
    QCheckBox *m_onlyText = nullptr;
    QCheckBox *m_byPauses = nullptr;
    QSpinBox *m_pause = nullptr;
    QComboBox *m_presets = nullptr;
    QComboBox *m_fingers = nullptr;
    QToolButton *m_saveButton = nullptr;
};
