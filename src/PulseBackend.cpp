#include "PulseBackend.h"
#include "ScopeMeter.h"

#include <pulse/pulseaudio.h>

#include <QByteArray>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QMultiHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>

static constexpr int kScopeRate = 48000;

static const char *pulsePositionsFor(int channels)
{
    switch (channels)
    {
    case 1:
        return "mono";
    case 4:
        return "front-left,front-right,rear-left,rear-right";
    case 6:
        return "front-left,front-right,front-center,lfe,rear-left,rear-right";
    case 8:
        return "front-left,front-right,front-center,lfe,rear-left,rear-right,side-left,side-right";
    case 2:
    default:
        return "front-left,front-right";
    }
}

static QString paQuote(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return out;
}

static bool isOurDevice(const QString &name)
{
    return name.startsWith(QLatin1String("piperack."));
}

struct PaCable : public CableHandle, public ScopeMeter
{
    QString id;
    QString label;
    QString sinkNode;
    QString sourceNode;
    QString monitorNode;
    int cfgChannels = 2;

    uint32_t nullModule = PA_INVALID_INDEX;
    uint32_t remapModule = PA_INVALID_INDEX;

    uint32_t injectModule = PA_INVALID_INDEX;
    AudioSource injectSrc;

    pa_stream *stream = nullptr;
    uint32_t scopeIndex = PA_INVALID_INDEX;

    uint32_t sinkIndex = PA_INVALID_INDEX;
    uint32_t sourceIndex = PA_INVALID_INDEX;

    std::atomic<int> playClients{0};
    std::atomic<int> capClients{0};

    float gain = 1.0f;
    bool muted = false;

    QString sinkName() const override { return sinkNode; }
    QString sourceName() const override { return sourceNode; }
    bool live() const override { return nullModule != PA_INVALID_INDEX && remapModule != PA_INVALID_INDEX; }
    ScopeRing *scope() override { return &ring; }
    int channels() const override { return int(rtChannels.load(std::memory_order_relaxed)); }
    int sampleRate() const override { return negotiatedRate.load(std::memory_order_relaxed); }
    float takePeak(int channel) override { return ScopeMeter::takePeak(channel); }
    int playbackClients() const override { return playClients.load(std::memory_order_relaxed); }
    int captureClients() const override { return capClients.load(std::memory_order_relaxed); }
    AudioSource injectedSource() const override { return injectSrc; }
};

struct PaPriv
{
    PulseBackend *owner = nullptr;

    pa_threaded_mainloop *loop = nullptr;
    pa_context *ctx = nullptr;
    bool running = false;

    QString serverVersion;

    bool pipewirePulse = false;
    QList<PaCable *> cables;

    QMultiHash<QString, uint32_t> strays;

    QHash<uint32_t, AudioSource> pendingSources;
    QList<AudioSource> sources;

    QHash<uint32_t, int> pendingPlay;
    QHash<uint32_t, int> pendingCap;
    bool countInFlight = false;
    bool countAgain = false;
};

static QString paPropList(const PaPriv *p, const QList<QPair<QString, QString>> &props)
{
    QStringList parts;
    parts.reserve(props.size());
    for (const QPair<QString, QString> &kv : props)
    {
        if (p->pipewirePulse)
        {
            QString value = kv.second;
            value.replace(QLatin1Char('\''), QChar(0x2019));
            parts += QStringLiteral("%1='%2'").arg(kv.first, value);
        }
        else
        {
            parts += QStringLiteral("%1=\"%2\"").arg(kv.first, paQuote(kv.second));
        }
    }

    const QString body = parts.join(QLatin1Char(','));
    return p->pipewirePulse ? QStringLiteral("\"%1\"").arg(body) : body;
}

static bool waitFor(PaPriv *p, pa_operation *op)
{
    if (!op)
        return false;
    bool ok = true;
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
    {
        if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(p->ctx)))
        {
            ok = false;
            break;
        }
        pa_threaded_mainloop_wait(p->loop);
    }
    if (ok)
        ok = pa_operation_get_state(op) == PA_OPERATION_DONE;
    pa_operation_unref(op);
    return ok;
}

