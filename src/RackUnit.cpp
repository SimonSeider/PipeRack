#include "RackUnit.h"
#include "RackWidgets.h"
#include "Theme.h"
#include "WaveformView.h"

#include <QApplication>
#include <QClipboard>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>

static constexpr int kRailWidth = 30;
static constexpr int kUnitHeight = 190;

PortStrip::PortStrip(Role role, QWidget *parent) : QWidget(parent), m_role(role)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFixedHeight(26);
    refreshTooltip();
}

void PortStrip::setNodeName(const QString &name)
{
    m_node = name;
    refreshTooltip();
    update();
}

void PortStrip::setClientCount(int n)
{
    if (m_clients == n)
        return;
    m_clients = n;
    refreshTooltip();
    update();
}

void PortStrip::refreshTooltip()
{
    const QString what = m_role == CableOutput
                             ? QStringLiteral("Applications select this as their <b>output device</b> to send audio into the cable.")
                             : QStringLiteral("Applications select this as their <b>input device</b> to record audio out of the cable.");
    setToolTip(QStringLiteral("%1<br><br>Device: <code>%2</code><br>Attached now: %3"
                              "<br><br><i>Click to copy the device name.</i>")
                   .arg(what, m_node.isEmpty() ? QStringLiteral("(not created)") : m_node)
                   .arg(m_clients));
}

void PortStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    RackPaint::inset(p, r, 4, QColor(0x1c, 0x1f, 0x24, m_hover ? 255 : 210));

    const bool active = m_clients > 0;
    RackPaint::ledDot(p, QPointF(r.left() + 13, r.center().y()), 3.4,
                      active ? Theme::signal() : Theme::textFaint(), active);

    const QString role = m_role == CableOutput ? QStringLiteral("OUT") : QStringLiteral("IN");
    const QString arrow = m_role == CableOutput ? QStringLiteral("▸") : QStringLiteral("◂");

    p.setFont(Theme::engravedFont(9));
    p.setPen(m_role == CableOutput ? Theme::accent() : Theme::warn());
    const QRectF roleRect(r.left() + 24, r.top(), 40, r.height());
    p.drawText(roleRect, Qt::AlignVCenter | Qt::AlignLeft, arrow + QLatin1Char(' ') + role);

    p.setFont(Theme::monoFont(10));
    const QRectF nameRect = r.adjusted(66, 0, m_clients > 0 ? -30 : -8, 0);
    QFontMetrics fm(Theme::monoFont(10));
    const QString shown = fm.elidedText(m_flash ? QStringLiteral("copied to clipboard")
                                                : m_node,
                                        m_flash ? Qt::ElideNone : Qt::ElideLeft,
                                        int(nameRect.width()));
    p.setPen(m_flash ? Theme::signal() : (m_hover ? Theme::textBright() : Theme::textDim()));
    p.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, shown);

    if (m_clients > 0)
    {
        const QRectF badge(r.right() - 27, r.center().y() - 8, 20, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::signalDim());
        p.drawRoundedRect(badge, 8, 8);
        p.setFont(Theme::monoFont(9, true));
        p.setPen(Theme::textBright());
        p.drawText(badge, Qt::AlignCenter, QString::number(m_clients));
    }
}

void PortStrip::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_node.isEmpty())
        return;
    QApplication::clipboard()->setText(m_node);
    m_flash = true;
    update();
    QTimer::singleShot(900, this, [this]
                       { m_flash = false; update(); });
}

void PortStrip::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}
void PortStrip::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

