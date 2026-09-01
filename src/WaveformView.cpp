#include "WaveformView.h"
#include "RackWidgets.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QVector>

#include <algorithm>
#include <cmath>

WaveformView::WaveformView(QWidget *parent) : QWidget(parent)
{
    m_bins.assign(kCapacity, ScopeBin{0.0f, 0.0f, 0.0f});
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip(QStringLiteral("Live signal on this cable"));
}

void WaveformView::setSource(CableHandle *handle)
{
    m_handle = handle;
    clear();
}

void WaveformView::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    update();
}

void WaveformView::clear()
{
    std::fill(m_bins.begin(), m_bins.end(), ScopeBin{0.0f, 0.0f, 0.0f});
    m_head = 0;
    m_count = 0;
    m_carry = 0.0;
    m_silentFor = 10.0;
    update();
}

void WaveformView::pushBin(const ScopeBin &b)
{
    m_bins[m_head] = b;
    m_head = (m_head + 1) % kCapacity;
    if (m_count < kCapacity)
        ++m_count;
}

const ScopeBin &WaveformView::binFromNewest(int i) const
{
    const int idx = ((m_head - 1 - i) % kCapacity + kCapacity) % kCapacity;
    return m_bins[idx];
}

void WaveformView::advance(qreal dt)
{
    m_carry += kBinsPerSecond * dt;
    const int want = int(m_carry);
    m_carry -= want;
    if (want <= 0)
        return;

    m_scrollPhase = std::fmod(m_scrollPhase + want, 10000.0);

    int got = 0;
    float loudest = 0.0f;

    if (m_handle)
    {
        const int ceiling = want * 4 + 8;
        ScopeBin b;
        while (got < ceiling && m_handle->scope()->pop(b))
        {
            pushBin(b);
            loudest = std::max({loudest, std::fabs(b.min), std::fabs(b.max)});
            ++got;
        }
    }

    if (got == 0)
    {
        for (int i = 0; i < want; ++i)
            pushBin(ScopeBin{0.0f, 0.0f, 0.0f});
    }

    if (loudest > 0.0005f)
        m_silentFor = 0.0;
    else
        m_silentFor += dt;

    update();
}

void WaveformView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    RackPaint::inset(p, r, 4, Theme::screenBg());

    const QRectF face = r.adjusted(3, 3, -3, -3);
    p.save();
    p.setClipRect(face);

    const qreal cy = face.center().y();
    const qreal halfH = face.height() / 2.0 - 2.0;

    p.setPen(QPen(Theme::screenGrid(), 1));
    for (const qreal frac : {0.5, 1.0})
    {
        p.drawLine(QPointF(face.left(), cy - halfH * frac), QPointF(face.right(), cy - halfH * frac));
        p.drawLine(QPointF(face.left(), cy + halfH * frac), QPointF(face.right(), cy + halfH * frac));
    }
    const qreal tickSpacing = kBinsPerSecond / 2.0;
    const qreal offset = std::fmod(m_scrollPhase, tickSpacing);
    for (qreal x = face.right() - offset; x > face.left(); x -= tickSpacing)
        p.drawLine(QPointF(x, face.top()), QPointF(x, face.bottom()));

    p.setPen(QPen(Theme::screenGrid().lighter(135), 1));
    p.drawLine(QPointF(face.left(), cy), QPointF(face.right(), cy));

    const int w = int(face.width());
    const int columns = std::min(w, m_count);

    QVector<QLineF> envelope;
    QVector<QLineF> core;
    QVector<QLineF> hot;
    envelope.reserve(columns);
    core.reserve(columns);

    float windowPeak = 0.0f;

    for (int i = 0; i < columns; ++i)
    {
        const ScopeBin &b = binFromNewest(i);
        const qreal x = face.right() - i - 0.5;

        const float hi = std::max(b.max, 0.0f);
        const float lo = std::min(b.min, 0.0f);
        windowPeak = std::max({windowPeak, std::fabs(b.max), std::fabs(b.min)});

        const qreal yTop = cy - std::min<qreal>(hi, 1.0) * halfH;
        const qreal yBot = cy - std::max<qreal>(lo, -1.0) * halfH;

        const QLineF line(x, std::min(yTop, cy - 0.5), x, std::max(yBot, cy + 0.5));
        if (std::fabs(b.max) >= 1.0f || std::fabs(b.min) >= 1.0f)
            hot.append(line);
        else
            envelope.append(line);

        const qreal rms = std::min<qreal>(b.rms, 1.0) * halfH;
        if (rms > 0.6)
            core.append(QLineF(x, cy - rms, x, cy + rms));
    }

    const QColor base = m_muted ? QColor(0x5c, 0x66, 0x72) : Theme::signal();

    QColor glow = base;
    glow.setAlpha(46);
    p.setPen(QPen(glow, 3.0));
    p.drawLines(envelope);

    p.setPen(QPen(base, 1.0));
    p.drawLines(envelope);

    QColor coreColour = base.lighter(m_muted ? 120 : 145);
    coreColour.setAlpha(220);
    p.setPen(QPen(coreColour, 1.0));
    p.drawLines(core);

    if (!hot.isEmpty())
    {
        p.setPen(QPen(Theme::clip(), 1.0));
        p.drawLines(hot);
    }

    QLinearGradient vg(face.topLeft(), face.bottomLeft());
    vg.setColorAt(0.0, QColor(0, 0, 0, 90));
    vg.setColorAt(0.25, QColor(0, 0, 0, 0));
    vg.setColorAt(0.75, QColor(0, 0, 0, 0));
    vg.setColorAt(1.0, QColor(0, 0, 0, 90));
    p.fillRect(face, vg);

    p.restore();

    p.setFont(Theme::monoFont(9));

    QString status;
    QColor statusColour;
    if (!m_handle)
    {
        status = QStringLiteral("OFFLINE");
        statusColour = Theme::textFaint();
    }
    else if (m_muted)
    {
        status = QStringLiteral("MUTED");
        statusColour = Theme::warn();
    }
    else if (m_silentFor > 1.5)
    {
        status = QStringLiteral("NO SIGNAL");
        statusColour = Theme::textFaint();
    }

    if (!status.isEmpty())
    {
        const QFontMetrics fm(p.font());
        QRectF plate(0, 0, fm.horizontalAdvance(status) + 18, fm.height() + 8);
        plate.moveCenter(face.center());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(plate, 3, 3);
        p.setPen(statusColour);
        p.drawText(plate, Qt::AlignCenter, status);
    }

    if (m_handle && windowPeak > 0.0f)
    {
        const float db = 20.0f * std::log10(windowPeak);
        p.setPen(db > -0.5f ? Theme::clip() : Theme::textFaint());
        p.drawText(face.adjusted(0, 2, -4, 0), Qt::AlignTop | Qt::AlignRight,
                   QStringLiteral("%1 dB").arg(db, 0, 'f', 1));
    }
}
