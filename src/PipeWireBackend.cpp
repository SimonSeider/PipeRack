#include "PipeWireBackend.h"
#include "ScopeMeter.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl-module.h>
#include <pipewire/node.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include <QByteArray>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>

static const char *positionsFor(int channels)
{
    switch (channels)
    {
    case 1:
        return "[ MONO ]";
    case 4:
        return "[ FL FR RL RR ]";
    case 6:
        return "[ FL FR FC LFE RL RR ]";
    case 8:
        return "[ FL FR FC LFE RL RR SL SR ]";
    case 2:
    default:
        return "[ FL FR ]";
    }
}

static QString spaQuote(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return out;
}

struct PwCable : public CableHandle, public ScopeMeter
{
    QString id;
    QString label;
    QString sinkNode;
    QString sourceNode;
    QString scopeNode;
    QString injectNode;
    int cfgChannels = 2;

    pw_impl_module *module = nullptr;

    pw_impl_module *injectModule = nullptr;
    AudioSource injectSrc;

    pw_stream *stream = nullptr;
    spa_hook streamHook{};
    bool streamHooked = false;

    pw_proxy *sinkProxy = nullptr;
    uint32_t sinkId = SPA_ID_INVALID;
    uint32_t sourceId = SPA_ID_INVALID;
    uint32_t scopeId = SPA_ID_INVALID;
    uint32_t injectId = SPA_ID_INVALID;

    std::atomic<int> playClients{0};
    std::atomic<int> capClients{0};

    float gain = 1.0f;
    bool muted = false;

    QString sinkName() const override { return sinkNode; }
    QString sourceName() const override { return sourceNode; }
    bool live() const override { return module != nullptr && sinkId != SPA_ID_INVALID; }
    ScopeRing *scope() override { return &ring; }
    int channels() const override { return int(rtChannels.load(std::memory_order_relaxed)); }
    int sampleRate() const override { return negotiatedRate.load(std::memory_order_relaxed); }
    float takePeak(int channel) override { return ScopeMeter::takePeak(channel); }
    int playbackClients() const override { return playClients.load(std::memory_order_relaxed); }
    int captureClients() const override { return capClients.load(std::memory_order_relaxed); }
    AudioSource injectedSource() const override { return injectSrc; }
};

struct PwPriv
{
    PipeWireBackend *owner = nullptr;

    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;

    spa_hook coreHook{};
    spa_hook registryHook{};
    bool coreHooked = false;
    bool registryHooked = false;

    QList<PwCable *> cables;
    QHash<uint32_t, QPair<uint32_t, uint32_t>> links;
    QHash<uint32_t, QString> nodeNames;

    QHash<uint32_t, AudioSource> sources;
};

static bool isOurNode(const QString &name)
{
    return name.startsWith(QLatin1String("piperack."));
}

static bool sourceKindOf(const char *mediaClass, bool *monitor)
{
    if (!mediaClass)
        return false;
    if (std::strcmp(mediaClass, "Audio/Source") == 0 ||
        std::strcmp(mediaClass, "Audio/Source/Virtual") == 0 ||
        std::strcmp(mediaClass, "Audio/Duplex") == 0)
    {
        *monitor = false;
        return true;
    }
    if (std::strcmp(mediaClass, "Audio/Sink") == 0)
    {
        *monitor = true;
        return true;
    }
    return false;
}

static void recount(PwPriv *p)
{
    for (PwCable *c : std::as_const(p->cables))
    {
        if (c->stream && c->scopeId == SPA_ID_INVALID)
        {
            const uint32_t sid = pw_stream_get_node_id(c->stream);
            if (sid != SPA_ID_INVALID)
                c->scopeId = sid;
        }

        QSet<uint32_t> feeding;
        QSet<uint32_t> draining;

        for (auto it = p->links.constBegin(); it != p->links.constEnd(); ++it)
        {
            const uint32_t outNode = it.value().first;
            const uint32_t inNode = it.value().second;
            if (c->sinkId != SPA_ID_INVALID && inNode == c->sinkId &&
                outNode != c->scopeId && outNode != c->injectId)
                feeding.insert(outNode);
            if (c->sourceId != SPA_ID_INVALID && outNode == c->sourceId && inNode != c->scopeId)
                draining.insert(inNode);
        }

        c->playClients.store(feeding.size(), std::memory_order_relaxed);
        c->capClients.store(draining.size(), std::memory_order_relaxed);
    }
    p->owner->notifyTopology();
}

