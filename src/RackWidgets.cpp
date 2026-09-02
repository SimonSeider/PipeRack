#include "RackWidgets.h"
#include "AudioBackend.h"
#include "Theme.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

namespace RackPaint
{

    void card(QPainter &p, const QRectF &r, qreal radius, const QColor &fill,
              const QColor &stroke)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, radius, radius);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(stroke, 1));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void field(QPainter &p, const QRectF &r, qreal radius, const QColor &fill)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, radius, radius);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::border(), 1));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void dot(QPainter &p, const QPointF &c, qreal radius, const QColor &colour, bool lit)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);

        if (lit)
        {
            QColor halo = colour;
            halo.setAlpha(56);
            p.setBrush(halo);
            p.drawEllipse(c, radius * 2.1, radius * 2.1);
        }

        QColor body = colour;
        if (!lit)
        {
            body.setAlpha(70);
            body = QColor::fromRgb(qRound(Theme::field().red() * 0.4 + body.red() * 0.35),
                                   qRound(Theme::field().green() * 0.4 + body.green() * 0.35),
                                   qRound(Theme::field().blue() * 0.4 + body.blue() * 0.35));
        }
        p.setBrush(body);
        p.drawEllipse(c, radius, radius);
        p.restore();
    }

}

Knob::Knob(QWidget *parent) : QWidget(parent)
{
    setCursor(Qt::SizeVerCursor);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("Cable gain\nDrag to adjust, double-click for unity"));
}

void Knob::setPosition(float pos)
{
    pos = std::clamp(pos, 0.0f, 1.0f);
    if (qFuzzyCompare(pos + 1.0f, m_pos + 1.0f))
        return;
    m_pos = pos;
    update();
    emit positionChanged(m_pos);
}

void Knob::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal side = qMin(width(), height());
    const QPointF c(width() / 2.0, height() / 2.0);
    const qreal trackR = side / 2.0 - 3.0;
    const QRectF trackRect(c.x() - trackR, c.y() - trackR, trackR * 2, trackR * 2);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::border(), 3.0, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(trackRect, 225 * 16, -270 * 16);

    const bool hot = m_pos > kUnityKnobPos + 1e-4f;
    p.setPen(QPen(hot ? Theme::warn() : Theme::accent(), 3.0, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(trackRect, 225 * 16, int(-270 * 16 * m_pos));

    const qreal unityAngle = qDegreesToRadians(225.0 - 270.0 * double(kUnityKnobPos));
    p.setPen(QPen(Theme::textFaint(), 1.2));
    p.drawLine(QPointF(c.x() + std::cos(unityAngle) * (trackR + 2.0),
                       c.y() - std::sin(unityAngle) * (trackR + 2.0)),
               QPointF(c.x() + std::cos(unityAngle) * (trackR + 4.5),
                       c.y() - std::sin(unityAngle) * (trackR + 4.5)));

    const qreal a = qDegreesToRadians(225.0 - 270.0 * double(m_pos));
    p.setPen(QPen(hasFocus() ? Theme::textBright() : Theme::textDim(), 2.0,
                  Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(c.x() + std::cos(a) * trackR * 0.30,
                       c.y() - std::sin(a) * trackR * 0.30),
               QPointF(c.x() + std::cos(a) * trackR * 0.66,
                       c.y() - std::sin(a) * trackR * 0.66));
}

void Knob::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_pressY = int(e->position().y());
    m_pressPos = m_pos;
    setFocus(Qt::MouseFocusReason);
}

void Knob::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    const qreal span = (e->modifiers() & Qt::ShiftModifier) ? 600.0 : 160.0;
    setPosition(m_pressPos + float((m_pressY - e->position().y()) / span));
}

void Knob::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}

void Knob::mouseDoubleClickEvent(QMouseEvent *)
{
    setPosition(kUnityKnobPos);
}

void Knob::wheelEvent(QWheelEvent *e)
{
    const float step = (e->modifiers() & Qt::ShiftModifier) ? 0.005f : 0.02f;
    setPosition(m_pos + (e->angleDelta().y() > 0 ? step : -step));
    e->accept();
}

LedMeter::LedMeter(QWidget *parent) : QWidget(parent)
{
    setToolTip(QStringLiteral("Signal level\nClick to clear the clip indicator"));
}

void LedMeter::setChannelCount(int n)
{
    m_channels = std::clamp(n, 1, 8);
    update();
}

void LedMeter::reset()
{
    for (int i = 0; i < 8; ++i)
    {
        m_level[i] = 0;
        m_hold[i] = 0;
        m_holdAge[i] = 0;
    }
    m_clipLatch = false;
    update();
}

