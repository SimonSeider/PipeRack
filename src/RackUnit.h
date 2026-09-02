#pragma once
#include <QList>
#include <QWidget>
#include "AudioBackend.h"

class QLabel;
class QLineEdit;
class Knob;
class LedMeter;
class RackButton;
class WaveformView;

class UsageStrip : public QWidget
{
    Q_OBJECT
public:
    explicit UsageStrip(QWidget *parent = nullptr);

    void setCounts(int playing, int recording);
    void setDeviceNames(const QString &sink, const QString &source);

    QSize sizeHint() const override { return QSize(230, 28); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void refreshTooltip();

    int m_playing = 0;
    int m_recording = 0;
    QString m_sink;
    QString m_source;
};

class SourceStrip : public QWidget
{
    Q_OBJECT
public:
    explicit SourceStrip(QWidget *parent = nullptr);

    void setSources(const QList<AudioSource> &sources);
    void setChosen(const AudioSource &src);

    QSize sizeHint() const override { return QSize(230, 28); }

signals:
    void chosen(const AudioSource &src);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void openMenu();
    void refreshTooltip();
    bool present() const;

    QList<AudioSource> m_sources;
    AudioSource m_chosen;
    bool m_hover = false;
};

class RackUnit : public QWidget
{
    Q_OBJECT
public:
    RackUnit(const CableConfig &cfg, QWidget *parent = nullptr);

    const CableConfig &config() const { return m_cfg; }
    CableHandle *handle() const { return m_handle; }

    void setHandle(CableHandle *handle);
    void setName(const QString &name);
    void setAvailableSources(const QList<AudioSource> &sources);
    void setInjectedSource(const AudioSource &src);
    void setSlotNumber(int n);
    void refreshFromBackend();
    void tick(qreal dtSeconds);

signals:
    void removeRequested(RackUnit *unit);
    void renameRequested(RackUnit *unit, const QString &newName);
    void gainChanged(RackUnit *unit, float linear, bool muted);
    void injectRequested(RackUnit *unit, const AudioSource &src);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void pushGain();
    void updateGainReadout();

    CableConfig m_cfg;
    CableHandle *m_handle = nullptr;
    int m_slot = 1;

    QLineEdit *m_name = nullptr;
    UsageStrip *m_usage = nullptr;
    SourceStrip *m_inject = nullptr;
    QLabel *m_status = nullptr;
    WaveformView *m_scope = nullptr;
    LedMeter *m_meter = nullptr;
    Knob *m_knob = nullptr;
    QLabel *m_gainText = nullptr;
    RackButton *m_mute = nullptr;
    RackButton *m_remove = nullptr;
};