static void recount(PaPriv *p);
static void pinUnity(PaPriv *p, const pa_source_output_info *i);
static void pinUnity(PaPriv *p, const pa_sink_input_info *i);
static void onModuleInfo(pa_context *, const pa_module_info *i, int eol, void *userdata);

static void onSinkInputInfo(pa_context *, const pa_sink_input_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    if (!eol && i)
    {
        for (const PaCable *c : p->cables)
        {
            if (c->injectModule != PA_INVALID_INDEX && i->owner_module == c->injectModule)
            {
                pinUnity(p, i);
                return;
            }
        }
        p->pendingPlay[i->sink] += 1;
        return;
    }
    for (PaCable *c : p->cables)
        c->playClients.store(c->sinkIndex == PA_INVALID_INDEX
                                 ? 0
                                 : p->pendingPlay.value(c->sinkIndex, 0),
                             std::memory_order_relaxed);
    p->pendingPlay.clear();
    pa_threaded_mainloop_signal(p->loop, 0);
}

static void onSourceOutputInfo(pa_context *, const pa_source_output_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    if (!eol && i)
    {
        for (const PaCable *c : p->cables)
        {
            const bool ours = c->scopeIndex == i->index ||
                              (c->remapModule != PA_INVALID_INDEX &&
                               i->owner_module == c->remapModule) ||
                              (c->injectModule != PA_INVALID_INDEX &&
                               i->owner_module == c->injectModule);
            if (ours)
            {
                pinUnity(p, i);
                return;
            }
        }
        p->pendingCap[i->source] += 1;
        return;
    }
    for (PaCable *c : p->cables)
        c->capClients.store(c->sourceIndex == PA_INVALID_INDEX
                                ? 0
                                : p->pendingCap.value(c->sourceIndex, 0),
                            std::memory_order_relaxed);
    p->pendingCap.clear();

    p->countInFlight = false;
    p->owner->notifyTopology();
    pa_threaded_mainloop_signal(p->loop, 0);

    if (p->countAgain)
    {
        p->countAgain = false;
        recount(p);
    }
}

static void recount(PaPriv *p)
{
    if (p->countInFlight)
    {
        p->countAgain = true;
        return;
    }
    p->countInFlight = true;
    p->pendingPlay.clear();
    p->pendingCap.clear();
    if (pa_operation *o = pa_context_get_sink_input_info_list(p->ctx, onSinkInputInfo, p))
        pa_operation_unref(o);
    if (pa_operation *o = pa_context_get_source_output_info_list(p->ctx, onSourceOutputInfo, p))
        pa_operation_unref(o);
    else
        p->countInFlight = false;
}

static void onSinkInfo(pa_context *, const pa_sink_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    if (!eol && i)
    {
        for (PaCable *c : p->cables)
            if (c->sinkNode == QString::fromUtf8(i->name))
                c->sinkIndex = i->index;
    }
    if (eol)
        pa_threaded_mainloop_signal(p->loop, 0);
}

static void onSourceInfo(pa_context *, const pa_source_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    if (!eol && i)
    {
        const QString name = QString::fromUtf8(i->name);
        for (PaCable *c : p->cables)
            if (c->sourceNode == name)
                c->sourceIndex = i->index;

        if (!isOurDevice(name))
        {
            AudioSource src;
            src.node = name;
            src.description = i->description ? QString::fromUtf8(i->description) : name;
            src.monitor = i->monitor_of_sink != PA_INVALID_INDEX;
            p->pendingSources.insert(i->index, src);
        }
        return;
    }

    QList<AudioSource> settled = p->pendingSources.values();
    p->pendingSources.clear();

    std::sort(settled.begin(), settled.end(), [](const AudioSource &a, const AudioSource &b)
              {
        if (a.monitor != b.monitor)
            return !a.monitor;
        const int byDesc = a.description.compare(b.description, Qt::CaseInsensitive);
        return byDesc != 0 ? byDesc < 0 : a.node < b.node; });

    bool changed = settled.size() != p->sources.size();
    for (int n = 0; !changed && n < settled.size(); ++n)
        changed = settled[n].node != p->sources[n].node ||
                  settled[n].description != p->sources[n].description;
    if (changed)
    {
        p->sources = settled;
        p->owner->notifySources();
    }

    pa_threaded_mainloop_signal(p->loop, 0);
}

