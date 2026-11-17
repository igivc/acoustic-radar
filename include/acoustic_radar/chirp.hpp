/**
 * @file chirp.hpp
 * @brief Генератор синхромаркера и акустического возбуждения.
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace acoustic_radar {

/**
 * @brief Один выходной аудиокадр: два одновременных отсчёта Float32.
 *
 * left поступает на OUTPUT 1 (петля синхронизации), right - на OUTPUT 2
 * (динамик). Значения - цифровые амплитуды, не напряжение и не звуковое давление.
 */
struct StereoSample
{
    /**
     * @brief Синхромаркер для OUTPUT 1.
     */
    float left;
    /**
     * @brief Акустическое возбуждение для OUTPUT 2.
     */
    float right;
};

/**
 * @brief Генератор повторяющегося цикла с двумя линейно-частотно-модулированными сигналами (ЛЧМ, chirp).
 *
 * Частота каждого сигнала плавно растёт от начальной
 * к конечной; амплитудная огибающая сглаживает его включение и выключение.
 * Оба сигнала начинаются одновременно, но имеют разные частоты и длительности.
 *
 * Генератор не использует системные часы: один вызов nextSample() соответствует
 * одному периоду дискретизации, 1/48000 с. Поэтому ритм задаёт аудиопоток,
 * независимо от скорости отрисовки окна и размера блока PortAudio.
 * nextSample(), reset() и sampleIndex() вызываются только из аудиопотока.
 * setAcousticParameters() можно одновременно вызывать из потока интерфейса:
 * он передаёт запрос, а действующие параметры меняет только обработчик звука.
 */
class ChirpGenerator final
{
public:
    /**
     * @brief Частота дискретизации выходного сигнала в герцах.
     */
    static constexpr double SampleRate = 48000.0;

    /**
     * @brief Период между началами соседних возбуждений, включая время тишины.
     *
     * Это не длительность самого chirp: при исходных настройках после 250 мс
     * возбуждения остаются 750 мс для регистрации затухающего отклика.
     */
    static constexpr double CycleDuration = 1.000;

    /**
     * @brief Короткий высокочастотный маркер идёт по электрической петле OUTPUT 1 -> INPUT 1.
     *
     * Корреляционный детектор использует этот же генератор для эталона.
     * Его параметры независимы от перестройки акустического возбуждения.
     * Синхромаркер: левый канал
     */
    static constexpr double SyncStartFrequency = 8000.0;
    /**
     * @brief Конечная частота синхромаркера в герцах.
     */
    static constexpr double SyncEndFrequency = 12000.0;
    /**
     * @brief Длительность синхромаркера в секундах.
     */
    static constexpr double SyncDuration = 0.010;

    /**
     * @brief Исходные параметры возбуждения.
     *
     * Во время работы частота и длительность
     * регулируются через setAcousticParameters(); эти константы не отражают
     * активные настройки. AcousticSamples также описывает исходную длительность.
     * Акустическое возбуждение: правый канал
     */
    static constexpr double AcousticStartFrequency = 1900.0;
    /**
     * @brief Исходная верхняя частота акустического возбуждения в герцах.
     */
    static constexpr double AcousticEndFrequency = 2900.0;
    /**
     * @brief Исходная длительность акустического возбуждения в секундах.
     */
    static constexpr double AcousticDuration = 0.250;

    /**
     * @brief Амплитуды до умножения на огибающую: 0,1 и 0,5 полной цифровой шкалы.
     *
     * Реальная громкость зависит от выходного усиления и динамика.
     */
    static constexpr float SyncAmplitude = 0.1f;
    /**
     * @brief Амплитуда акустического chirp до умножения на огибающую.
     */
    static constexpr float AcousticAmplitude = 0.5f;

    /**
     * @brief Число аудиокадров в одном цикле, включая тишину.
     */
    static constexpr uint64_t CycleSamples =
        static_cast<uint64_t>(SampleRate * CycleDuration);

