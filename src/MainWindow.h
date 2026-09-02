#pragma once
#include <QMainWindow>
#include <QVector>
#include "AudioBackend.h"

class QElapsedTimer;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class RackButton;
class RackUnit;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AudioBackend *backend, QWidget *parent = nullptr);
    ~MainWindow() override;

    void restoreSession();

protected:
    void closeEvent(QCloseEvent *) override;
    void paintEvent(QPaintEvent *) override;

private slots:
    void onAddCable();
    void onRemoveRequested(RackUnit *unit);
    void onRenameRequested(RackUnit *unit, const QString &newName);
    void onGainChanged(RackUnit *unit, float linear, bool muted);
    void onInjectRequested(RackUnit *unit, const AudioSource &src);
    void onSourcesChanged();
    void onTopologyChanged();
    void onBackendLost(const QString &reason);
    void onTick();

private:
    RackUnit *addUnit(const CableConfig &cfg, bool announceFailure);
    bool openCable(RackUnit *unit, QString *error);
    void closeCable(RackUnit *unit);
    void renumber();
    void updateEmptyState();
    void saveSession() const;
    QString uniqueId() const;
    QString uniqueName() const;

    AudioBackend *m_backend = nullptr;
    QWidget *m_rackHost = nullptr;
    QVBoxLayout *m_rackLayout = nullptr;
    QScrollArea *m_scroll = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_badge = nullptr;
    QWidget *m_addRow = nullptr;
    RackButton *m_add = nullptr;
    QVector<RackUnit *> m_units;
    QElapsedTimer *m_clock = nullptr;
    bool m_lost = false;
    bool m_restoring = false;
};
