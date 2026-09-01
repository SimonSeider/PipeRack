#pragma once
#include "AudioBackend.h"
#include "ScopeRing.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

struct ScopeMeter
{
    static constexpr int kBinsPerSecond = 150;

    std::atomic<uint32_t> rtChannels{2};
    std::atomic<uint32_t> binSize{320};
    std::atomic<int> negotiatedRate{0};

    ScopeRing ring;
    std::atomic<float> peaks[kMaxChannels];

    ScopeMeter()
    {
        for (int i = 0; i < kMaxChannels; ++i)
            peaks[i].store(0.0f, std::memory_order_relaxed);
    }

    void setFormat(int rate, int channels)
    {
        if (rate > 0)
        {
            negotiatedRate.store(rate, std::memory_order_relaxed);
            binSize.store(std::max<uint32_t>(1, uint32_t(rate / kBinsPerSecond)),
                          std::memory_order_relaxed);
        }
        if (channels > 0)
            rtChannels.store(uint32_t(channels), std::memory_order_relaxed);
    }

    float takePeak(int channel)
    {
        if (channel < 0 || channel >= kMaxChannels)
            return 0.0f;
        return peaks[channel].exchange(0.0f, std::memory_order_relaxed);
    }

    void consume(const float *src, uint32_t frames) noexcept
    {
        const uint32_t ch = rtChannels.load(std::memory_order_relaxed);
        if (ch == 0)
            return;
        const uint32_t nch = std::min<uint32_t>(ch, kMaxChannels);
        const uint32_t bin = binSize.load(std::memory_order_relaxed);

        float blockPeak[kMaxChannels] = {0.0f};

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float *f = src + size_t(i) * ch;
            float mono = 0.0f;
            for (uint32_t c = 0; c < nch; ++c)
            {
                const float v = f[c];
                const float a = std::fabs(v);
                if (a > blockPeak[c])
                    blockPeak[c] = a;
                mono += v;
            }
            mono /= float(nch);

            if (mono < binMin)
                binMin = mono;
            if (mono > binMax)
                binMax = mono;
            binSum += double(mono) * double(mono);

            if (++binCount >= bin)
            {
                ring.push({binMin, binMax, float(std::sqrt(binSum / binCount))});
                binMin = 1.0f;
                binMax = -1.0f;
                binSum = 0.0;
                binCount = 0;
            }
        }

        for (uint32_t c = 0; c < nch; ++c)
        {
            if (blockPeak[c] > peaks[c].load(std::memory_order_relaxed))
                peaks[c].store(blockPeak[c], std::memory_order_relaxed);
        }
    }

private:
    float binMin = 1.0f;
    float binMax = -1.0f;
    double binSum = 0.0;
    uint32_t binCount = 0;
};