static void refreshIndices(PaPriv *p)
{
    p->pendingSources.clear();
    if (pa_operation *o = pa_context_get_sink_info_list(p->ctx, onSinkInfo, p))
        pa_operation_unref(o);
    if (pa_operation *o = pa_context_get_source_info_list(p->ctx, onSourceInfo, p))
        pa_operation_unref(o);
}

static void onSubscribe(pa_context *, pa_subscription_event_type_t t, uint32_t, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK)
    {
    case PA_SUBSCRIPTION_EVENT_SINK:
    case PA_SUBSCRIPTION_EVENT_SOURCE:
        refreshIndices(p);
        recount(p);
        break;
    case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
    case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
        recount(p);
        break;
    default:
        break;
    }
}

static void onContextState(pa_context *ctx, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    const pa_context_state_t st = pa_context_get_state(ctx);
    if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED)
    {
        if (p->running)
        {
            p->running = false;
            p->owner->notifyLost(QString::fromUtf8(
                pa_strerror(pa_context_errno(ctx))));
        }
    }
    pa_threaded_mainloop_signal(p->loop, 0);
}

static void onServerInfo(pa_context *, const pa_server_info *i, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    if (i && i->server_version)
        p->serverVersion = QString::fromUtf8(i->server_version);
    if (i && i->server_name)
        p->pipewirePulse =
            QString::fromUtf8(i->server_name).contains(QLatin1String("PipeWire"), Qt::CaseInsensitive);
    pa_threaded_mainloop_signal(p->loop, 0);
}

struct LoadResult
{
    PaPriv *priv;
    uint32_t index = PA_INVALID_INDEX;
};

static void onModuleLoaded(pa_context *, uint32_t index, void *userdata)
{
    auto *r = static_cast<LoadResult *>(userdata);
    r->index = index;
    pa_threaded_mainloop_signal(r->priv->loop, 0);
}

static uint32_t loadModule(PaPriv *p, const char *name, const QString &args)
{
    LoadResult r{p, PA_INVALID_INDEX};
    const QByteArray a = args.toUtf8();
    if (!waitFor(p, pa_context_load_module(p->ctx, name, a.constData(), onModuleLoaded, &r)))
        return PA_INVALID_INDEX;
    return r.index;
}

static void onSimpleDone(pa_context *, int, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    pa_threaded_mainloop_signal(p->loop, 0);
}

static void unloadModule(PaPriv *p, uint32_t index)
{
    if (index == PA_INVALID_INDEX)
        return;
    waitFor(p, pa_context_unload_module(p->ctx, index, onSimpleDone, p));
}

static void onStreamRead(pa_stream *s, size_t, void *userdata)
{
    auto *c = static_cast<PaCable *>(userdata);
    const void *data = nullptr;
    size_t bytes = 0;

    while (pa_stream_readable_size(s) > 0)
    {
        if (pa_stream_peek(s, &data, &bytes) < 0)
            return;
        if (bytes == 0)
            return;
        if (data)
            c->consume(static_cast<const float *>(data),
                       uint32_t(bytes / sizeof(float) /
                                std::max<uint32_t>(1, c->rtChannels.load(std::memory_order_relaxed))));
        pa_stream_drop(s);
    }
}

