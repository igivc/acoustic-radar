/**
 * @file processing.cpp
 * @brief Накопление входа, обработка циклов и передача результатов.
 */
#include "acoustic_radar/processing.hpp"
#include "acoustic_radar/sync.hpp"
#include <chrono>
namespace acoustic_radar {
std::vector<SpectrumFrame> CycleProcessor::append(const InputFrame& frame) {
    // Индексы относятся к исходному аудиопотоку, а не к позиции в кольце.
    // При пропуске кадров удаляем накопленные данные и найденный маркер.
    // Иначе FFT могла бы объединить участки сигнала до и после пропуска.
    // Например, после индекса 1000 ожидается 1001; если кольцо отбросило кадры
    // и пришёл 1200, старый хвост нельзя считать непрерывным с новым входом.
    // Проверяется последовательность кадров приложения; эта проверка сама
    // по себе не показывает, сколько кадров потерял драйвер PortAudio.
    if (!initialized_ || frame.index != nextIndex_) {
        samples_.clear(); pendingMarker_.reset(); base_ = frame.index; initialized_ = true;
    }
    // Сохраняем все четыре канала: детектор использует первый, FFT - второй.
    // Выделения памяти здесь допустимы: append работает вне callback PortAudio.
    nextIndex_ = frame.index + 1;
    samples_.insert(samples_.end(), frame.samples.begin(), frame.samples.end());
    constexpr size_t cycle = ChirpGenerator::CycleSamples;
    constexpr size_t markerLength = ChirpGenerator::SyncSamples;
    std::vector<SpectrumFrame> output;
    // Для поиска по начальному периоду нужен также хвост длиной маркера:
    // синхросигнал, начавшийся возле конца периода, должен целиком попасть в буфер.
    // Проверяем состояние лишь при размере, кратном 512 кадрам (10,67 мс).
    // Это ограничивает частоту проверок и не связано с шагом окон
    // STFT (Short-Time Fourier Transform) 1024.
    const size_t frames = samples_.size()/4;
    if (frames < cycle + markerLength || frames % 512 != 0) return output;
    // Найденный маркер сохраняем, пока не накопится полный цикл после него.
    // Пока ждём полный цикл, не тратим время на повторный поиск того же маркера.
    if (!pendingMarker_) {
        auto markers = SyncDetector{}.detect(samples_.data(), frames);
        if (!markers.empty()) pendingMarker_ = markers.front();
    }
    size_t discard = 0;
    // Без маркера продвигаем область поиска на период, сохраняя хвост:
    // маркер на стыке предыдущей и следующей порций данных не будет обрезан.
    // Если маркер уже есть, но цикл ещё не завершился, discard остаётся нулём.
    if (!pendingMarker_) {
        discard = cycle; // Сохраняем хвост на границе поиска.
    } else {
        const auto marker = *pendingMarker_;
        // Здесь frames и sampleIndex измеряются в кадрах. Обрабатываем ровно
        // цикл [marker, marker+cycle), включая возбуждение и последующее затухание.
        if (marker.sampleIndex + cycle <= frames) {
            auto spectrum = analyzer_.analyze(samples_.data(), frames, marker.sampleIndex, marker.correlation);
            // Анализатор принимает локальное смещение; наружу возвращаем
            // абсолютную позицию, сохраняющую смысл после удаления начала буфера.
            spectrum.markerSample += base_;
            output.push_back(std::move(spectrum));
            // Оставляем запас перед следующим ожидаемым маркером, чтобы
            // небольшое смещение периода не обрезало начало синхросигнала.
            discard = marker.sampleIndex + cycle - 512;
            pendingMarker_.reset();
        }
    }
    // Удаление начала буфера меняет локальные индексы, но не время в исходном потоке.
    // Умножение на четыре переводит число кадров в количество float-элементов.
    if (discard) {
        samples_.erase(samples_.begin(), samples_.begin() + discard*4);
        base_ += discard;
    }
    return output;
}
ProcessingWorker::ProcessingWorker(AudioDevice& audio)
    : thread_([this, &audio](std::stop_token stop) {
        try {
            // Создаём анализатор в рабочем потоке; его план FFTW и буферы
            // не используются одновременно основным потоком.
            CycleProcessor processor;
            while (!stop.stop_requested()) {
                InputFrame input;
                bool received = false;
                // Читаем кадры, пока они есть в кольце. Запрос остановки проверяем
                // перед каждым чтением, даже если новые данные поступают непрерывно.
                while (!stop.stop_requested() && audio.readInput(input)) {
                    received = true;
                    auto frames = processor.append(input);
                    if (!frames.empty()) {
                        // Корреляция и FFT уже закончены до захвата мьютекса.
                        // Защищаем только передачу результата, не весь анализ.
                        std::lock_guard lock(mutex_);
                        for (auto& frame : frames) {
                            // Медленный интерфейс не должен приводить к росту памяти:
                            // приоритет у свежих циклов, старейший результат удаляется.
                            if (ready_.size() == 8)
                            {
                                ready_.pop_front();
                                ++droppedResults_;
                            }
                            ready_.push_back(std::move(frame));
                        }
                    }
                }
                // Опрос выполняется только в рабочем потоке; обработчик звука
                // не использует условную переменную и не захватывает мьютекс.
                if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        // Исключение сохраняется под тем же мьютексом, что читает take().
        // Без перехвата исключение из функции std::jthread завершило бы процесс.
        // После сохранения ошибки рабочий поток выходит, основной увидит её
        // при следующем take(), после чего main освободит ресурсы.
        } catch (...) { std::lock_guard lock(mutex_); error_ = std::current_exception(); }
    })
{
    // Поток уже запущен инициализатором thread_; дополнительных действий нет.
}

/**
 * @brief Сначала ждём завершения рабочего потока, затем освобождаем его данные.
 *
 * AudioDevice уничтожается позже worker, поэтому остаётся доступным до конца чтения.
 */
ProcessingWorker::~ProcessingWorker() {
    // Не удерживаем mutex_ во время join: завершающийся поток может нуждаться
    // в нём, чтобы записать результат или ошибку. request_stop не прерывает
    // текущую FFT/корреляцию; ждём её окончания и следующей проверки флага.
    thread_.request_stop();
    thread_.join();
}

std::vector<SpectrumFrame> ProcessingWorker::take() {
    // Под мьютексом читаем и очередь, и сообщение об ошибке.
    // При исключении lock_guard освободит мьютекс перед выходом из take().
    // Поэтому деструктор worker сможет ждать поток, не удерживая нужный ему мьютекс.
    std::lock_guard lock(mutex_);
    if (error_) std::rethrow_exception(error_);
    std::vector<SpectrumFrame> result;
    result.reserve(ready_.size()); // Избегаем повторных расширений вектора при переносе.
    // Перемещаем готовые спектры в локальный вектор, который получит SDL-поток.
    // После возврата мьютекс свободен, поэтому отрисовка не задерживает worker.
    while (!ready_.empty()) {
        result.push_back(std::move(ready_.front()));
        ready_.pop_front();
    }
    return result;
}
} // пространство имён acoustic_radar
