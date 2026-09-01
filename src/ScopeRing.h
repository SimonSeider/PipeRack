#pragma once

#include <atomic>
#include <cstdint>

struct ScopeBin
{
    float min;
    float max;
    float rms;
};

class ScopeRing
{
public:
    static constexpr uint32_t kCapacity = 4096;
    static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of two");

    void push(const ScopeBin &bin) noexcept
    {
        const uint32_t w = m_write.load(std::memory_order_relaxed);
        const uint32_t r = m_read.load(std::memory_order_acquire);
        if (w - r >= kCapacity)
            return;
        m_data[w & (kCapacity - 1)] = bin;
        m_write.store(w + 1, std::memory_order_release);
    }

    bool pop(ScopeBin &out) noexcept
    {
        const uint32_t r = m_read.load(std::memory_order_relaxed);
        if (r == m_write.load(std::memory_order_acquire))
            return false;
        out = m_data[r & (kCapacity - 1)];
        m_read.store(r + 1, std::memory_order_release);
        return true;
    }

    void clear() noexcept { m_read.store(m_write.load(std::memory_order_acquire), std::memory_order_release); }

private:
    ScopeBin m_data[kCapacity]{};
    std::atomic<uint32_t> m_write{0};
    std::atomic<uint32_t> m_read{0};
};
