#pragma once
#include "AudioBackend.h"

struct PaPriv;

class PulseBackend : public AudioBackend
{
    Q_OBJECT
public:
    explicit PulseBackend(QObject *parent = nullptr);
    ~PulseBackend() override;

    static bool isAvailable();

    QString displayName() const override { return QStringLiteral("PulseAudio"); }
    QString versionInfo() const override;

    bool start(QString *error) override;
    void stop() override;

    CableHandle *createCable(const CableConfig &cfg, QString *error) override;
    void destroyCable(CableHandle *handle) override;
    void applyGain(CableHandle *handle, float linear, bool muted) override;

    QList<AudioSource> availableSources() const override;
    bool setInjectedSource(CableHandle *handle, const AudioSource &src,
                           QString *error) override;

    void notifyTopology();
    void notifySources();
    void notifyLost(const QString &reason);

private:
    PaPriv *d = nullptr;
};
