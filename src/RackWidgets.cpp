#include "RackWidgets.h"
#include "Theme.h"

#include <QEnterEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

namespace RackPaint
{

    void brushedPlate(QPainter &p, const QRectF &r, qreal radius)
    {
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0.0, Theme::plateTop());
        g.setColorAt(0.45, Theme::plateTop().darker(108));
        g.setColorAt(0.55, Theme::plateBottom().lighter(104));
        g.setColorAt(1.0, Theme::plateBottom());

        QPainterPath path;
        path.addRoundedRect(r, radius, radius);
        p.fillPath(path, g);

        p.save();
        p.setClipPath(path);
        for (int y = int(r.top()); y < int(r.bottom()); y += 2)
        {
            const int h = (y * 2654435761u) >> 24;
            const int alpha = 6 + (h % 10);
            p.setPen(QPen(QColor(255, 255, 255, alpha), 1));
            p.drawLine(QPointF(r.left(), y + 0.5), QPointF(r.right(), y + 0.5));
        }
        p.restore();

        p.setPen(QPen(QColor(255, 255, 255, 34), 1));
        p.drawLine(QPointF(r.left() + radius, r.top() + 0.5), QPointF(r.right() - radius, r.top() + 0.5));
        p.setPen(QPen(QColor(0, 0, 0, 110), 1));
        p.drawLine(QPointF(r.left() + radius, r.bottom() - 0.5), QPointF(r.right() - radius, r.bottom() - 0.5));

