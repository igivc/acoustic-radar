#include "acoustic_radar/spectrum.hpp"
#include "acoustic_radar/chirp.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace acoustic_radar {
SpectrumAnalyzer::SpectrumAnalyzer() {
    // Симметричное окно Ханна подавляет утечку между частотными бинами.
    // Сумма окна нужна для амплитудной нормировки, а не нормировки мощности.
    for (size_t i = 0; i < FFTSize; ++i) {
        window_[i] = .5 - .5 * std::cos(2 * std::acos(-1.) * i / (FFTSize - 1));
        windowSum_ += window_[i];
    }
    // План привязан к адресам input_/output_ и переиспользуется для всех окон.
    // Один экземпляр анализатора обслуживается одним потоком.
    plan_ = fftw_plan_dft_r2c_1d(FFTSize, input_.data(), output_, FFTW_ESTIMATE);
    if (!plan_) throw std::runtime_error("Cannot create FFTW plan");
}
SpectrumAnalyzer::~SpectrumAnalyzer() { fftw_destroy_plan(plan_); }
SpectrumFrame SpectrumAnalyzer::analyze(const float* data, size_t frames,
                                        uint64_t marker, double correlation) {
    SpectrumFrame result{marker, correlation, {}};
    // Неполный цикл не дополняем нулями, чтобы не рисовать искусственный спад.
    if (!data || marker > frames || frames - marker < ChirpGenerator::CycleSamples)
        return result;
    for (size_t offset = 0; offset + FFTSize <= ChirpGenerator::CycleSamples; offset += HopSize) {
        // Четыре входных канала с чередованием отсчётов; микрофон читается из канала с индексом 1.
        for (size_t j = 0; j < FFTSize; ++j)
            input_[j] = data[(marker + offset + j) * 4 + 1] * window_[j];
        fftw_execute(plan_);
        SpectrumColumn column{};
        // X соответствует центру измерительного окна, а не его началу.
        column.timeSeconds = (offset + FFTSize / 2.) / ChirpGenerator::SampleRate;
        for (size_t b = 0; b <= FFTSize / 2; ++b) {
            // Односторонний спектр: удваиваем положительные частоты, кроме
            // постоянной составляющей и частоты Найквиста. Синус амплитуды 1 в центре бина даёт 0 dBFS.
            const double scale = (b == 0 || b == FFTSize / 2) ? 1 : 2;
            const double amplitude = scale * std::hypot(output_[b][0], output_[b][1]) / windowSum_;
            // Нижний предел -200 dBFS исключает log(0) и бесконечности.
            column.decibels[b] = static_cast<float>(20 * std::log10(std::max(amplitude, 1e-10)));
        }
        result.columns.push_back(column);
    }
    return result;
}
}
