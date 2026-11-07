#pragma once
#include "acoustic_radar/audio.hpp"
#include "acoustic_radar/sync.hpp"
#include "acoustic_radar/spectrum.hpp"
#include <deque>
#include <mutex>
#include <thread>
#include <exception>
#include <optional>
namespace acoustic_radar {
// Потоковый сборщик хранит только хвост для поиска маркера и полного цикла.
// append() вызывается последовательно одним рабочим потоком.
class CycleProcessor {
public:
    std::vector<SpectrumFrame> append(const InputFrame& frame);
private:
    SpectrumAnalyzer analyzer_;
    std::vector<float> samples_;
    uint64_t base_ = 0;
    uint64_t nextIndex_ = 0;
    bool initialized_ = false;
    std::optional<SyncMarker> pendingMarker_;
};
// Владеет потоком анализа. Мьютекс защищает только готовые результаты
// и исключение; аудиопроизводитель никогда не захватывает этот мьютекс.
class ProcessingWorker final {
public:
    explicit ProcessingWorker(AudioDevice& audio);
    ~ProcessingWorker();
    std::vector<SpectrumFrame> take();
    uint64_t droppedResults() const noexcept { return droppedResults_.load(); }
private:
    std::mutex mutex_;
    std::deque<SpectrumFrame> ready_;
    std::exception_ptr error_;
    std::atomic<uint64_t> droppedResults_{0};
    std::jthread thread_;
};
}
