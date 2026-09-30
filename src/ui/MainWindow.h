#pragma once

#include "core/FingerZones.h"
#include "core/Recalc.h"
#include "core/TsfFile.h"

#include <QWidget>

class AxisPanel;
class GraphWidget;
class KlavogramWidget;
class LegendPanel;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QScrollBar;
class QSpinBox;
class QSplitter;
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
    void showEvent(QShowEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    QWidget *createToolBar();
    QToolButton *toolButton(QWidget *panel, int n, int x, int y, int h, const QString &hint);
    void loadSettings();
    void saveSettings() const;

    RecalcOptions options() const;
    void recalculate();
    void updateStats();
    void updateKeyList();
    void klavogramMoved();
    void graphMoved();
    void syncGraphScrollBar();
    qint64 drawTimeOfElement(int element) const;
    void createGraphPanels();
    void toggleAxisPanel();
    void toggleLegend();
    void showAxisMenu(const QPoint &globalPos);
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
    GraphWidget *m_graph = nullptr;
    LegendPanel *m_legend = nullptr;
    AxisPanel *m_axisPanel = nullptr;
    bool m_legendPlaced = false;
    QScrollBar *m_graphScroll = nullptr;
    KlavogramWidget *m_klav = nullptr;
    QTableWidget *m_stats = nullptr;
    QTableWidget *m_keys = nullptr;
    QLabel *m_damaged = nullptr;
    QSplitter *m_leftSplit = nullptr;
    QSplitter *m_mainSplit = nullptr;

    QCheckBox *m_capture = nullptr;
    QCheckBox *m_onlyText = nullptr;
    QCheckBox *m_byPauses = nullptr;
    QSpinBox *m_pause = nullptr;
    QComboBox *m_presets = nullptr;
    QComboBox *m_fingers = nullptr;
    QToolButton *m_saveButton = nullptr;
};
