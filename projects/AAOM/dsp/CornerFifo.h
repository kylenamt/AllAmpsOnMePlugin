#pragma once

// CornerFifo — a tiny single-producer / single-consumer lock-free queue for
// pushing corner-embedding updates from the message thread to the audio thread.
// JUCE-free (only <atomic>/<vector>) so MorphEngine stays testable without JUCE.
//
// Producer: the UI / message thread (paste, clear, preset load) — push().
// Consumer: the audio thread — drain() at the top of the block.
// Latest-wins semantics are fine; corners change rarely (human interaction), so
// the modest capacity never overflows in practice and push() drops on full.

#include <atomic>
#include <cstdint>
#include <vector>

namespace aaom
{

class CornerFifo
{
public:
    // Preallocate `capacity` slots, each holding an embedding of `embeddingDim`.
    void resize(int capacity, int embeddingDim)
    {
        capacity_ = capacity > 1 ? capacity : 1;
        index_.assign(static_cast<std::size_t>(capacity_), 0);
        isClear_.assign(static_cast<std::size_t>(capacity_), 0);
        emb_.assign(static_cast<std::size_t>(capacity_), std::vector<float>(static_cast<std::size_t>(embeddingDim), 0.0f));
        writePos_.store(0, std::memory_order_relaxed);
        readPos_.store(0, std::memory_order_relaxed);
    }

    // Producer (message thread). `data` has `n` floats (ignored when clear==true).
    // Returns false if the queue is full (update dropped).
    bool push(int cornerIndex, const float* data, int n, bool clear)
    {
        const uint32_t w = writePos_.load(std::memory_order_relaxed);
        const uint32_t r = readPos_.load(std::memory_order_acquire);
        if (w - r >= static_cast<uint32_t>(capacity_))
            return false; // full

        const std::size_t s = w % static_cast<uint32_t>(capacity_);
        index_[s] = cornerIndex;
        isClear_[s] = clear ? 1 : 0;
        if (!clear)
        {
            auto& dst = emb_[s];
            const int m = n < static_cast<int>(dst.size()) ? n : static_cast<int>(dst.size());
            for (int i = 0; i < m; ++i)
                dst[static_cast<std::size_t>(i)] = data[i];
        }
        writePos_.store(w + 1, std::memory_order_release);
        return true;
    }

    // Consumer (audio thread). apply(index, clear, embedding) is called for each
    // pending update in order. `embedding` is valid only during the callback.
    template <typename Apply>
    void drain(Apply&& apply)
    {
        uint32_t r = readPos_.load(std::memory_order_relaxed);
        const uint32_t w = writePos_.load(std::memory_order_acquire);
        while (r != w)
        {
            const std::size_t s = r % static_cast<uint32_t>(capacity_);
            apply(index_[s], isClear_[s] != 0, emb_[s]);
            ++r;
        }
        readPos_.store(r, std::memory_order_release);
    }

private:
    int capacity_ = 1;
    std::vector<int> index_;
    std::vector<char> isClear_;
    std::vector<std::vector<float>> emb_;
    std::atomic<uint32_t> writePos_{0};
    std::atomic<uint32_t> readPos_{0};
};

} // namespace aaom