static void onStreamState(pa_stream *s, void *userdata)
{
    auto *c = static_cast<PaCable *>(userdata);
    if (pa_stream_get_state(s) == PA_STREAM_READY)
        c->scopeIndex = pa_stream_get_index(s);
}

static void pushVolume(PaPriv *p, PaCable *c)
{
    if (c->sinkNode.isEmpty())
        return;

    pa_cvolume vol;
    pa_cvolume_set(&vol, uint8_t(std::max(1, c->cfgChannels)),
                   pa_sw_volume_from_linear(double(c->gain)));

    const QByteArray sink = c->sinkNode.toUtf8();
    if (pa_operation *o = pa_context_set_sink_volume_by_name(p->ctx, sink.constData(), &vol,
                                                             nullptr, nullptr))
        pa_operation_unref(o);
    if (pa_operation *o = pa_context_set_sink_mute_by_name(p->ctx, sink.constData(),
                                                           c->muted ? 1 : 0, nullptr, nullptr))
        pa_operation_unref(o);
}

PulseBackend::PulseBackend(QObject *parent) : AudioBackend(parent), d(new PaPriv)
{
    d->owner = this;
}

PulseBackend::~PulseBackend()
{
    stop();
    delete d;
}

bool PulseBackend::isAvailable()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains(QStringLiteral("PULSE_SERVER")))
        return true;
    QString dir = env.value(QStringLiteral("XDG_RUNTIME_DIR"));
    return !dir.isEmpty() && QFileInfo::exists(dir + QStringLiteral("/pulse/native"));
}

QString PulseBackend::versionInfo() const
{
    return d->serverVersion.isEmpty() ? QStringLiteral("connected") : d->serverVersion;
}

void PulseBackend::notifyTopology()
{
    QMetaObject::invokeMethod(this, [this]
                              { emit topologyChanged(); }, Qt::QueuedConnection);
}

void PulseBackend::notifySources()
{
    QMetaObject::invokeMethod(this, [this]
                              { emit sourcesChanged(); }, Qt::QueuedConnection);
}

void PulseBackend::notifyLost(const QString &reason)
{
    QMetaObject::invokeMethod(this, [this, reason]
                              { emit backendLost(reason); }, Qt::QueuedConnection);
}

