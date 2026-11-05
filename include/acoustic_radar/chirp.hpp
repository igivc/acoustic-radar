#pragma once

#include <cstdint>

namespace acoustic_radar {

struct StereoSample
{
    float left;
    float right;
};

class ChirpGenerator final
{
public:
    static constexpr double SampleRate = 48000.0;

    static constexpr double CycleDuration = 0.600;

    // Синхромаркер: левый канал
    static constexpr double SyncStartFrequency = 8000.0;
    static constexpr double SyncEndFrequency = 12000.0;
    static constexpr double SyncDuration = 0.010;

    // Акустическое возбуждение: правый канал
    static constexpr double AcousticStartFrequency = 700.0;
    static constexpr double AcousticEndFrequency = 1500.0;
    static constexpr double AcousticDuration = 0.250;

    static constexpr float SyncAmplitude = 0.1f;
    static constexpr float AcousticAmplitude = 0.5f;

    static constexpr uint64_t CycleSamples =
        static_cast<uint64_t>(SampleRate * CycleDuration);

    static constexpr uint64_t SyncSamples =
        static_cast<uint64_t>(SampleRate * SyncDuration);

    static constexpr uint64_t AcousticSamples =
        static_cast<uint64_t>(SampleRate * AcousticDuration);

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
    uint64_t sampleIndex_ = 0;
};

} // namespace acoustic_radar