SourceStrip::SourceStrip(QWidget *parent) : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFixedHeight(26);
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
    RackPaint::inset(p, r, 4, QColor(0x1c, 0x1f, 0x24, m_hover ? 255 : 210));

    const bool live = present();
    const bool stale = !m_chosen.isNull() && !live;
    RackPaint::ledDot(p, QPointF(r.left() + 13, r.center().y()), 3.4,
                      stale ? Theme::clip() : (live ? Theme::signal() : Theme::textFaint()),
                      live || stale);

    p.setFont(Theme::engravedFont(9));
    p.setPen(live ? Theme::signal() : Theme::textFaint());
    p.drawText(QRectF(r.left() + 24, r.top(), 40, r.height()),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("◂ SRC"));

    const QRectF nameRect = r.adjusted(66, 0, -20, 0);
    p.setFont(Theme::monoFont(10));
    QFontMetrics fm(Theme::monoFont(10));
    const QString label = m_chosen.isNull() ? QStringLiteral("no injection")
                                            : m_chosen.description;
    p.setPen(m_chosen.isNull() ? Theme::textFaint()
                               : (stale ? Theme::clip()
                                        : (m_hover ? Theme::textBright() : Theme::textDim())));
    p.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(label, Qt::ElideRight, int(nameRect.width())));

    p.setFont(Theme::labelFont(9));
    p.setPen(m_hover ? Theme::textDim() : Theme::textFaint());
    p.drawText(QRectF(r.right() - 18, r.top(), 14, r.height()),
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
        "QMenu { background: #22252a; border: 1px solid #3d434b; padding: 4px; }"
        "QMenu::item { padding: 5px 26px 5px 22px; color: #e6e9ee; }"
        "QMenu::item:selected { background: #33383f; }"
        "QMenu::item:disabled { color: #6b727d; }"
        "QMenu::separator { height: 1px; background: #3d434b; margin: 4px 8px; }"));

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
    root->setContentsMargins(kRailWidth + 12, 12, kRailWidth + 12, 12);
    root->setSpacing(14);

    auto *left = new QVBoxLayout;
    left->setSpacing(5);

    m_name = new QLineEdit(m_cfg.name, this);
    m_name->setFrame(false);
    m_name->setMaxLength(48);
    m_name->setToolTip(QStringLiteral("Name of this cable\nPress Enter to apply"));
    m_name->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; border-bottom: 1px solid #3d434b;"
        " color: #e6e9ee; font-size: 15px; font-weight: 600; padding: 1px 0 3px 0; }"
        "QLineEdit:hover { border-bottom: 1px solid #5a626c; }"
        "QLineEdit:focus { border-bottom: 1px solid #62b8f0; }"));
    connect(m_name, &QLineEdit::editingFinished, this, [this]
            {
        const QString wanted = m_name->text().trimmed();
        if (wanted.isEmpty() || wanted == m_cfg.name) {
            m_name->setText(m_cfg.name);
            return;
        }
        emit renameRequested(this, wanted); });

    m_out = new PortStrip(PortStrip::CableOutput, this);
    m_in = new PortStrip(PortStrip::CableInput, this);

    m_inject = new SourceStrip(this);
    m_inject->setChosen(m_cfg.inject);
    connect(m_inject, &SourceStrip::chosen, this, [this](const AudioSource &src)
            { emit injectRequested(this, src); });

    m_status = new QLabel(this);
    m_status->setFont(Theme::monoFont(9));
    m_status->setStyleSheet(QStringLiteral("color: #6b727d;"));

    left->addWidget(m_name);
    left->addSpacing(2);
    left->addWidget(m_out);
    left->addWidget(m_in);
    left->addWidget(m_inject);
    left->addWidget(m_status);
    left->addStretch(1);

    auto *leftHost = new QWidget(this);
    leftHost->setLayout(left);
    leftHost->setFixedWidth(262);
    root->addWidget(leftHost);

    m_scope = new WaveformView(this);
    root->addWidget(m_scope, 1);

    m_meter = new LedMeter(this);
    m_meter->setChannelCount(m_cfg.channels);
    m_meter->setFixedWidth(m_cfg.channels <= 2 ? 24 : 10 + 7 * m_cfg.channels);
    root->addWidget(m_meter);

    auto *gainCol = new QVBoxLayout;
    gainCol->setSpacing(2);
    gainCol->setAlignment(Qt::AlignHCenter);

    m_knob = new Knob(this);
    m_knob->setFixedSize(52, 52);
    m_knob->setPosition(linearToKnob(m_cfg.gain));
    connect(m_knob, &Knob::positionChanged, this, [this](float)
            {
        m_cfg.gain = knobToLinear(m_knob->position());
        updateGainReadout();
        pushGain(); });

    m_gainText = new QLabel(this);
    m_gainText->setAlignment(Qt::AlignCenter);
    m_gainText->setFont(Theme::monoFont(10, true));
    m_gainText->setStyleSheet(QStringLiteral("color: #969da8;"));

    auto *gainCaption = new QLabel(QStringLiteral("GAIN"), this);
    gainCaption->setAlignment(Qt::AlignCenter);
    gainCaption->setFont(Theme::engravedFont(8));
    gainCaption->setStyleSheet(QStringLiteral("color: #6b727d;"));

    gainCol->addWidget(m_knob, 0, Qt::AlignHCenter);
    gainCol->addWidget(m_gainText);
    gainCol->addWidget(gainCaption);
    gainCol->addStretch(1);
    root->addLayout(gainCol);

    auto *btnCol = new QVBoxLayout;
    btnCol->setSpacing(6);

    m_mute = new RackButton(QStringLiteral("Mute"), this);
    m_mute->setCheckable(true);
    m_mute->setLedColour(Theme::warn());
    m_mute->setChecked(m_cfg.muted);
    m_mute->setFixedWidth(84);
    connect(m_mute, &RackButton::toggled, this, [this](bool on)
            {
        m_cfg.muted = on;
        m_scope->setMuted(on);
        if (on)
            m_meter->reset();
        pushGain(); });

    m_remove = new RackButton(QStringLiteral("Remove"), this);
    m_remove->setDanger(true);
    m_remove->setFixedWidth(84);
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
    {
        m_out->setNodeName(handle->sinkName());
        m_in->setNodeName(handle->sourceName());
    }
    else
    {
        m_out->setNodeName(QString());
        m_in->setNodeName(QString());
    }
    refreshFromBackend();
}