static void pushVolume(PwCable *c)
{
    if (!c->sinkProxy)
        return;

    const int nch = std::max(1, std::min(c->cfgChannels, kMaxChannels));
    float vols[kMaxChannels];
    const float v = c->gain * c->gain * c->gain;
    for (int i = 0; i < nch; ++i)
        vols[i] = v;

    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *pod = (const spa_pod *)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
        SPA_PROP_mute, SPA_POD_Bool(c->muted),
        SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, nch, vols));

    pw_node_set_param((pw_node *)c->sinkProxy, SPA_PARAM_Props, 0, pod);
}

static void onStreamParamChanged(void *userdata, uint32_t id, const spa_pod *param)
{
    auto *c = static_cast<PwCable *>(userdata);
    if (!param || id != SPA_PARAM_Format)
        return;

    uint32_t mediaType = 0, mediaSubtype = 0;
    if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0)
        return;
    if (mediaType != SPA_MEDIA_TYPE_audio || mediaSubtype != SPA_MEDIA_SUBTYPE_raw)
        return;

    spa_audio_info_raw raw{};
    if (spa_format_audio_raw_parse(param, &raw) < 0)
        return;

    c->setFormat(int(raw.rate ? raw.rate : 48000), int(raw.channels ? raw.channels : 2));
}

static void onStreamProcess(void *userdata)
{
    auto *c = static_cast<PwCable *>(userdata);
    pw_buffer *b = pw_stream_dequeue_buffer(c->stream);
    if (!b)
        return;

    spa_data &d0 = b->buffer->datas[0];
    if (d0.data && d0.chunk)
    {
        const uint32_t ch = c->rtChannels.load(std::memory_order_relaxed);
        const uint32_t stride = uint32_t(sizeof(float)) * ch;
        if (stride > 0 && d0.chunk->size >= stride)
        {
            const uint32_t frames = d0.chunk->size / stride;
            const auto *base = static_cast<const uint8_t *>(d0.data) + d0.chunk->offset;
            c->consume(reinterpret_cast<const float *>(base), frames);
        }
    }

    pw_stream_queue_buffer(c->stream, b);
}

static const pw_stream_events kStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .param_changed = onStreamParamChanged,
    .process = onStreamProcess,
};

static void onRegistryGlobal(void *data, uint32_t id, uint32_t permissions,
                             const char *type, uint32_t version, const spa_dict *props)
{
    auto *p = static_cast<PwPriv *>(data);
    if (!props || !type)
        return;

    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0)
    {
        const char *nn = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        if (!nn)
            return;
        const QString name = QString::fromUtf8(nn);
        p->nodeNames.insert(id, name);

        bool monitor = false;
        if (!isOurNode(name) &&
            sourceKindOf(spa_dict_lookup(props, PW_KEY_MEDIA_CLASS), &monitor))
        {
            const char *desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
            if (!desc)
                desc = spa_dict_lookup(props, PW_KEY_NODE_NICK);

            AudioSource src;
            src.node = name;
            src.description = desc ? QString::fromUtf8(desc) : name;
            src.monitor = monitor;
            p->sources.insert(id, src);
            p->owner->notifySources();
        }

        bool touched = false;
        for (PwCable *c : std::as_const(p->cables))
        {
            if (c->sinkNode == name && c->sinkId == SPA_ID_INVALID)
            {
                c->sinkId = id;
                c->sinkProxy = (pw_proxy *)pw_registry_bind(p->registry, id,
                                                            PW_TYPE_INTERFACE_Node,
                                                            PW_VERSION_NODE, 0);
                pushVolume(c);
                touched = true;
            }
            else if (c->sourceNode == name && c->sourceId == SPA_ID_INVALID)
            {
                c->sourceId = id;
                touched = true;
            }
            else if (c->injectNode == name && c->injectId == SPA_ID_INVALID)
            {
                c->injectId = id;
                touched = true;
            }
        }
        if (touched)
            recount(p);
    }
    else if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0)
    {
        const char *on = spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_NODE);
        const char *in = spa_dict_lookup(props, PW_KEY_LINK_INPUT_NODE);
        if (!on || !in)
            return;
        p->links.insert(id, qMakePair(uint32_t(std::strtoul(on, nullptr, 10)),
                                      uint32_t(std::strtoul(in, nullptr, 10))));
        recount(p);
    }
}