    /**
     * @brief Число отсчётов синхромаркера.
     */
    static constexpr uint64_t SyncSamples =
        static_cast<uint64_t>(SampleRate * SyncDuration);

    /**
     * @brief Число отсчётов возбуждения при исходной длительности.
     */
    static constexpr uint64_t AcousticSamples =
        static_cast<uint64_t>(SampleRate * AcousticDuration);

    /**
     * @brief Передаёт новые настройки из интерфейса, не меняя текущий цикл.
     *
     * startHz - нижняя граница в герцах, верхняя равна startHz + BandwidthHz;
     * durationMs - длительность возбуждения в миллисекундах, не период цикла.
     * Значения ограничиваются диапазонами ниже. Если до начала следующего
     * цикла пришло несколько запросов, применяется тот, который генератор
     * прочитает на границе цикла; промежуточные запросы могут не использоваться.
     * Метод не ждёт подтверждения от аудиопотока.
     *
     * @param startHz Нижняя частота в герцах; ограничивается MinStartHz и MaxStartHz.
     * @param durationMs Длительность в миллисекундах; ограничивается MinDurationMs и MaxDurationMs.
     * @note Запрос можно передавать из интерфейса одновременно с генерацией.
     */
    void setAcousticParameters(unsigned startHz, unsigned durationMs) noexcept;

    /**
     * @brief Верхняя частота не превышает 20000 Гц и остаётся ниже Найквиста 24000 Гц.
     *
     * Минимальные 20 мс вмещают два участка огибающей по 10 мс;
     * максимальные 590 мс оставляют паузу до конца секундного цикла.
     */
    static constexpr unsigned BandwidthHz = 1000;
    /**
     * @brief Минимальная нижняя частота возбуждения в герцах.
     */
    static constexpr unsigned MinStartHz = 100;
    /**
     * @brief Максимальная нижняя частота возбуждения в герцах.
     */
    static constexpr unsigned MaxStartHz = 19000;
    /**
     * @brief Минимальная длительность возбуждения в миллисекундах.
     */
    static constexpr unsigned MinDurationMs = 20;
    /**
     * @brief Максимальная длительность возбуждения в миллисекундах.
     */
    static constexpr unsigned MaxDurationMs = 590;

    /**
     * @brief Возвращает текущую пару отсчётов и продвигает счётчик на один кадр.
     *
     * Не выделяет память, не блокируется и не выполняет файловые операции.
     *
     * @return Два выходных отсчёта текущего кадра: маркер и возбуждение.
     * @note Вызывать только из аудиопотока.
     */
    [[nodiscard]] StereoSample nextSample() noexcept;

    /**
     * @brief Возвращает позицию к началу цикла.
     *
     * Запрошенные настройки сохраняются
     * и будут приняты следующим nextSample(). Не вызывать параллельно генерации.
     */
    void reset() noexcept
    {
        sampleIndex_ = 0;
    }

    /**
     * @brief Число уже сгенерированных кадров с момента создания или reset(), а не позиция внутри одного
     * цикла.
     *
     * Читать из того же аудиопотока.
     *
     * @return Число сгенерированных кадров с момента создания или reset().
     */
    [[nodiscard]] uint64_t sampleIndex() const noexcept
    {
        return sampleIndex_;
    }

private:
    // Публикация настроек не должна скрыто захватывать мьютекс в обработчике звука.
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    /**
     * @brief Старшие 32 бита содержат startHz, младшие - durationMs.
     *
     * Только эта переменная доступна обоим потокам. Поля ниже читает
     * и изменяет только аудиопоток.
     */
    std::atomic<uint64_t> requestedParameters_{(uint64_t{1900} << 32) | 250};
    unsigned startHz_ = 1900;
    unsigned durationMs_ = 250;
    uint64_t sampleIndex_ = 0;
};

} // пространство имён acoustic_radar