bool PulseBackend::start(QString *error)
{
    auto fail = [&](const QString &msg)
    {
        if (error)
            *error = msg;
        stop();
        return false;
    };

    d->loop = pa_threaded_mainloop_new();
    if (!d->loop)
        return fail(QStringLiteral("Could not create the PulseAudio main loop."));

    pa_mainloop_api *api = pa_threaded_mainloop_get_api(d->loop);
    d->ctx = pa_context_new(api, "PipeRack");
    if (!d->ctx)
        return fail(QStringLiteral("Could not create a PulseAudio context."));

    pa_context_set_state_callback(d->ctx, onContextState, d);

    if (pa_threaded_mainloop_start(d->loop) < 0)
        return fail(QStringLiteral("Could not start the PulseAudio main loop."));

    pa_threaded_mainloop_lock(d->loop);

    if (pa_context_connect(d->ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
    {
        const QString msg = QString::fromUtf8(pa_strerror(pa_context_errno(d->ctx)));
        pa_threaded_mainloop_unlock(d->loop);
        return fail(msg);
    }

    for (;;)
    {
        const pa_context_state_t st = pa_context_get_state(d->ctx);
        if (st == PA_CONTEXT_READY)
            break;
        if (!PA_CONTEXT_IS_GOOD(st))
        {
            const QString msg = QString::fromUtf8(pa_strerror(pa_context_errno(d->ctx)));
            pa_threaded_mainloop_unlock(d->loop);
            return fail(msg.isEmpty() ? QStringLiteral("No PulseAudio server answered.") : msg);
        }
        pa_threaded_mainloop_wait(d->loop);
    }

    d->running = true;
    waitFor(d, pa_context_get_server_info(d->ctx, onServerInfo, d));
    waitFor(d, pa_context_get_module_info_list(d->ctx, onModuleInfo, d));
    waitFor(d, pa_context_get_source_info_list(d->ctx, onSourceInfo, d));

    pa_context_set_subscribe_callback(d->ctx, onSubscribe, d);
    if (pa_operation *o = pa_context_subscribe(
            d->ctx,
            pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
                                   PA_SUBSCRIPTION_MASK_SINK_INPUT |
                                   PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT),
            nullptr, nullptr))
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(d->loop);
    return true;
}

void PulseBackend::stop()
{
    if (d->loop && d->ctx)
    {
        pa_threaded_mainloop_lock(d->loop);
        d->running = false;
        while (!d->cables.isEmpty())
        {
            PaCable *c = d->cables.takeLast();
            if (c->stream)
            {
                pa_stream_set_read_callback(c->stream, nullptr, nullptr);
                pa_stream_set_state_callback(c->stream, nullptr, nullptr);
                pa_stream_disconnect(c->stream);
                pa_stream_unref(c->stream);
            }
            unloadModule(d, c->injectModule);
            unloadModule(d, c->remapModule);
            unloadModule(d, c->nullModule);
            delete c;
        }
        pa_threaded_mainloop_unlock(d->loop);
    }

    if (d->ctx)
    {
        pa_context_set_state_callback(d->ctx, nullptr, nullptr);
        pa_context_disconnect(d->ctx);
        pa_context_unref(d->ctx);
        d->ctx = nullptr;
    }
    if (d->loop)
    {
        pa_threaded_mainloop_stop(d->loop);
        pa_threaded_mainloop_free(d->loop);
        d->loop = nullptr;
    }
    d->sources.clear();
    d->pendingSources.clear();
}

static void onModuleInfo(pa_context *, const pa_module_info *i, int eol, void *userdata)
{
    auto *p = static_cast<PaPriv *>(userdata);
    if (eol || !i)
    {
        pa_threaded_mainloop_signal(p->loop, 0);
        return;
    }
    if (!i->argument)
        return;

    static const QRegularExpression re(
        QStringLiteral("(?:^|\\s)(?:sink_name|source_name|sink|source)="
                       "(piperack\\.[^\\s]+)"));
    QRegularExpressionMatchIterator it = re.globalMatch(QString::fromUtf8(i->argument));
    while (it.hasNext())
        p->strays.insert(it.next().captured(1), i->index);
}

static void reclaimStray(PaPriv *p, const QString &node)
{
    const QList<uint32_t> found = p->strays.values(node);
    p->strays.remove(node);
    for (const uint32_t index : found)
        unloadModule(p, index);
}

static void pinUnity(PaPriv *p, const pa_source_output_info *i)
{
    if (pa_cvolume_is_norm(&i->volume))
        return;

    pa_cvolume unity;
    pa_cvolume_set(&unity, i->volume.channels ? i->volume.channels : 2, PA_VOLUME_NORM);
    if (pa_operation *o = pa_context_set_source_output_volume(p->ctx, i->index, &unity,
                                                              nullptr, nullptr))
        pa_operation_unref(o);
}

static void pinUnity(PaPriv *p, const pa_sink_input_info *i)
{
    if (pa_cvolume_is_norm(&i->volume))
        return;

    pa_cvolume unity;
    pa_cvolume_set(&unity, i->volume.channels ? i->volume.channels : 2, PA_VOLUME_NORM);
    if (pa_operation *o = pa_context_set_sink_input_volume(p->ctx, i->index, &unity,
                                                           nullptr, nullptr))
        pa_operation_unref(o);
}

CableHandle *PulseBackend::createCable(const CableConfig &cfg, QString *error)
{
    if (!d->ctx || !d->loop)
    {
        if (error)
            *error = QStringLiteral("Not connected to PulseAudio.");
        return nullptr;
    }

    auto *c = new PaCable;
    c->id = cfg.id;
    c->label = cfg.name;
    c->cfgChannels = std::clamp(cfg.channels, 1, kMaxChannels);
    c->sinkNode = QStringLiteral("piperack.%1.sink").arg(cfg.id);
    c->sourceNode = QStringLiteral("piperack.%1.source").arg(cfg.id);
    c->monitorNode = c->sinkNode + QStringLiteral(".monitor");
    c->gain = cfg.gain;
    c->muted = cfg.muted;
    c->setFormat(kScopeRate, c->cfgChannels);

    const QString label = paQuote(cfg.name);
    const QString map = QLatin1String(pulsePositionsFor(c->cfgChannels));

    pa_threaded_mainloop_lock(d->loop);

    reclaimStray(d, c->sinkNode);
    reclaimStray(d, c->sourceNode);

    c->nullModule = loadModule(
        d, "module-null-sink",
        QStringLiteral("sink_name=%1 channels=%2 channel_map=%3 rate=%4 "
                       "sink_properties=%5")
            .arg(c->sinkNode)
            .arg(c->cfgChannels)
            .arg(map)
            .arg(kScopeRate)
            .arg(paPropList(d, {
                                   {QStringLiteral("device.description"),
                                    QStringLiteral("%1 (Cable Output)").arg(label)},
                                   {QStringLiteral("device.icon_name"), QStringLiteral("audio-card")},
                                   {QStringLiteral("device.class"), QStringLiteral("sound")},
                                   {QStringLiteral("state.default-volume"), QStringLiteral("1.0")},
                                   {QStringLiteral("state.restore-props"), QStringLiteral("false")},
                               })));

    if (c->nullModule == PA_INVALID_INDEX)
    {
        pa_threaded_mainloop_unlock(d->loop);
        if (error)
            *error = QStringLiteral("PulseAudio refused to load module-null-sink.");
        delete c;
        return nullptr;
    }

    c->remapModule = loadModule(
        d, "module-remap-source",
        QStringLiteral("source_name=%1 master=%2 channels=%3 channel_map=%4 "
                       "master_channel_map=%4 remix=no source_properties=%5")
            .arg(c->sourceNode, c->monitorNode)
            .arg(c->cfgChannels)
            .arg(map)
            .arg(paPropList(d, {
                                   {QStringLiteral("device.description"),
                                    QStringLiteral("%1 (Cable Input)").arg(label)},
                                   {QStringLiteral("device.icon_name"),
                                    QStringLiteral("audio-input-microphone")},
                                   {QStringLiteral("state.default-volume"), QStringLiteral("1.0")},
                                   {QStringLiteral("state.restore-props"), QStringLiteral("false")},
                               })));

    if (c->remapModule == PA_INVALID_INDEX)
    {
        unloadModule(d, c->nullModule);
        pa_threaded_mainloop_unlock(d->loop);
        if (error)
            *error = QStringLiteral("PulseAudio refused to load module-remap-source.");
        delete c;
        return nullptr;
    }

    pa_sample_spec spec;
    spec.format = PA_SAMPLE_FLOAT32NE;
    spec.rate = kScopeRate;
    spec.channels = uint8_t(c->cfgChannels);

    pa_proplist *props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "PipeRack");
    pa_proplist_sets(props, PA_PROP_MEDIA_ROLE, "filter");
    pa_proplist_sets(props, PA_PROP_MEDIA_NAME, "PipeRack scope");

    c->stream = pa_stream_new_with_proplist(d->ctx, "PipeRack scope", &spec, nullptr, props);
    pa_proplist_free(props);

    if (c->stream)
    {
        pa_stream_set_read_callback(c->stream, onStreamRead, c);
        pa_stream_set_state_callback(c->stream, onStreamState, c);

        pa_buffer_attr attr;
        attr.maxlength = uint32_t(-1);
        attr.tlength = uint32_t(-1);
        attr.prebuf = uint32_t(-1);
        attr.minreq = uint32_t(-1);
        attr.fragsize = uint32_t(kScopeRate / 100) * spec.channels * sizeof(float);

        const QByteArray monitor = c->monitorNode.toUtf8();
        if (pa_stream_connect_record(c->stream, monitor.constData(), &attr,
                                     pa_stream_flags_t(PA_STREAM_ADJUST_LATENCY |
                                                       PA_STREAM_DONT_MOVE)) < 0)
        {
            pa_stream_unref(c->stream);
            c->stream = nullptr;
        }
    }

    d->cables.append(c);
    pushVolume(d, c);
    refreshIndices(d);
    recount(d);

    pa_threaded_mainloop_unlock(d->loop);
    return c;
}

