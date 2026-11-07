#pragma once
#include <cstdint>
#include <vector>

namespace acoustic_radar {

// sampleIndex указывает начало эталона в переданном буфере,
// correlation — нормированная оценка сходства, а не уровень входного сигнала.
struct SyncMarker
{
    uint64_t sampleIndex;
    double correlation;
};

// Пакетный детектор положительно коррелированных маркеров. Потоковый
// поиск и повторный захват после пропусков обеспечивает CycleProcessor.
class SyncDetector final
{
public:
    // input — четырёхканальный Float32 с чередованием отсчётов.
    // frames — количество аудиокадров, не отсчётов.
    [[nodiscard]] std::vector<SyncMarker> detect(
        const float* input,
        size_t frames
    ) const;
};

} // пространство имён acoustic_radar
