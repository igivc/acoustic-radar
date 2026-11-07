#include "acoustic_radar/processing.hpp"
#include "acoustic_radar/sync.hpp"
#include <chrono>
namespace acoustic_radar {
std::vector<SpectrumFrame> CycleProcessor::append(const InputFrame& frame) {
    // Индексы относятся к исходному аудиопотоку, а не к позиции в кольце.
    // Разрыв сбрасывает и данные, и ожидающий маркер: FFT не пересекает потерю.
    if (!initialized_ || frame.index != nextIndex_) {
        samples_.clear(); pendingMarker_.reset(); base_ = frame.index; initialized_ = true;
    }
    nextIndex_ = frame.index + 1;
    samples_.insert(samples_.end(), frame.samples.begin(), frame.samples.end());
    constexpr size_t cycle = ChirpGenerator::CycleSamples;
    constexpr size_t markerLength = ChirpGenerator::SyncSamples;
    std::vector<SpectrumFrame> output;
    // Проверяем поиск не чаще одного раза на блок примерно 10 мс, а не на каждом отсчёте.
    const size_t frames = samples_.size()/4;
    if (frames < cycle + markerLength || frames % 512 != 0) return output;
    // Найденный маркер сохраняем, пока не накопится полный цикл после него.
    // Это исключает повторный дорогой поиск при каждом поступлении данных.
    if (!pendingMarker_) {
        auto markers = SyncDetector{}.detect(samples_.data(), frames);
        if (!markers.empty()) pendingMarker_ = markers.front();
    }
    size_t discard = 0;
    // Найденный маркер сохраняем, пока не накопится полный цикл после него.
    // Это исключает повторный дорогой поиск при каждом поступлении данных.
    if (!pendingMarker_) {
        discard = cycle; // Сохраняем хвост на границе, чтобы обнаруживать маркеры, разделённые между блоками.
    } else {
        const auto marker = *pendingMarker_;
        if (marker.sampleIndex + cycle <= frames) {
            auto spectrum = analyzer_.analyze(samples_.data(), frames, marker.sampleIndex, marker.correlation);
            // Анализатор принимает локальное смещение; наружу возвращаем
            // абсолютную позицию, сохраняющую смысл после удаления префикса.
            spectrum.markerSample += base_;
            output.push_back(std::move(spectrum));
            // Оставляем запас перед следующим ожидаемым маркером, чтобы
            // небольшое смещение периода не обрезало начало синхросигнала.
            discard = marker.sampleIndex + cycle - 512;
            pendingMarker_.reset();
        }
    }
    if (discard) {
        samples_.erase(samples_.begin(), samples_.begin() + discard*4);
        base_ += discard;
    }
    return output;
}
ProcessingWorker::ProcessingWorker(AudioDevice& audio)
    : thread_([this, &audio](std::stop_token stop) {
        try {
            CycleProcessor processor;
            while (!stop.stop_requested()) {
                InputFrame input;
                bool received = false;
                while (!stop.stop_requested() && audio.readInput(input)) {
                    received = true;
                    auto frames = processor.append(input);
                    if (!frames.empty()) {
                        std::lock_guard lock(mutex_);
                        for (auto& frame : frames) {
                            // Медленный интерфейс не должен приводить к росту памяти:
                            // приоритет у свежих циклов, старейший результат удаляется.
                            if (ready_.size() == 8) { ready_.pop_front(); ++droppedResults_; }
                            ready_.push_back(std::move(frame));
                        }
                    }
                }
                // Опрос выполняется только в рабочем потоке; обработчик звука
                // не использует условную переменную и не захватывает мьютекс.
                if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        // Исключение переносится в SDL-поток через take(), а не завершает процесс.
        } catch (...) { std::lock_guard lock(mutex_); error_ = std::current_exception(); }
    }) {}
// Ожидание завершения потока заканчивается до уничтожения очереди и анализатора; AudioDevice
// должен существовать дольше рабочего потока (это обеспечивает порядок владения в main).
ProcessingWorker::~ProcessingWorker() { thread_.request_stop(); thread_.join(); }
std::vector<SpectrumFrame> ProcessingWorker::take() {
    std::lock_guard lock(mutex_);
    if (error_) std::rethrow_exception(error_);
    std::vector<SpectrumFrame> result;
    result.reserve(ready_.size());
    while (!ready_.empty()) { result.push_back(std::move(ready_.front())); ready_.pop_front(); }
    return result;
}
}
