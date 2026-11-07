#include "acoustic_radar/chirp.hpp"

#include <algorithm>
#include <cmath>

namespace acoustic_radar {

namespace {

constexpr double pi = 3.14159265358979323846;

// Интеграл мгновенной частоты f(t)=f0+(f1-f0)*t/duration.
// Вычисление фазы из времени не накапливает ошибку суммирования приращений.
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
double acousticEnvelope(double t, double duration) noexcept
{
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

} // анонимное пространство имён

void ChirpGenerator::setAcousticParameters(unsigned startHz, unsigned durationMs) noexcept
{
    startHz = std::clamp(startHz, MinStartHz, MaxStartHz);
    durationMs = std::clamp(durationMs, MinDurationMs, MaxDurationMs);
    // Одна атомарная пара не допускает смешивания новой частоты со старой
    // длительностью. Порядка memory_order_relaxed достаточно: других данных через неё не публикуем.
    requestedParameters_.store(
        (uint64_t{startHz} << 32) | durationMs, std::memory_order_relaxed);
}

StereoSample ChirpGenerator::nextSample() noexcept
{
    const uint64_t position =
        sampleIndex_++ % CycleSamples;

    // Настройки фиксируются на весь цикл. Интерфейс может менять запрос в любой
    // момент, но фаза текущего возбуждения при этом не разрывается.
    if (position == 0)
    {
        const auto parameters = requestedParameters_.load(std::memory_order_relaxed);
        startHz_ = static_cast<unsigned>(parameters >> 32);
        durationMs_ = static_cast<unsigned>(parameters & 0xffffffffULL);
    }

    const double duration = durationMs_ / 1000.0;
    // При 48 кГц в каждой миллисекунде ровно 48 отсчётов.
    const uint64_t acousticSamples = uint64_t{durationMs_} * 48;
    const double t =
        static_cast<double>(position) / SampleRate;

    // После окончания возбуждения оба канала молчат до следующего цикла.
    StereoSample result{0.0f, 0.0f};

    // Левый канал: синхромаркер 8–12 кГц.
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

    // Правый канал: акустический ЛЧМ-сигнал с настраиваемыми параметрами.
    if (position < acousticSamples)
    {
        const double phase = chirpPhase(
            t,
            startHz_,
            startHz_ + BandwidthHz,
            duration
        );

        const double envelope = acousticEnvelope(t, duration);

        result.right = static_cast<float>(
            AcousticAmplitude *
            envelope *
            std::sin(phase)
        );
    }

    return result;
}

} // пространство имён acoustic_radar
