#pragma once

#include <atomic>
#include <cstddef>
#include <concepts>
#include <vector>
#include <optional>
#include <new>

namespace apex {

// Standard 64-byte L1 cache line size to prevent false sharing without compiler ABI warning
constexpr size_t cache_line_size = 64;

template <typename T>
class SPSCRingBuffer {
public:
    explicit SPSCRingBuffer(size_t capacity)
        : capacity_(round_up_to_power_of_two(capacity > 2 ? capacity : 2)),
          mask_(capacity_ - 1),
          buffer_(capacity_),
          dropped_count_(0) {}

    ~SPSCRingBuffer() = default;

    // Disallow copying and moving for lock-free thread safety
    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer(SPSCRingBuffer&&) = delete;
    SPSCRingBuffer& operator=(SPSCRingBuffer&&) = delete;

    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire);

        if ((current_tail - current_head) >= capacity_) {
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            return false; // Buffer full
        }

        buffer_[current_tail & mask_] = item;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return false; // Buffer empty
        }

        item = buffer_[current_head & mask_];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        return (current_tail >= current_head) ? (current_tail - current_head) : 0;
    }

    [[nodiscard]] size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t dropped_count() const noexcept {
        return dropped_count_.load(std::memory_order_relaxed);
    }

private:
    static size_t round_up_to_power_of_two(size_t v) {
        --v;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        return ++v;
    }

    const size_t capacity_;
    const size_t mask_;
    std::vector<T> buffer_;

    // Align indices to separate cache lines to prevent false sharing
    alignas(cache_line_size) std::atomic<size_t> tail_{0};
    alignas(cache_line_size) std::atomic<size_t> head_{0};
    alignas(cache_line_size) std::atomic<uint64_t> dropped_count_{0};
};

} // namespace apex
