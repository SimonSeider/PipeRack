#pragma once

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <atomic>
#include "ScopeRing.h"

#include <cmath>

static constexpr int kMaxChannels = 8;
static constexpr float kUnityKnobPos = 0.8f;

inline float knobToLinear(float pos)
{
    pos = std::fmax(0.0f, std::fmin(1.0f, pos));
    const float r = pos / kUnityKnobPos;
    return r * r * r;
}

inline float linearToKnob(float linear)
{
    if (linear <= 0.0f)
        return 0.0f;
    return std::fmin(1.0f, std::cbrt(linear) * kUnityKnobPos);
}

inline float linearToDb(float linear)
{
    return linear <= 0.00001f ? -120.0f : 20.0f * std::log10(linear);
}

struct AudioSource
{
    QString node;
    QString description;
    bool monitor = false;

    bool isNull() const { return node.isEmpty(); }
    bool operator==(const AudioSource &o) const { return node == o.node; }
};
Q_DECLARE_METATYPE(AudioSource)

struct CableConfig
{
    QString id;
    QString name;
    int channels = 2;
    float gain = 1.0f;
    bool muted = false;

    AudioSource inject;
};

class CableHandle
{
public:
    virtual ~CableHandle() = default;

    virtual QString sinkName() const = 0;
    virtual QString sourceName() const = 0;
    virtual bool live() const = 0;

    virtual ScopeRing *scope() = 0;
    virtual int channels() const = 0;
    virtual int sampleRate() const = 0;

    virtual float takePeak(int channel) = 0;

    virtual int playbackClients() const = 0;
    virtual int captureClients() const = 0;

    virtual AudioSource injectedSource() const = 0;
};

class AudioBackend : public QObject
{
    Q_OBJECT
public:
    explicit AudioBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~AudioBackend() override = default;

    virtual QString displayName() const = 0;
    virtual QString versionInfo() const = 0;

    virtual bool start(QString *error) = 0;
    virtual void stop() = 0;

    virtual CableHandle *createCable(const CableConfig &cfg, QString *error) = 0;
    virtual void destroyCable(CableHandle *handle) = 0;

    virtual void applyGain(CableHandle *handle, float gain, bool muted) = 0;

    virtual QList<AudioSource> availableSources() const = 0;

    virtual bool setInjectedSource(CableHandle *handle, const AudioSource &src,
                                   QString *error) = 0;

signals:
    void topologyChanged();
    void sourcesChanged();
    void backendLost(QString reason);
};
