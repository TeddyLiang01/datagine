#pragma once

#include "datagine/core/market_event.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace datagine {

class SpscRingBuffer {
public:
    explicit SpscRingBuffer(std::size_t capacity_power_of_two)
        : capacity_(capacity_power_of_two),
          mask_(capacity_power_of_two - 1),
          buffer_(capacity_power_of_two) {
        if (capacity_power_of_two == 0 || !is_power_of_two(capacity_power_of_two)) {
            throw std::invalid_argument{"SpscRingBuffer capacity must be a nonzero power of two"};
        }
    }

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    [[nodiscard]] bool try_push(const MarketEvent& event) noexcept {
        const auto write = write_sequence_.value.load(std::memory_order_relaxed);
        const auto read = read_sequence_.value.load(std::memory_order_acquire);

        if (write - read >= capacity_) {
            return false;
        }

        buffer_[write & mask_] = event;
        write_sequence_.value.store(write + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(MarketEvent& out) noexcept {
        const auto read = read_sequence_.value.load(std::memory_order_relaxed);
        const auto write = write_sequence_.value.load(std::memory_order_acquire);

        if (write == read) {
            return false;
        }

        out = buffer_[read & mask_];
        read_sequence_.value.store(read + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] std::size_t approximate_size() const noexcept {
        const auto write = write_sequence_.value.load(std::memory_order_acquire);
        const auto read = read_sequence_.value.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

    [[nodiscard]] bool empty() const noexcept {
        return approximate_size() == 0;
    }

    [[nodiscard]] bool full() const noexcept {
        return approximate_size() >= capacity_;
    }

private:
    struct alignas(64) PaddedSequence {
        std::atomic<std::uint64_t> value{0};
    };

    [[nodiscard]] static constexpr bool is_power_of_two(std::size_t value) noexcept {
        return (value & (value - 1)) == 0;
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<MarketEvent> buffer_;
    PaddedSequence write_sequence_{};
    PaddedSequence read_sequence_{};
};

static_assert(alignof(SpscRingBuffer) >= alignof(std::max_align_t));

}  // namespace datagine