static void onRegistryGlobalRemove(void *data, uint32_t id)
{
    auto *p = static_cast<PwPriv *>(data);
    bool touched = false;

    if (p->links.remove(id) > 0)
        touched = true;

    if (p->sources.remove(id) > 0)
        p->owner->notifySources();

    if (p->nodeNames.remove(id) > 0)
    {
        for (PwCable *c : std::as_const(p->cables))
        {
            if (c->sinkId == id)
            {
                c->sinkId = SPA_ID_INVALID;
                if (c->sinkProxy)
                {
                    pw_proxy_destroy(c->sinkProxy);
                    c->sinkProxy = nullptr;
                }
                touched = true;
            }
            if (c->sourceId == id)
            {
                c->sourceId = SPA_ID_INVALID;
                touched = true;
            }
            if (c->injectId == id)
            {
                c->injectId = SPA_ID_INVALID;
                touched = true;
            }
        }
    }

    if (touched)
        recount(p);
}

static const pw_registry_events kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = onRegistryGlobal,
    .global_remove = onRegistryGlobalRemove,
};

static void onCoreError(void *data, uint32_t id, int seq, int res, const char *message)
{
    auto *p = static_cast<PwPriv *>(data);
    if (id == PW_ID_CORE && res == -EPIPE)
        emit p->owner->backendLost(QStringLiteral("Connection to PipeWire was lost: %1")
                                       .arg(QString::fromUtf8(message ? message : "")));
}

static const pw_core_events kCoreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .error = onCoreError,
};

PipeWireBackend::PipeWireBackend(QObject *parent)
    : AudioBackend(parent), d(new PwPriv)
{
    d->owner = this;
}

PipeWireBackend::~PipeWireBackend()
{
    stop();
    delete d;
}

bool PipeWireBackend::isAvailable()
{
    const QByteArray runtime = qgetenv("PIPEWIRE_RUNTIME_DIR").isEmpty()
                                   ? qgetenv("XDG_RUNTIME_DIR")
                                   : qgetenv("PIPEWIRE_RUNTIME_DIR");
    if (runtime.isEmpty())
        return false;
    return QFileInfo::exists(QString::fromLocal8Bit(runtime) + QStringLiteral("/pipewire-0"));
}

QString PipeWireBackend::versionInfo() const
{
    return QString::fromUtf8(pw_get_library_version());
}

void PipeWireBackend::notifyTopology()
{
    emit topologyChanged();
}

void PipeWireBackend::notifySources()
{
    emit sourcesChanged();
}

bool PipeWireBackend::start(QString *error)
{
    if (d->loop)
        return true;

    pw_init(nullptr, nullptr);

    d->loop = pw_thread_loop_new("piperack", nullptr);
    if (!d->loop)
    {
        if (error)
            *error = QStringLiteral("Could not create the PipeWire event loop.");
        return false;
    }

    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    if (!d->context)
    {
        if (error)
            *error = QStringLiteral("Could not create a PipeWire context.");
        stop();
        return false;
    }

    if (pw_thread_loop_start(d->loop) < 0)
    {
        if (error)
            *error = QStringLiteral("Could not start the PipeWire event loop.");
        stop();
        return false;
    }

    pw_thread_loop_lock(d->loop);
    d->core = pw_context_connect(d->context, nullptr, 0);
    if (!d->core)
    {
        pw_thread_loop_unlock(d->loop);
        if (error)
            *error = QStringLiteral("Could not connect to the PipeWire server.");
        stop();
        return false;
    }
    pw_core_add_listener(d->core, &d->coreHook, &kCoreEvents, d);
    d->coreHooked = true;

    d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
    if (d->registry)
    {
        pw_registry_add_listener(d->registry, &d->registryHook, &kRegistryEvents, d);
        d->registryHooked = true;
    }
    pw_thread_loop_unlock(d->loop);

    return true;
}

