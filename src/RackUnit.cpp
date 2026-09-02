#include "RackUnit.h"
#include "RackWidgets.h"
#include "Theme.h"
#include "WaveformView.h"

#include <QAction>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>

static constexpr int kUnitHeight = 156;
static constexpr int kSidePad = 16;

UsageStrip::UsageStrip(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(28);
    refreshTooltip();
}

void UsageStrip::setCounts(int playing, int recording)
{
    if (m_playing == playing && m_recording == recording)
        return;
    m_playing = playing;
    m_recording = recording;
    refreshTooltip();
    update();
}

void UsageStrip::setDeviceNames(const QString &sink, const QString &source)
{
    m_sink = sink;
    m_source = source;
    refreshTooltip();
}

void UsageStrip::refreshTooltip()
{
    const QString devices = m_sink.isEmpty() && m_source.isEmpty()
        ? QStringLiteral("The cable is not created yet.")
        : QStringLiteral("Output device: <code>%1</code><br>Input device: <code>%2</code>")
              .arg(m_sink.toHtmlEscaped(), m_source.toHtmlEscaped());

    setToolTip(QStringLiteral("Applications attached to this cable right now:"
                              "<br>%1 playing into it, %2 recording from it.<br><br>%3")
                   .arg(m_playing)
                   .arg(m_recording)
                   .arg(devices));
}

void UsageStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    RackPaint::field(p, r, 6, Theme::field());

    const int total = m_playing + m_recording;
    RackPaint::dot(p, QPointF(r.left() + 13, r.center().y()), 3.2,
                   total > 0 ? Theme::signal() : Theme::textFaint(), total > 0);

    QString text;
    if (total == 0)
        text = QStringLiteral("no apps connected");
    else
        text = QStringLiteral("%1 playing  ·  %2 recording").arg(m_playing).arg(m_recording);

    p.setFont(Theme::labelFont(11));
    p.setPen(total > 0 ? Theme::textDim() : Theme::textFaint());
    p.drawText(r.adjusted(26, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

SourceStrip::SourceStrip(QWidget *parent) : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFixedHeight(28);
    refreshTooltip();
}

bool SourceStrip::present() const
{
    if (m_chosen.isNull())
        return false;
    for (const AudioSource &s : m_sources)
        if (s.node == m_chosen.node)
            return true;
    return false;
}

void SourceStrip::setSources(const QList<AudioSource> &sources)
{
    m_sources = sources;

    for (const AudioSource &s : m_sources)
        if (s.node == m_chosen.node)
            m_chosen = s;

    refreshTooltip();
    update();
}

void SourceStrip::setChosen(const AudioSource &src)
{
    m_chosen = src;
    refreshTooltip();
    update();
}

void SourceStrip::refreshTooltip()
{
    QString what;
    if (m_chosen.isNull())
        what = QStringLiteral("Nothing is being injected. This cable carries only what "
                              "applications send to its output.");
    else if (present())
        what = QStringLiteral("Mixing <b>%1</b> into this cable.<br>Device: <code>%2</code>")
                   .arg(m_chosen.description.toHtmlEscaped(), m_chosen.node.toHtmlEscaped());
    else
        what = QStringLiteral("<b>%1</b> is selected but is not available right now.")
                   .arg(m_chosen.description.toHtmlEscaped());

    setToolTip(QStringLiteral("%1<br><br>Injects a microphone or other input into this "
                              "cable, alongside the applications playing into it."
                              "<br><br><i>Click to choose a source.</i>")
                   .arg(what));
}

void SourceStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    RackPaint::field(p, r, 6, m_hover ? Theme::surfaceHi() : Theme::field());

    const bool live = present();
    const bool stale = !m_chosen.isNull() && !live;
    RackPaint::dot(p, QPointF(r.left() + 13, r.center().y()), 3.2,
                   stale ? Theme::clip() : (live ? Theme::signal() : Theme::textFaint()),
                   live || stale);

    const QRectF nameRect = r.adjusted(26, 0, -22, 0);
    p.setFont(Theme::labelFont(11));
    const QFontMetrics fm(Theme::labelFont(11));
    const QString label = m_chosen.isNull() ? QStringLiteral("no input injected")
                                            : m_chosen.description;
    p.setPen(m_chosen.isNull() ? Theme::textFaint()
                               : (stale ? Theme::clip()
                                        : (m_hover ? Theme::textBright() : Theme::textDim())));
    p.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(label, Qt::ElideRight, int(nameRect.width())));

    p.setFont(Theme::labelFont(9));
    p.setPen(m_hover ? Theme::textDim() : Theme::textFaint());
    p.drawText(QRectF(r.right() - 20, r.top(), 14, r.height()),
               Qt::AlignVCenter | Qt::AlignHCenter, QStringLiteral("▾"));
}