void LedMeter::pushPeaks(const float *peaks, int count, qreal dt)
{
    const float release = float(std::pow(10.0, -dt));
    for (int i = 0; i < m_channels && i < count; ++i)
    {
        const float v = peaks[i];
        if (v >= 0.999f)
            m_clipLatch = true;
        m_level[i] = std::max(v, m_level[i] * release);

        if (v >= m_hold[i])
        {
            m_hold[i] = v;
            m_holdAge[i] = 0;
        }
        else
        {
            m_holdAge[i] += float(dt);
            if (m_holdAge[i] > 1.2f)
                m_hold[i] = std::max(m_level[i], m_hold[i] * float(std::pow(10.0, -1.5 * dt)));
        }
    }
    update();
}

static float ampToNorm(float amp)
{
    if (amp <= 0.0f)
        return 0.0f;
    const float db = 20.0f * std::log10(amp);
    return std::clamp((db + 48.0f) / 54.0f, 0.0f, 1.0f);
}

static QColor levelColour(float norm)
{
    if (norm > 0.888f)
        return Theme::clip();
    if (norm > 0.777f)
        return Theme::warn();
    return Theme::signal();
}

void LedMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    RackPaint::field(p, r, 4, Theme::field());

    const qreal pad = 4.0;
    const qreal gap = 3.0;
    const qreal barW = (r.width() - pad * 2 - gap * (m_channels - 1)) / m_channels;
    const qreal top = r.top() + pad;
    const qreal bot = r.bottom() - pad;
    const qreal h = bot - top;
    const qreal radius = qMin<qreal>(barW / 2.0, 3.0);

    p.setPen(Qt::NoPen);
    for (int ch = 0; ch < m_channels; ++ch)
    {
        const qreal x = r.left() + pad + ch * (barW + gap);

        const qreal level = ampToNorm(m_level[ch]);
        p.setBrush(QColor(Theme::border()));
        p.drawRoundedRect(QRectF(x, top, barW, h), radius, radius);

        if (level > 0.001)
        {
            const qreal barH = h * level;
            p.setBrush(levelColour(float(level)));
            p.drawRoundedRect(QRectF(x, bot - barH, barW, barH), radius, radius);
        }

        const qreal hold = ampToNorm(m_hold[ch]);
        if (hold > 0.001)
        {
            QColor hc = levelColour(float(hold));
            hc.setAlpha(200);
            p.setBrush(hc);
            p.drawRect(QRectF(x, bot - h * hold - 1.0, barW, 2.0));
        }
    }

    if (m_clipLatch)
    {
        p.setBrush(Theme::clip());
        p.drawRoundedRect(QRectF(r.left() + pad, r.top() + 1.5, r.width() - pad * 2, 2.5), 1, 1);
    }
}

void LedMeter::mousePressEvent(QMouseEvent *)
{
    clearClip();
}

RackButton::RackButton(const QString &text, QWidget *parent)
    : QWidget(parent), m_text(text), m_active(Theme::accent())
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void RackButton::setChecked(bool on)
{
    if (m_checked == on)
        return;
    m_checked = on;
    update();
    emit toggled(m_checked);
}

QSize RackButton::sizeHint() const
{
    QFontMetrics fm(Theme::labelFont(12, true));
    return QSize(fm.horizontalAdvance(m_text) + 28, 30);
}

void RackButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor tint = m_danger ? Theme::clip() : m_active;

    QColor fill;
    QColor stroke = Theme::border();
    QColor text = Theme::textDim();

    if (m_checkable && m_checked)
    {
        fill = tint;
        fill.setAlpha(m_down ? 64 : 44);
        stroke = tint;
        stroke.setAlpha(150);
        text = tint;
    }
    else if (m_ghost)
    {
        fill = m_hover ? QColor(tint.red(), tint.green(), tint.blue(), m_down ? 42 : 26)
                       : QColor(0, 0, 0, 0);
        stroke = m_hover ? QColor(tint.red(), tint.green(), tint.blue(), 160) : Theme::border();
        text = m_hover ? tint : Theme::textDim();
    }
    else
    {
        fill = m_down ? Theme::field() : (m_hover ? Theme::surfaceHi() : Theme::surface());
        stroke = m_hover ? Theme::borderHi() : Theme::border();
        text = m_hover ? (m_danger ? Theme::clip() : Theme::textBright()) : Theme::textDim();
    }

    if (!isEnabled())
    {
        fill = Theme::surface();
        stroke = Theme::border();
        text = Theme::textFaint();
    }

    RackPaint::card(p, r, 6, fill, stroke);

    p.setFont(Theme::labelFont(12, true));
    p.setPen(text);
    p.drawText(r, Qt::AlignCenter, m_text);
}

void RackButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !isEnabled())
        return;
    m_down = true;
    update();
}

void RackButton::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !isEnabled())
        return;
    m_down = false;
    if (rect().contains(e->position().toPoint()))
    {
        if (m_checkable)
            setChecked(!m_checked);
        emit clicked();
    }
    update();
}

void RackButton::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}
void RackButton::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}