void RackUnit::refreshFromBackend()
{
    if (!m_handle)
    {
        m_status->setText(QStringLiteral("offline"));
        m_out->setClientCount(0);
        m_in->setClientCount(0);
        return;
    }

    m_out->setClientCount(m_handle->playbackClients());
    m_in->setClientCount(m_handle->captureClients());

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
                                  ? QStringLiteral("color: #46e092;")
                                  : QStringLiteral("color: #969da8;"));
}

void RackUnit::pushGain()
{
    emit gainChanged(this, m_cfg.gain, m_cfg.muted);
}

void RackUnit::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0, 2, 0, -2);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawRoundedRect(r.adjusted(2, 4, -2, 2), 6, 6);

    RackPaint::brushedPlate(p, r, 5);

    const QRectF leftRail(r.left(), r.top(), kRailWidth, r.height());
    const QRectF rightRail(r.right() - kRailWidth, r.top(), kRailWidth, r.height());
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(r, 5, 5);
    p.setClipPath(clip);
    RackPaint::rail(p, leftRail, true);
    RackPaint::rail(p, rightRail, false);
    p.restore();

    p.setPen(QPen(QColor(0, 0, 0, 90), 1));
    p.drawLine(QPointF(leftRail.right(), r.top() + 1), QPointF(leftRail.right(), r.bottom() - 1));
    p.drawLine(QPointF(rightRail.left(), r.top() + 1), QPointF(rightRail.left(), r.bottom() - 1));

    const qreal sx1 = leftRail.center().x();
    const qreal sx2 = rightRail.center().x();
    RackPaint::screw(p, QPointF(sx1, r.top() + 17), 6, 18 + m_slot * 23);
    RackPaint::screw(p, QPointF(sx1, r.bottom() - 17), 6, -35 + m_slot * 11);
    RackPaint::screw(p, QPointF(sx2, r.top() + 17), 6, 62 - m_slot * 17);
    RackPaint::screw(p, QPointF(sx2, r.bottom() - 17), 6, 5 + m_slot * 29);

    p.save();
    p.translate(leftRail.center().x(), r.center().y());
    p.rotate(-90);
    p.setFont(Theme::engravedFont(9));
    p.setPen(QColor(0, 0, 0, 120));
    p.drawText(QRectF(-40, -8 + 1, 80, 16), Qt::AlignCenter, QStringLiteral("CH %1").arg(m_slot, 2, 10, QLatin1Char('0')));
    p.setPen(Theme::textFaint());
    p.drawText(QRectF(-40, -8, 80, 16), Qt::AlignCenter, QStringLiteral("CH %1").arg(m_slot, 2, 10, QLatin1Char('0')));
    p.restore();
}
