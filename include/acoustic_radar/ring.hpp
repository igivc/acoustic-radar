#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace acoustic_radar {
// Один поток записи и один поток чтения. Полная очередь отвергает новые данные; непрочитанные не перезаписываются.
template<class T, size_t Capacity> class SpscRing final {
public:
    bool push(const T& value) noexcept {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) == Capacity) return false;
        // Чтение read_ с memory_order_acquire гарантирует, что потребитель закончил читать освобождённую
        // ячейку; запись write_ с memory_order_release публикует заполненную ячейку целиком.
        data_[write % Capacity] = value;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        // Чтение write_ с memory_order_acquire видит данные производителя; запись read_ с memory_order_release разрешает
        // повторное использование ячейки только после копирования значения.
        value = data_[read % Capacity];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
private:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::array<T, Capacity> data_{};
    // Разносим изменяемые разными потоками индексы по строкам кэша.
    alignas(64) std::atomic<uint64_t> write_{0};
    // Разносим изменяемые разными потоками индексы по строкам кэша.
    alignas(64) std::atomic<uint64_t> read_{0};
};
// Один кадр содержит одновременные отсчёты всех входов. index — позиция
// в исходном потоке; он продолжает расти даже при переполнении кольца.
struct InputFrame {
    uint64_t index;
    std::array<float, 4> samples;
};
}
