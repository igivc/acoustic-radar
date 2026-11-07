#include "acoustic_radar/sync.hpp"
#include "acoustic_radar/chirp.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace acoustic_radar {

namespace {

constexpr size_t InputChannels = 4;
constexpr size_t SyncChannel = 0;

constexpr size_t MarkerSamples =
    ChirpGenerator::SyncSamples;

constexpr size_t CycleSamples =
    ChirpGenerator::CycleSamples;

constexpr double DetectionThreshold = 0.75;

// Максимальное отклонение от ожидаемой
// позиции последующих маркеров.
constexpr size_t SearchRadius = 512;

struct Peak
{
    size_t position;
    double correlation;
};

// Генерируем эталон тем же кодом, что и реальный выход: частоты,
// длительность и окно синхромаркера не должны расходиться.
std::vector<float> makeReference()
{
    ChirpGenerator generator;

    std::vector<float> reference;
    reference.reserve(MarkerSamples);

    for (size_t i = 0; i < MarkerSamples; ++i)
    {
        reference.push_back(
            generator.nextSample().left
        );
    }

    return reference;
}

std::optional<Peak> findPeak(
    const float* input,
    size_t frames,
    const std::vector<float>& reference,
    size_t first,
    size_t last)
{
    if (frames < MarkerSamples)
        return std::nullopt;

    // Последняя допустимая позиция гарантирует полное окно корреляции.
    const size_t maximumStart = frames - MarkerSamples;

    first = std::min(first, maximumStart);
    last = std::min(last, maximumStart);

    if (first > last)
        return std::nullopt;

    double referenceEnergy = 0.0;

    for (float value : reference)
        referenceEnergy += double(value) * value;

    if (referenceEnergy == 0.0)
        return std::nullopt;

    Peak best{0, -std::numeric_limits<double>::infinity()};

    // Прямой перебор позиций; вычислительно дорогой участок,
    // поэтому вызывается только в обработчике, а не в обработчике звука.
    for (size_t position = first; position <= last; ++position)
    {
        double dot = 0.0;
        double signalEnergy = 0.0;

        for (size_t j = 0; j < MarkerSamples; ++j)
        {
            const double sample =
                input[(position + j) * InputChannels + SyncChannel];

            const double expected = reference[j];

            dot += sample * expected;
            signalEnergy += sample * sample;
        }

        if (signalEnergy < 1e-12)
            continue;

        // Нормировка энергиями делает оценку нечувствительной к усилению.
        // Знак сохраняется: инвертированный маркер этот детектор не принимает.
        const double correlation =
            dot / std::sqrt(signalEnergy * referenceEnergy);

        if (correlation > best.correlation)
        {
            best = {
                position,
                correlation
            };
        }
    }

    if (best.correlation < DetectionThreshold)
        return std::nullopt;

    return best;
}

} // анонимное пространство имён

std::vector<SyncMarker> SyncDetector::detect(
    const float* input,
    size_t frames) const
{
    std::vector<SyncMarker> markers;

    if (!input || frames < MarkerSamples)
        return markers;

    const auto reference = makeReference();

    // Возвращается наиболее коррелированный маркер в поисковом диапазоне,
    // он не обязательно хронологически самый ранний.
    // Первый маркер ищем на протяжении
    // одного полного периода.
    const auto first = findPeak(
        input,
        frames,
        reference,
        0,
        CycleSamples - 1
    );

    if (!first)
        return markers;

    markers.push_back({
        first->position,
        first->correlation
    });

    // Остальные маркеры должны повторяться
    // через CycleSamples отсчётов.
    for (size_t cycle = 1; ; ++cycle)
    {
        const size_t expected =
            first->position + cycle * CycleSamples;

        if (expected >= frames ||
            frames - expected < MarkerSamples)
            break;

        const size_t begin =
            expected > SearchRadius
                ? expected - SearchRadius
                : 0;

        const size_t end = expected + SearchRadius;

        const auto peak = findPeak(
            input,
            frames,
            reference,
            begin,
            end
        );

        if (peak)
        {
            markers.push_back({
                peak->position,
                peak->correlation
            });
        }
    }

    return markers;
}

} // пространство имён acoustic_radar
