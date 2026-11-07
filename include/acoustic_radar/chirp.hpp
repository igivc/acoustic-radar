#pragma once

#include <atomic>
#include <cstdint>

namespace acoustic_radar {

// Один выходной аудиокадр: два одновременных отсчёта Float32.
// left поступает на OUTPUT 1 (петля синхронизации), right — на OUTPUT 2
// (динамик). Значения — цифровые амплитуды, не напряжение и не звуковое давление.
struct StereoSample
{
    float left;
    float right;
};

// Генератор повторяющегося цикла с двумя линейно-частотно-модулированными
// сигналами (ЛЧМ, chirp). Частота каждого сигнала плавно растёт от начальной
// к конечной; амплитудная огибающая сглаживает его включение и выключение.
// Оба сигнала начинаются одновременно, но имеют разные частоты и длительности.
//
// Генератор не использует системные часы: один вызов nextSample() соответствует
// одному периоду дискретизации, 1/48000 с. Поэтому ритм задаёт аудиопоток,
// независимо от скорости отрисовки окна и размера блока PortAudio.
// nextSample(), reset() и sampleIndex() вызываются только из аудиопотока.
// setAcousticParameters() можно одновременно вызывать из потока интерфейса:
// он передаёт запрос, а действующие параметры меняет только обработчик звука.
class ChirpGenerator final
{
public:
    static constexpr double SampleRate = 48000.0;

    // Период между началами соседних возбуждений, включая время тишины.
    // Это не длительность самого chirp: при исходных настройках после 250 мс
    // возбуждения остаются 750 мс для регистрации затухающего отклика.
    static constexpr double CycleDuration = 1.000;

    // Короткий высокочастотный маркер идёт по электрической петле OUTPUT 1 →
    // INPUT 1. Корреляционный детектор использует этот же генератор для эталона.
    // Его параметры независимы от перестройки акустического возбуждения.
    // Синхромаркер: левый канал
    static constexpr double SyncStartFrequency = 8000.0;
    static constexpr double SyncEndFrequency = 12000.0;
    static constexpr double SyncDuration = 0.010;

    // Исходные параметры возбуждения. Во время работы частота и длительность
    // регулируются через setAcousticParameters(); эти константы не отражают
    // активные настройки. AcousticSamples также описывает исходную длительность.
    // Акустическое возбуждение: правый канал
    static constexpr double AcousticStartFrequency = 1900.0;
    static constexpr double AcousticEndFrequency = 2900.0;
    static constexpr double AcousticDuration = 0.250;

    // Амплитуды до умножения на огибающую: 0,1 и 0,5 полной цифровой
    // шкалы. Реальная громкость зависит от выходного усиления и динамика.
    static constexpr float SyncAmplitude = 0.1f;
    static constexpr float AcousticAmplitude = 0.5f;

    static constexpr uint64_t CycleSamples =
        static_cast<uint64_t>(SampleRate * CycleDuration);

    static constexpr uint64_t SyncSamples =
        static_cast<uint64_t>(SampleRate * SyncDuration);

    static constexpr uint64_t AcousticSamples =
        static_cast<uint64_t>(SampleRate * AcousticDuration);

    // Передаёт новые настройки из интерфейса, не меняя текущий цикл.
    // startHz — нижняя граница в герцах, верхняя равна startHz + BandwidthHz;
    // durationMs — длительность возбуждения в миллисекундах, не период цикла.
    // Значения ограничиваются диапазонами ниже. Если до начала следующего
    // цикла пришло несколько запросов, применяется тот, который генератор
    // прочитает на границе цикла; промежуточные запросы могут не использоваться.
    // Метод не ждёт подтверждения от аудиопотока.
    void setAcousticParameters(unsigned startHz, unsigned durationMs) noexcept;

    // Верхняя частота не превышает 20000 Гц и остаётся ниже Найквиста 24000 Гц.
    // Минимальные 20 мс вмещают два участка огибающей по 10 мс;
    // максимальные 590 мс оставляют паузу до конца секундного цикла.
    static constexpr unsigned BandwidthHz = 1000;
    static constexpr unsigned MinStartHz = 100;
    static constexpr unsigned MaxStartHz = 19000;
    static constexpr unsigned MinDurationMs = 20;
    static constexpr unsigned MaxDurationMs = 590;

    // Возвращает текущую пару отсчётов и продвигает счётчик на один кадр.
    // Не выделяет память, не блокируется и не выполняет файловые операции.
    [[nodiscard]] StereoSample nextSample() noexcept;

    // Возвращает позицию к началу цикла. Запрошенные настройки сохраняются
    // и будут приняты следующим nextSample(). Не вызывать параллельно генерации.
    void reset() noexcept
    {
        sampleIndex_ = 0;
    }

    // Число уже сгенерированных кадров с момента создания или reset(),
    // а не позиция внутри одного цикла. Читать из того же аудиопотока.
    [[nodiscard]] uint64_t sampleIndex() const noexcept
    {
        return sampleIndex_;
    }

private:
    // Публикация настроек не должна скрыто захватывать мьютекс в обработчике звука.
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    // Старшие 32 бита содержат startHz, младшие — durationMs.
    // Только эта переменная доступна обоим потокам. Поля ниже читает
    // и изменяет только аудиопоток.
    std::atomic<uint64_t> requestedParameters_{(uint64_t{1900} << 32) | 250};
    unsigned startHz_ = 1900;
    unsigned durationMs_ = 250;
    uint64_t sampleIndex_ = 0;
};

} // пространство имён acoustic_radar