        p.setPen(QPen(Theme::plateEdge(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void rail(QPainter &p, const QRectF &r, bool leftSide)
    {
        QLinearGradient g(r.left(), 0, r.right(), 0);
        if (leftSide)
        {
            g.setColorAt(0.0, Theme::railDark());
            g.setColorAt(0.7, Theme::railLight());
            g.setColorAt(1.0, Theme::railDark().darker(120));
        }
        else
        {
            g.setColorAt(0.0, Theme::railDark().darker(120));
            g.setColorAt(0.3, Theme::railLight());
            g.setColorAt(1.0, Theme::railDark());
        }
        p.fillRect(r, g);
    }

    void screw(QPainter &p, const QPointF &c, qreal radius, qreal angleDeg)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF hole(c.x() - radius - 1.5, c.y() - radius - 1.5,
                          (radius + 1.5) * 2, (radius + 1.5) * 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawEllipse(hole);

        QRadialGradient g(c.x() - radius * 0.35, c.y() - radius * 0.35, radius * 1.8);
        g.setColorAt(0.0, QColor(0xb4, 0xba, 0xc4));
        g.setColorAt(0.5, QColor(0x76, 0x7d, 0x88));
        g.setColorAt(1.0, QColor(0x3c, 0x41, 0x48));
        p.setBrush(g);
        p.setPen(QPen(QColor(0x22, 0x25, 0x2a), 1));
        p.drawEllipse(c, radius, radius);

        p.save();
        p.translate(c);
        p.rotate(angleDeg);
        p.setPen(QPen(QColor(0x1e, 0x21, 0x25), qMax<qreal>(1.4, radius * 0.28), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(-radius * 0.62, 0), QPointF(radius * 0.62, 0));
        p.setPen(QPen(QColor(255, 255, 255, 40), 1));
        p.drawLine(QPointF(-radius * 0.62, 1.2), QPointF(radius * 0.62, 1.2));
        p.restore();

        p.restore();
    }

    void inset(QPainter &p, const QRectF &r, qreal radius, const QColor &fill)
    {
        QPainterPath path;
        path.addRoundedRect(r, radius, radius);
        p.fillPath(path, fill);

        p.setPen(QPen(QColor(0, 0, 0, 190), 1.4));
        p.drawArc(r.adjusted(0.7, 0.7, -0.7, -0.7), 30 * 16, 130 * 16);
        p.setPen(QPen(QColor(255, 255, 255, 26), 1.2));
        p.drawArc(r.adjusted(0.7, 0.7, -0.7, -0.7), 210 * 16, 130 * 16);

        p.setPen(QPen(QColor(0x14, 0x17, 0x1a), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void ledDot(QPainter &p, const QPointF &c, qreal radius, const QColor &colour, bool lit)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);

        if (lit)
        {
            QRadialGradient glow(c, radius * 3.2);
            QColor g0 = colour;
            g0.setAlpha(120);
            QColor g1 = colour;
            g1.setAlpha(0);
            glow.setColorAt(0.0, g0);
            glow.setColorAt(1.0, g1);
            p.setBrush(glow);
            p.drawEllipse(c, radius * 3.2, radius * 3.2);
        }

        QColor body = lit ? colour : colour.darker(340);
        QRadialGradient g(c.x() - radius * 0.3, c.y() - radius * 0.3, radius * 1.6);
        g.setColorAt(0.0, lit ? body.lighter(150) : body.lighter(115));
        g.setColorAt(1.0, body.darker(140));
        p.setBrush(g);
        p.drawEllipse(c, radius, radius);

        p.setPen(QPen(QColor(0, 0, 0, 130), 1));
        p.setBrush(Qt::NoBrush);
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
    const qreal outer = side / 2.0 - 2.0;
    const qreal trackR = outer - 1.5;
    const qreal bodyR = outer * 0.68;

    const QRectF trackRect(c.x() - trackR, c.y() - trackR, trackR * 2, trackR * 2);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 150), 3.4, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(trackRect, 225 * 16, -270 * 16);

    const bool hot = m_pos > 0.8f + 1e-4f;
    p.setPen(QPen(hot ? Theme::warn() : Theme::signal(), 2.6, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(trackRect, 225 * 16, int(-270 * 16 * m_pos));

    p.setPen(QPen(Theme::textFaint(), 1.4));
    const qreal unityAngle = qDegreesToRadians(225.0 - 270.0 * 0.8);
    p.drawLine(QPointF(c.x() + std::cos(unityAngle) * (trackR + 1.5),
                       c.y() - std::sin(unityAngle) * (trackR + 1.5)),
               QPointF(c.x() + std::cos(unityAngle) * (trackR + 4.0),
                       c.y() - std::sin(unityAngle) * (trackR + 4.0)));

    QRadialGradient g(c.x() - bodyR * 0.4, c.y() - bodyR * 0.5, bodyR * 2.1);
    g.setColorAt(0.0, QColor(0x6e, 0x75, 0x80));
    g.setColorAt(0.55, QColor(0x44, 0x49, 0x52));
    g.setColorAt(1.0, QColor(0x25, 0x28, 0x2d));
    p.setPen(QPen(QColor(0x18, 0x1a, 0x1e), 1));
    p.setBrush(g);
    p.drawEllipse(c, bodyR, bodyR);

    const qreal a = qDegreesToRadians(225.0 - 270.0 * double(m_pos));
    const QPointF from(c.x() + std::cos(a) * bodyR * 0.35, c.y() - std::sin(a) * bodyR * 0.35);
    const QPointF to(c.x() + std::cos(a) * bodyR * 0.86, c.y() - std::sin(a) * bodyR * 0.86);
    p.setPen(QPen(QColor(0xea, 0xee, 0xf4), 2.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(from, to);

    if (hasFocus())
    {
        p.setPen(QPen(Theme::accent(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, outer + 1, outer + 1);
    }
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
    setPosition(0.8f);
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

void LedMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0, 0, -1, -1);
    RackPaint::inset(p, r, 3, QColor(0x0a, 0x0c, 0x0e));

    const qreal pad = 3.0;
    const qreal gap = 2.0;
    const qreal barW = (r.width() - pad * 2 - gap * (m_channels - 1)) / m_channels;
    const qreal top = r.top() + pad;
    const qreal bot = r.bottom() - pad;
    const qreal h = bot - top;

    const int segments = std::max(8, int(h / 5.0));
    const qreal segH = h / segments;

    for (int ch = 0; ch < m_channels; ++ch)
    {
        const qreal x = r.left() + pad + ch * (barW + gap);
        const int litCount = int(ampToNorm(m_level[ch]) * segments + 0.5f);
        const int holdSeg = int(ampToNorm(m_hold[ch]) * segments + 0.5f);

        for (int s = 0; s < segments; ++s)
        {
            const qreal frac = qreal(s) / segments;
            const qreal y = bot - (s + 1) * segH;
            const QRectF seg(x, y + 0.6, barW, segH - 1.2);

            QColor c;
            if (frac > 0.888)
                c = Theme::clip();
            else if (frac > 0.777)
                c = Theme::warn();
            else
                c = Theme::signal();

            const bool lit = s < litCount;
            const bool isHold = (s == holdSeg - 1 && holdSeg > 0);

            if (lit || isHold)
            {
                p.setBrush(isHold && !lit ? c.darker(140) : c);
                p.setPen(Qt::NoPen);
                p.drawRect(seg);
            }
            else
            {
                p.setBrush(QColor(c.red() / 9, c.green() / 9, c.blue() / 9));
                p.setPen(Qt::NoPen);
                p.drawRect(seg);
            }
        }
    }

    if (m_clipLatch)
    {
        p.setBrush(Theme::clip());
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(r.left() + pad, r.top() + 1, r.width() - pad * 2, 2.0));
    }
}

void LedMeter::mousePressEvent(QMouseEvent *)
{
    clearClip();
}

RackButton::RackButton(const QString &text, QWidget *parent)
    : QWidget(parent), m_text(text), m_led(Theme::signal())
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
    QFontMetrics fm(Theme::engravedFont(10));
    return QSize(fm.horizontalAdvance(m_text) + 30, 26);
}

void RackButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const bool pressedLook = m_down || (m_checkable && m_checked);

    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (pressedLook)
    {
        g.setColorAt(0.0, QColor(0x22, 0x25, 0x2a));
        g.setColorAt(1.0, QColor(0x33, 0x37, 0x3e));
    }
    else
    {
        g.setColorAt(0.0, QColor(0x4b, 0x51, 0x5a));
        g.setColorAt(1.0, QColor(0x30, 0x34, 0x3a));
    }

    QPainterPath path;
    path.addRoundedRect(r, 4, 4);
    p.fillPath(path, g);

    QColor edge = m_hover ? (m_danger ? Theme::clip() : Theme::accent()) : QColor(0x1c, 0x1f, 0x23);
    if (m_hover)
        edge.setAlpha(190);
    p.setPen(QPen(edge, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 4, 4);

    qreal textLeft = r.left() + 9;
    if (m_checkable)
    {
        RackPaint::ledDot(p, QPointF(r.left() + 11, r.center().y()), 3.2, m_led, m_checked);
        textLeft = r.left() + 21;
    }

    p.setFont(Theme::engravedFont(10));
    QColor tc = m_checkable && m_checked ? Theme::textBright()
                                         : (m_danger && m_hover ? Theme::clip() : Theme::textDim());
    p.setPen(QColor(0, 0, 0, 120));
    p.drawText(QRectF(textLeft, r.top() + 1, r.width(), r.height()), Qt::AlignVCenter | Qt::AlignLeft, m_text);
    p.setPen(tc);
    p.drawText(QRectF(textLeft, r.top(), r.width(), r.height()), Qt::AlignVCenter | Qt::AlignLeft, m_text);
}

void RackButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_down = true;
    update();
}

void RackButton::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
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