void PulseBackend::destroyCable(CableHandle *handle)
{
    auto *c = static_cast<PaCable *>(handle);
    if (!c || !d->loop || !d->ctx)
        return;

    pa_threaded_mainloop_lock(d->loop);
    d->cables.removeOne(c);

    if (c->stream)
    {
        pa_stream_set_read_callback(c->stream, nullptr, nullptr);
        pa_stream_set_state_callback(c->stream, nullptr, nullptr);
        pa_stream_disconnect(c->stream);
        pa_stream_unref(c->stream);
        c->stream = nullptr;
    }
    unloadModule(d, c->injectModule);
    unloadModule(d, c->remapModule);
    unloadModule(d, c->nullModule);
    pa_threaded_mainloop_unlock(d->loop);

    delete c;
}

void PulseBackend::applyGain(CableHandle *handle, float linear, bool muted)
{
    auto *c = static_cast<PaCable *>(handle);
    if (!c || !d->loop || !d->ctx)
        return;

    pa_threaded_mainloop_lock(d->loop);
    c->gain = std::clamp(linear, 0.0f, 4.0f);
    c->muted = muted;
    pushVolume(d, c);
    pa_threaded_mainloop_unlock(d->loop);
}

QList<AudioSource> PulseBackend::availableSources() const
{
    QList<AudioSource> out;
    if (!d->loop || !d->ctx)
        return out;

    pa_threaded_mainloop_lock(d->loop);
    out = d->sources;
    pa_threaded_mainloop_unlock(d->loop);
    return out;
}