void SourceStrip::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        openMenu();
}

void SourceStrip::openMenu()
{
    QMenu menu(this);
    menu.setFont(Theme::labelFont(12));
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: #1e222a; border: 1px solid #272c35; border-radius: 8px; padding: 5px; }"
        "QMenu::item { padding: 6px 26px 6px 22px; color: #e8eaef; border-radius: 5px; }"
        "QMenu::item:selected { background: #2c323c; }"
        "QMenu::item:disabled { color: #636b77; }"
        "QMenu::separator { height: 1px; background: #272c35; margin: 5px 8px; }"));

    auto addSource = [&](const AudioSource &src, const QString &text)
    {
        QAction *a = menu.addAction(text);
        a->setCheckable(true);
        a->setChecked(src.node == m_chosen.node);
        a->setData(QVariant::fromValue(src));
    };

    addSource(AudioSource(), QStringLiteral("No injection"));

    bool headed = false;
    for (const AudioSource &s : std::as_const(m_sources))
    {
        if (s.monitor)
            continue;
        if (!headed)
        {
            menu.addSection(QStringLiteral("Inputs"));
            headed = true;
        }
        addSource(s, s.description);
    }

    headed = false;
    for (const AudioSource &s : std::as_const(m_sources))
    {
        if (!s.monitor)
            continue;
        if (!headed)
        {
            menu.addSection(QStringLiteral("Outputs (what they are playing)"));
            headed = true;
        }
        addSource(s, s.description);
    }

    if (!m_chosen.isNull() && !present())
    {
        menu.addSeparator();
        addSource(m_chosen, QStringLiteral("%1  (unavailable)").arg(m_chosen.description));
    }

    if (QAction *picked = menu.exec(mapToGlobal(QPoint(0, height() + 2))))
    {
        const AudioSource src = picked->data().value<AudioSource>();
        if (src.node != m_chosen.node)
            emit chosen(src);
    }
}

void SourceStrip::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}
void SourceStrip::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

RackUnit::RackUnit(const CableConfig &cfg, QWidget *parent) : QWidget(parent), m_cfg(cfg)
{
    setFixedHeight(kUnitHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(kSidePad, 14, kSidePad, 14);
    root->setSpacing(14);

    auto *left = new QVBoxLayout;
    left->setSpacing(6);

    m_name = new QLineEdit(m_cfg.name, this);
    m_name->setFrame(false);
    m_name->setMaxLength(48);
    m_name->setToolTip(QStringLiteral("Name of this cable\nPress Enter to apply"));
    m_name->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; color: #e8eaef;"
        " font-size: 15px; font-weight: 600; padding: 0 0 2px 0; }"
        "QLineEdit:hover { color: #ffffff; }"
        "QLineEdit:focus { border-bottom: 1px solid #5b9dff; }"));
    connect(m_name, &QLineEdit::editingFinished, this, [this]
            {
        const QString wanted = m_name->text().trimmed();
        if (wanted.isEmpty() || wanted == m_cfg.name) {
            m_name->setText(m_cfg.name);
            return;
        }
        emit renameRequested(this, wanted); });

    m_usage = new UsageStrip(this);

    m_inject = new SourceStrip(this);
    m_inject->setChosen(m_cfg.inject);
    connect(m_inject, &SourceStrip::chosen, this, [this](const AudioSource &src)
            { emit injectRequested(this, src); });

    m_status = new QLabel(this);
    m_status->setFont(Theme::labelFont(10));
    m_status->setStyleSheet(QStringLiteral("color: #636b77;"));

    left->addWidget(m_name);
    left->addWidget(m_usage);
    left->addWidget(m_inject);
    left->addWidget(m_status);
    left->addStretch(1);

    auto *leftHost = new QWidget(this);
    leftHost->setLayout(left);
    leftHost->setFixedWidth(250);
    root->addWidget(leftHost);

    m_scope = new WaveformView(this);
    root->addWidget(m_scope, 1);

    m_meter = new LedMeter(this);
    m_meter->setChannelCount(m_cfg.channels);
    m_meter->setFixedWidth(m_cfg.channels <= 2 ? 22 : 11 + 7 * m_cfg.channels);
    root->addWidget(m_meter);

    auto *gainCol = new QVBoxLayout;
    gainCol->setSpacing(3);
    gainCol->setAlignment(Qt::AlignHCenter);

    m_knob = new Knob(this);
    m_knob->setFixedSize(48, 48);
    m_knob->setPosition(linearToKnob(m_cfg.gain));
    connect(m_knob, &Knob::positionChanged, this, [this](float)
            {
        m_cfg.gain = knobToLinear(m_knob->position());
        updateGainReadout();
        pushGain(); });

    m_gainText = new QLabel(this);
    m_gainText->setAlignment(Qt::AlignCenter);
    m_gainText->setFont(Theme::monoFont(10, true));

    auto *gainCaption = new QLabel(QStringLiteral("Gain"), this);
    gainCaption->setAlignment(Qt::AlignCenter);
    gainCaption->setFont(Theme::capsFont(8));
    gainCaption->setStyleSheet(QStringLiteral("color: #636b77;"));

    gainCol->addWidget(m_knob, 0, Qt::AlignHCenter);
    gainCol->addWidget(m_gainText);
    gainCol->addWidget(gainCaption);
    gainCol->addStretch(1);
    root->addLayout(gainCol);

    auto *btnCol = new QVBoxLayout;
    btnCol->setSpacing(7);

    m_mute = new RackButton(QStringLiteral("Mute"), this);
    m_mute->setCheckable(true);
    m_mute->setActiveColour(Theme::warn());
    m_mute->setChecked(m_cfg.muted);
    m_mute->setFixedSize(84, 30);
    connect(m_mute, &RackButton::toggled, this, [this](bool on)
            {
        m_cfg.muted = on;
        m_scope->setMuted(on);
        if (on)
            m_meter->reset();
        pushGain(); });

    m_remove = new RackButton(QStringLiteral("Remove"), this);
    m_remove->setDanger(true);
    m_remove->setFixedSize(84, 30);
    connect(m_remove, &RackButton::clicked, this, [this]
            { emit removeRequested(this); });

    btnCol->addWidget(m_mute);
    btnCol->addWidget(m_remove);
    btnCol->addStretch(1);
    root->addLayout(btnCol);

    m_scope->setMuted(m_cfg.muted);
    updateGainReadout();
    refreshFromBackend();
}

