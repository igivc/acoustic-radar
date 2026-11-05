#pragma once
#include <cstdint>
#include <vector>

namespace acoustic_radar {

struct SyncMarker
{
    uint64_t sampleIndex;
    double correlation;
};

class SyncDetector final
{
public:
    // input — четырёхканальный interleaved Float32.
    // frames — количество аудиокадров, не отсчётов.
    [[nodiscard]] std::vector<SyncMarker> detect(
        const float* input,
        size_t frames
    ) const;
};

} // namespace acoustic_radar