bool PulseBackend::setInjectedSource(CableHandle *handle, const AudioSource &src, QString *error)
{
    auto *c = static_cast<PaCable *>(handle);
    if (!c || !d->loop || !d->ctx)
    {
        if (error)
            *error = QStringLiteral("Not connected to PulseAudio.");
        return false;
    }

    pa_threaded_mainloop_lock(d->loop);

    if (c->injectModule != PA_INVALID_INDEX)
    {
        unloadModule(d, c->injectModule);
        c->injectModule = PA_INVALID_INDEX;
    }
    c->injectSrc = AudioSource();

    if (src.isNull())
    {
        recount(d);
        pa_threaded_mainloop_unlock(d->loop);
        return true;
    }

    c->injectModule = loadModule(
        d, "module-loopback",
        QStringLiteral("source=%1 sink=%2 latency_msec=40 "
                       "source_dont_move=true sink_dont_move=true "
                       "source_output_properties=%3 sink_input_properties=%3")
            .arg(src.node, c->sinkNode,
                 paPropList(d, {
                                   {QStringLiteral("media.name"),
                                    QStringLiteral("%1 injection").arg(c->label)},
                                   {QStringLiteral("state.default-volume"), QStringLiteral("1.0")},
                                   {QStringLiteral("state.restore-props"), QStringLiteral("false")},
                               })));

    if (c->injectModule == PA_INVALID_INDEX)
    {
        pa_threaded_mainloop_unlock(d->loop);
        if (error)
            *error = QStringLiteral("PulseAudio refused to load module-loopback for “%1”.")
                         .arg(src.description);
        return false;
    }

    c->injectSrc = src;
    recount(d);
    pa_threaded_mainloop_unlock(d->loop);
    return true;
}