void RackUnit::setName(const QString &name)
{
    m_cfg.name = name;
    if (m_name->text() != name)
        m_name->setText(name);
}

void RackUnit::setAvailableSources(const QList<AudioSource> &sources)
{
    m_inject->setSources(sources);
}

void RackUnit::setInjectedSource(const AudioSource &src)
{
    m_cfg.inject = src;
    m_inject->setChosen(src);
}

void RackUnit::setSlotNumber(int n)
{
    m_slot = n;
    update();
}

void RackUnit::setHandle(CableHandle *handle)
{
    m_handle = handle;
    m_scope->setSource(handle);
    m_meter->reset();
    if (handle)
        m_usage->setDeviceNames(handle->sinkName(), handle->sourceName());
    else
        m_usage->setDeviceNames(QString(), QString());
    refreshFromBackend();
}

void RackUnit::refreshFromBackend()
{
    if (!m_handle)
    {
        m_status->setText(QStringLiteral("offline"));
        m_usage->setCounts(0, 0);
        return;
    }

    m_usage->setCounts(m_handle->playbackClients(), m_handle->captureClients());

    const int rate = m_handle->sampleRate();
    const int ch = m_handle->channels();
    const QString state = m_handle->live() ? QStringLiteral("live") : QStringLiteral("starting…");
    m_status->setText(rate > 0
                          ? QStringLiteral("%1 · %2 Hz · %3 ch").arg(state).arg(rate).arg(ch)
                          : state);
}

void RackUnit::tick(qreal dt)
{
    m_scope->advance(dt);

    if (!m_handle)
        return;

    float peaks[kMaxChannels];
    const int ch = std::clamp(m_handle->channels(), 1, kMaxChannels);
    for (int i = 0; i < ch; ++i)
        peaks[i] = m_handle->takePeak(i);
    m_meter->pushPeaks(peaks, ch, dt);
}

void RackUnit::updateGainReadout()
{
    const float db = linearToDb(m_cfg.gain);
    if (m_cfg.gain <= 0.0001f)
        m_gainText->setText(QStringLiteral("-∞"));
    else
        m_gainText->setText(QStringLiteral("%1%2 dB")
                                .arg(db >= 0.05f ? QStringLiteral("+") : QString())
                                .arg(db, 0, 'f', 1));
    m_gainText->setStyleSheet(std::fabs(db) < 0.05f
                                  ? QStringLiteral("color: #3dcf8e;")
                                  : QStringLiteral("color: #98a0ac;"));
}

void RackUnit::pushGain()
{
    emit gainChanged(this, m_cfg.gain, m_cfg.muted);
}

void RackUnit::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    RackPaint::card(p, r, 10, Theme::surface(), Theme::border());
}
