#include "acoustic_radar/chirp.hpp"

#include <cmath>

namespace acoustic_radar {

namespace {

constexpr double pi = 3.14159265358979323846;

// Мгновенная фаза линейного chirp.
double chirpPhase(
    double t,
    double f0,
    double f1,
    double duration) noexcept
{
    const double sweepRate = (f1 - f0) / duration;

    return 2.0 * pi *
        (f0 * t + 0.5 * sweepRate * t * t);
}

// Окно Ханна для короткого синхромаркера.
double hannWindow(
    uint64_t position,
    uint64_t count) noexcept
{
    if (count <= 1)
        return 0.0;

    return 0.5 * (
        1.0 - std::cos(
            2.0 * pi *
            static_cast<double>(position) /
            static_cast<double>(count - 1)
        )
    );
}

// Огибающая акустического сигнала:
// 10 мс нарастания, 10 мс спада.
double acousticEnvelope(double t) noexcept
{
    constexpr double duration =
        ChirpGenerator::AcousticDuration;

    constexpr double fade = 0.010;

    if (t < fade)
    {
        return 0.5 *
            (1.0 - std::cos(pi * t / fade));
    }

    if (t > duration - fade)
    {
        return 0.5 *
            (1.0 - std::cos(
                pi * (duration - t) / fade));
    }

    return 1.0;
}

} // namespace

StereoSample ChirpGenerator::nextSample() noexcept
{
    const uint64_t position =
        sampleIndex_++ % CycleSamples;

    const double t =
        static_cast<double>(position) / SampleRate;

    StereoSample result{0.0f, 0.0f};

    // LEFT: синхромаркер 8–12 кГц.
    if (position < SyncSamples)
    {
        const double phase = chirpPhase(
            t,
            SyncStartFrequency,
            SyncEndFrequency,
            SyncDuration
        );

        const double window = hannWindow(
            position,
            SyncSamples
        );

        result.left = static_cast<float>(
            SyncAmplitude *
            window *
            std::sin(phase)
        );
    }

    // RIGHT: акустический chirp 700–1500 Гц.
    if (position < AcousticSamples)
    {
        const double phase = chirpPhase(
            t,
            AcousticStartFrequency,
            AcousticEndFrequency,
            AcousticDuration
        );

        const double envelope = acousticEnvelope(t);

        result.right = static_cast<float>(
            AcousticAmplitude *
            envelope *
            std::sin(phase)
        );
    }

    return result;
}

} // namespace acoustic_radar
