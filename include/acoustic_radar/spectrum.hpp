#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <fftw3.h>

namespace acoustic_radar {
inline constexpr size_t SpectrumFFTSize = 4096;
inline constexpr size_t SpectrumHopSize = SpectrumFFTSize / 4;
// Один столбец STFT: время центра окна относительно маркера и амплитуды
// бинов от нуля до частоты Найквиста в dBFS; частота бина b = b * SampleRate / FFTSize.
struct SpectrumColumn {
    double timeSeconds;
    std::array<float, SpectrumFFTSize / 2 + 1> decibels;
};
// Завершённый цикл, готовый к передаче в интерфейс; аудиобуфер ему больше не нужен.
struct SpectrumFrame {
    uint64_t markerSample;
    double correlation;
    std::vector<SpectrumColumn> columns;
};
// Владеет FFTW-планом и его рабочими массивами. Не потокобезопасен:
// каждый вычисляющий поток создаёт собственный экземпляр.
class SpectrumAnalyzer final {
public:
    static constexpr size_t FFTSize = SpectrumFFTSize;
    static constexpr size_t HopSize = SpectrumHopSize;
    SpectrumAnalyzer();
    ~SpectrumAnalyzer();
    SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
    SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;
    SpectrumFrame analyze(const float* interleaved, size_t frames,
                          uint64_t markerSample, double correlation);
private:
    std::array<double, FFTSize> input_{};
    std::array<double, FFTSize> window_{};
    fftw_complex output_[FFTSize / 2 + 1]{};
    fftw_plan plan_ = nullptr;
    double windowSum_ = 0;
};
}
