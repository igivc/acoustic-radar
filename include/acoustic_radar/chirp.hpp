#pragma once

#include <atomic>
#include <cstdint>

namespace acoustic_radar {

struct StereoSample
{
    float left;
    float right;
};

// nextSample/reset принадлежат аудиопотоку. Только setAcousticParameters
// разрешён одновременно из интерфейса; активные параметры читает и меняет обработчик звука.
class ChirpGenerator final
{
public:
    static constexpr double SampleRate = 48000.0;

    static constexpr double CycleDuration = 1.000;

    // Синхромаркер: левый канал
    static constexpr double SyncStartFrequency = 8000.0;
    static constexpr double SyncEndFrequency = 12000.0;
    static constexpr double SyncDuration = 0.010;

    // Акустическое возбуждение: правый канал
    static constexpr double AcousticStartFrequency = 1900.0;
    static constexpr double AcousticEndFrequency = 2900.0;
    static constexpr double AcousticDuration = 0.250;

    static constexpr float SyncAmplitude = 0.1f;
    static constexpr float AcousticAmplitude = 0.5f;

    static constexpr uint64_t CycleSamples =
        static_cast<uint64_t>(SampleRate * CycleDuration);

    static constexpr uint64_t SyncSamples =
        static_cast<uint64_t>(SampleRate * SyncDuration);

    static constexpr uint64_t AcousticSamples =
        static_cast<uint64_t>(SampleRate * AcousticDuration);

    // Можно вызывать из интерфейса; параметры фиксируются на границе цикла.
    void setAcousticParameters(unsigned startHz, unsigned durationMs) noexcept;

    static constexpr unsigned BandwidthHz = 1000;
    static constexpr unsigned MinStartHz = 100;
    static constexpr unsigned MaxStartHz = 19000;
    static constexpr unsigned MinDurationMs = 20;
    static constexpr unsigned MaxDurationMs = 590;

    [[nodiscard]] StereoSample nextSample() noexcept;

    void reset() noexcept
    {
        sampleIndex_ = 0;
    }

    [[nodiscard]] uint64_t sampleIndex() const noexcept
    {
        return sampleIndex_;
    }

private:
    // Публикация настроек не должна скрыто захватывать мьютекс в обработчике звука.
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<uint64_t> requestedParameters_{(uint64_t{1900} << 32) | 250};
    unsigned startHz_ = 1900;
    unsigned durationMs_ = 250;
    uint64_t sampleIndex_ = 0;
};

} // пространство имён acoustic_radar
