#pragma once
#include <QWidget>
#include <vector>
#include "AudioBackend.h"

class WaveformView : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformView(QWidget *parent = nullptr);

    void setSource(CableHandle *handle);
    void setMuted(bool muted);
    void clear();

    void advance(qreal dtSeconds);

    QSize sizeHint() const override { return QSize(340, 104); }
    QSize minimumSizeHint() const override { return QSize(160, 72); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    static constexpr int kCapacity = 2048;
    static constexpr qreal kBinsPerSecond = 150.0;

    void pushBin(const ScopeBin &b);
    const ScopeBin &binFromNewest(int i) const;

    CableHandle *m_handle = nullptr;
    std::vector<ScopeBin> m_bins;
    int m_head = 0;
    int m_count = 0;
    qreal m_carry = 0.0;
    qreal m_silentFor = 10.0;
    qreal m_scrollPhase = 0.0;
    bool m_muted = false;
};
