#include "acoustic_radar/spectrum.hpp"
#include "acoustic_radar/chirp.hpp"
#include "acoustic_radar/sync.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int main() {
    using namespace acoustic_radar;
    SpectrumAnalyzer analyzer;
    constexpr size_t marker = 1253;
    std::vector<float> input((marker + ChirpGenerator::CycleSamples)*4);
    const double frequency = 100 * 48000. / SpectrumAnalyzer::FFTSize;
    for (size_t i = marker; i < input.size()/4; ++i)
        input[i*4+1] = .5 * std::sin(2*std::acos(-1.)*frequency*(i-marker)/48000);
    auto frame = analyzer.analyze(input.data(), input.size()/4, marker, .9);
    require(frame.columns.size() == (ChirpGenerator::CycleSamples - SpectrumAnalyzer::FFTSize) / SpectrumAnalyzer::HopSize + 1, "FFT column count");
    require(std::abs(frame.columns.front().timeSeconds - (SpectrumAnalyzer::FFTSize / 2.)/48000) < 1e-10, "center timestamp");
    for (const auto& column : frame.columns) {
        auto peak = std::max_element(column.decibels.begin(), column.decibels.end())-column.decibels.begin();
        require(peak == 100, "wrong frequency or microphone channel");
        require(std::abs(column.decibels[100] + 6.0206) < .01, "amplitude calibration");
    }
    require(analyzer.analyze(input.data(), input.size()/4-1, marker, 1).columns.empty(), "partial cycle accepted");
    std::fill(input.begin(), input.end(), 0);
    frame = analyzer.analyze(input.data(), input.size()/4, marker, .9);
    require(frame.columns.front().decibels[100] <= -199, "silence floor");
    ChirpGenerator chirp;
    for(size_t i=marker;i<input.size()/4;++i) input[i*4] = chirp.nextSample().left;
    auto markers = SyncDetector{}.detect(input.data(),input.size()/4);
    require(!markers.empty() && markers[0].sampleIndex == marker, "sync alignment");
    std::cout << "FFT frequency, amplitude, timestamps, silence, partial cycles, sync: passed\n";
}