void PipeWireBackend::stop()
{
    if (!d->loop)
        return;

    pw_thread_loop_lock(d->loop);

    const QList<PwCable *> doomed = d->cables;
    d->cables.clear();
    for (PwCable *c : doomed)
    {
        if (c->stream)
        {
            pw_stream_destroy(c->stream);
            c->stream = nullptr;
        }
        if (c->sinkProxy)
        {
            pw_proxy_destroy(c->sinkProxy);
            c->sinkProxy = nullptr;
        }
        if (c->injectModule)
        {
            pw_impl_module_destroy(c->injectModule);
            c->injectModule = nullptr;
        }
        if (c->module)
        {
            pw_impl_module_destroy(c->module);
            c->module = nullptr;
        }
        delete c;
    }

    if (d->registryHooked)
    {
        spa_hook_remove(&d->registryHook);
        d->registryHooked = false;
    }
    if (d->registry)
    {
        pw_proxy_destroy((pw_proxy *)d->registry);
        d->registry = nullptr;
    }
    if (d->coreHooked)
    {
        spa_hook_remove(&d->coreHook);
        d->coreHooked = false;
    }
    if (d->core)
    {
        pw_core_disconnect(d->core);
        d->core = nullptr;
    }
    pw_thread_loop_unlock(d->loop);

    pw_thread_loop_stop(d->loop);

    if (d->context)
    {
        pw_context_destroy(d->context);
        d->context = nullptr;
    }
    pw_thread_loop_destroy(d->loop);
    d->loop = nullptr;

    d->links.clear();
    d->nodeNames.clear();
    d->sources.clear();
}

CableHandle *PipeWireBackend::createCable(const CableConfig &cfg, QString *error)
{
    if (!d->loop || !d->core)
    {
        if (error)
            *error = QStringLiteral("The PipeWire backend is not running.");
        return nullptr;
    }

    auto *c = new PwCable;
    c->id = cfg.id;
    c->label = cfg.name;
    c->cfgChannels = cfg.channels;
    c->gain = cfg.gain;
    c->muted = cfg.muted;
    c->sinkNode = QStringLiteral("piperack.%1.sink").arg(cfg.id);
    c->sourceNode = QStringLiteral("piperack.%1.source").arg(cfg.id);
    c->scopeNode = QStringLiteral("piperack.%1.scope").arg(cfg.id);
    c->injectNode = QStringLiteral("piperack.%1.inject").arg(cfg.id);
    c->rtChannels.store(uint32_t(std::max(1, cfg.channels)), std::memory_order_relaxed);

    const char *pos = positionsFor(cfg.channels);
    const QString label = spaQuote(cfg.name);

    const QString args = QStringLiteral(
                             "{ node.description = \"%1\" "
                             "capture.props = { "
                             "node.name = \"%2\" "
                             "node.description = \"%1 (Cable Output)\" "
                             "media.class = Audio/Sink "
                             "audio.position = %4 "
                             "device.icon-name = \"audio-card\" "
                             "state.default-volume = 1.0 "
                             "state.restore-props = false "
                             "} "
                             "playback.props = { "
                             "node.name = \"%3\" "
                             "node.description = \"%1 (Cable Input)\" "
                             "media.class = Audio/Source "
                             "audio.position = %4 "
                             "device.icon-name = \"audio-input-microphone\" "
                             "state.default-volume = 1.0 "
                             "state.restore-props = false "
                             "} }")
                             .arg(label, c->sinkNode, c->sourceNode, QLatin1String(pos));

    const QByteArray argsUtf8 = args.toUtf8();

    pw_thread_loop_lock(d->loop);

    c->module = pw_context_load_module(d->context, "libpipewire-module-loopback",
                                       argsUtf8.constData(), nullptr);
    if (!c->module)
    {
        pw_thread_loop_unlock(d->loop);
        delete c;
        if (error)
            *error = QStringLiteral("PipeWire refused to load libpipewire-module-loopback. "
                                    "Check that the pipewire-audio modules are installed.");
        return nullptr;
    }

    auto *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Music",
        PW_KEY_STREAM_CAPTURE_SINK, "true",
        PW_KEY_STREAM_MONITOR, "true",
        PW_KEY_NODE_NAME, c->scopeNode.toUtf8().constData(),
        PW_KEY_NODE_DESCRIPTION, "PipeRack meter",
        "state.restore-props", "false",
        "state.default-volume", "1.0",
        nullptr);
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, c->sinkNode.toUtf8().constData());

    c->stream = pw_stream_new(d->core, "piperack-scope", props);
    if (c->stream)
    {
        pw_stream_add_listener(c->stream, &c->streamHook, &kStreamEvents, c);
        c->streamHooked = true;

        uint8_t buffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        const spa_pod *params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

        pw_stream_connect(c->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                          pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
                          params, 1);
    }

    d->cables.append(c);
    pw_thread_loop_unlock(d->loop);

    return c;
}

