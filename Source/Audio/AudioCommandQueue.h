#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace padflow {
#if defined(_MSC_VER)
#pragma warning(push)
// Cache-line separation is intentional; MSVC C4324 reports the resulting class padding.
#pragma warning(disable : 4324)
#endif

template <typename Value, std::size_t Capacity> class SpscQueue final {
    static_assert(Capacity > 0);
    static_assert(std::is_trivially_copyable_v<Value>);

  public:
    [[nodiscard]] bool tryPush(const Value& value) noexcept {
        const auto write = writePosition_.load(std::memory_order_relaxed);
        const auto read = readPosition_.load(std::memory_order_acquire);
        if (write - read == Capacity)
            return false;

        storage_[write % Capacity] = value;
        writePosition_.store(write + 1U, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool tryPop(Value& value) noexcept {
        const auto read = readPosition_.load(std::memory_order_relaxed);
        const auto write = writePosition_.load(std::memory_order_acquire);
        if (read == write)
            return false;

        value = storage_[read % Capacity];
        readPosition_.store(read + 1U, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const auto write = writePosition_.load(std::memory_order_acquire);
        const auto read = readPosition_.load(std::memory_order_acquire);
        return write - read;
    }

    void resetWhenQuiescent() noexcept {
        readPosition_.store(0U, std::memory_order_relaxed);
        writePosition_.store(0U, std::memory_order_relaxed);
    }

  private:
    std::array<Value, Capacity> storage_{};
    alignas(64) std::atomic<std::size_t> writePosition_{0U};
    alignas(64) std::atomic<std::size_t> readPosition_{0U};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

enum class AudioCommandType : std::uint8_t {
    none,
    stopAll,
    publishProject,
    publishAsset,
    setParameter
};

struct AudioCommand final {
    AudioCommandType type{AudioCommandType::none};
    std::uint32_t objectIndex{0U};
    std::uint32_t generation{0U};
    float value{0.0F};
};

using AudioCommandQueue = SpscQueue<AudioCommand, 1024U>;
static_assert(std::is_trivially_copyable_v<AudioCommand>);
} // namespace padflow
