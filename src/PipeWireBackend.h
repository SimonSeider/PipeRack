#pragma once
#include "AudioBackend.h"

struct PwPriv;

class PipeWireBackend : public AudioBackend
{
    Q_OBJECT
public:
    explicit PipeWireBackend(QObject *parent = nullptr);
    ~PipeWireBackend() override;

    static bool isAvailable();

    QString displayName() const override { return QStringLiteral("PipeWire"); }
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

private:
    PwPriv *d = nullptr;
};