void PipeWireBackend::destroyCable(CableHandle *handle)
{
    auto *c = static_cast<PwCable *>(handle);
    if (!c || !d->loop)
        return;

    pw_thread_loop_lock(d->loop);
    d->cables.removeAll(c);

    if (c->stream)
    {
        pw_stream_destroy(c->stream);
        c->stream = nullptr;
        c->streamHooked = false;
    }
    if (c->sinkProxy)
    {
        pw_proxy_destroy(c->sinkProxy);
        c->sinkProxy = nullptr;
    }
    if (c->injectModule)
    {
        pw_impl_module_destroy(c->injectModule);
        c->injectModule = nullptr;
    }
    if (c->module)
    {
        pw_impl_module_destroy(c->module);
        c->module = nullptr;
    }
    pw_thread_loop_unlock(d->loop);

    delete c;
}

void PipeWireBackend::applyGain(CableHandle *handle, float linear, bool muted)
{
    auto *c = static_cast<PwCable *>(handle);
    if (!c || !d->loop)
        return;

    pw_thread_loop_lock(d->loop);
    c->gain = std::clamp(linear, 0.0f, 4.0f);
    c->muted = muted;
    pushVolume(c);
    pw_thread_loop_unlock(d->loop);
}

QList<AudioSource> PipeWireBackend::availableSources() const
{
    QList<AudioSource> out;
    if (!d->loop)
        return out;

    pw_thread_loop_lock(d->loop);
    out.reserve(d->sources.size());
    for (const AudioSource &s : d->sources)
        out.append(s);
    pw_thread_loop_unlock(d->loop);

    std::sort(out.begin(), out.end(), [](const AudioSource &a, const AudioSource &b)
              {
        if (a.monitor != b.monitor)
            return !a.monitor;
        const int byDesc = a.description.compare(b.description, Qt::CaseInsensitive);
        return byDesc != 0 ? byDesc < 0 : a.node < b.node; });
    return out;
}

bool PipeWireBackend::setInjectedSource(CableHandle *handle, const AudioSource &src,
                                        QString *error)
{
    auto *c = static_cast<PwCable *>(handle);
    if (!c || !d->loop || !d->core)
    {
        if (error)
            *error = QStringLiteral("The PipeWire backend is not running.");
        return false;
    }

    pw_thread_loop_lock(d->loop);

    if (c->injectModule)
    {
        pw_impl_module_destroy(c->injectModule);
        c->injectModule = nullptr;
        c->injectId = SPA_ID_INVALID;
    }
    c->injectSrc = AudioSource();

    if (src.isNull())
    {
        recount(d);
        pw_thread_loop_unlock(d->loop);
        return true;
    }

    const QString args = QStringLiteral(
                             "{ node.description = \"PipeRack injection\" "
                             "capture.props = { "
                             "node.name = \"%1.in\" "
                             "node.description = \"%2 (injected from %3)\" "
                             "media.class = Stream/Input/Audio "
                             "target.object = \"%4\" "
                             "stream.capture.sink = %5 "
                             "node.passive = false "
                             "state.default-volume = 1.0 "
                             "state.restore-props = false "
                             "} "
                             "playback.props = { "
                             "node.name = \"%1\" "
                             "node.description = \"%2 (injection)\" "
                             "media.class = Stream/Output/Audio "
                             "target.object = \"%6\" "
                             "node.passive = false "
                             "state.default-volume = 1.0 "
                             "state.restore-props = false "
                             "} }")
                             .arg(c->injectNode, spaQuote(c->label),
                                  spaQuote(src.description), spaQuote(src.node),
                                  src.monitor ? QStringLiteral("true") : QStringLiteral("false"),
                                  c->sinkNode);

    const QByteArray argsUtf8 = args.toUtf8();
    c->injectModule = pw_context_load_module(d->context, "libpipewire-module-loopback",
                                             argsUtf8.constData(), nullptr);
    if (!c->injectModule)
    {
        pw_thread_loop_unlock(d->loop);
        if (error)
            *error = QStringLiteral("PipeWire refused to load a second "
                                    "libpipewire-module-loopback for the injection.");
        return false;
    }

    c->injectSrc = src;
    recount(d);
    pw_thread_loop_unlock(d->loop);
    return true;
}
