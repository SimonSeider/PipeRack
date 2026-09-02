#pragma once
#include <QColor>
#include <QString>
#include <QWidget>

class QPainter;

namespace RackPaint
{
    void card(QPainter &p, const QRectF &r, qreal radius, const QColor &fill,
              const QColor &stroke);
    void field(QPainter &p, const QRectF &r, qreal radius, const QColor &fill);
    void dot(QPainter &p, const QPointF &c, qreal radius, const QColor &colour, bool lit);
}

class Knob : public QWidget
{
    Q_OBJECT
public:
    explicit Knob(QWidget *parent = nullptr);

    float position() const { return m_pos; }
    void setPosition(float pos);

    QSize sizeHint() const override { return QSize(48, 48); }

signals:
    void positionChanged(float pos);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    float m_pos = 0.8f;
    bool m_dragging = false;
    int m_pressY = 0;
    float m_pressPos = 0.8f;
};

class LedMeter : public QWidget
{
    Q_OBJECT
public:
    explicit LedMeter(QWidget *parent = nullptr);

    void setChannelCount(int n);
    void pushPeaks(const float *peaks, int count, qreal dtSeconds);
    void reset();
    bool clipped() const { return m_clipLatch; }
    void clearClip()
    {
        m_clipLatch = false;
        update();
    }

    QSize sizeHint() const override { return QSize(20, 90); }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    int m_channels = 2;
    float m_level[8] = {0};
    float m_hold[8] = {0};
    float m_holdAge[8] = {0};
    bool m_clipLatch = false;
};

class RackButton : public QWidget
{
    Q_OBJECT
public:
    explicit RackButton(const QString &text, QWidget *parent = nullptr);

    void setCheckable(bool on) { m_checkable = on; }
    bool isChecked() const { return m_checked; }
    void setChecked(bool on);

    void setActiveColour(const QColor &c)
    {
        m_active = c;
        update();
    }
    void setDanger(bool on)
    {
        m_danger = on;
        update();
    }
    void setGhost(bool on)
    {
        m_ghost = on;
        update();
    }

    QSize sizeHint() const override;

signals:
    void clicked();
    void toggled(bool on);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QString m_text;
    bool m_checkable = false;
    bool m_checked = false;
    bool m_down = false;
    bool m_hover = false;
    bool m_danger = false;
    bool m_ghost = false;
    QColor m_active;
};
