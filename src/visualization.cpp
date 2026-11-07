#include "acoustic_radar/visualization.hpp"
#include "acoustic_radar/chirp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace acoustic_radar {
namespace {
constexpr int Width = 1100, Height = 540;
constexpr double CycleSeconds = ChirpGenerator::CycleDuration;
constexpr int CycleMs = static_cast<int>(CycleSeconds * 1000);
void check(bool ok) { if (!ok) throw std::runtime_error(SDL_GetError()); }
}
Spectrogram::Spectrogram(SDL_Renderer* renderer): renderer_(renderer) {
    // Задний буфер окна не хранит историю между кадрами. Отдельная целевая
    // текстура содержит накопление и масштабируется при изменении размера окна.
    accumulation_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, Width, Height);
    if (!accumulation_) throw std::runtime_error(SDL_GetError());
    try { clear(); } catch (...) { SDL_DestroyTexture(accumulation_); throw; }
}
Spectrogram::~Spectrogram() { SDL_DestroyTexture(accumulation_); }
void Spectrogram::clear() {
    check(SDL_SetRenderTarget(renderer_, accumulation_));
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    check(SDL_RenderClear(renderer_));
    check(SDL_SetRenderTarget(renderer_, nullptr));
    cycles_ = 0;
}
void Spectrogram::setRange(int lowHz) {
    // Старые следы в новых координатах частоты потеряли бы физический смысл.
    if (lowHz != lowHz_) { lowHz_ = lowHz; clear(); }
}
void Spectrogram::add(const SpectrumFrame& frame) {
    if (frame.columns.empty()) return;
    check(SDL_SetRenderTarget(renderer_, accumulation_));
    check(SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND));
    // Один шаг затухания на принятый цикл, независимо от частоты SDL.draw().
    // Чёрный слой alpha=102/255 оставляет 60% предыдущего изображения.
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 102); // Уменьшаем яркость предыдущих циклов на 40%.
    check(SDL_RenderFillRect(renderer_, nullptr));
    check(SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND));
    // Геометрия ячейки соответствует шагу STFT и ширине частотного бина,
    // а не полной длительности перекрывающегося окна FFT.
    const float cellWidth = Width * (float(SpectrumAnalyzer::HopSize) / ChirpGenerator::SampleRate) / CycleSeconds;
    const float cellHeight = Height * (ChirpGenerator::SampleRate / SpectrumAnalyzer::FFTSize) / 1000.f;
    for (const auto& column : frame.columns) {
        for (size_t bin = 1; bin < column.decibels.size(); ++bin) {
            const float frequency = bin * ChirpGenerator::SampleRate / SpectrumAnalyzer::FFTSize;
            if (frequency < lowHz_ || frequency > lowHz_ + 1000) continue;
            const float heat = (frequency - lowHz_) / 1000.f;
            // Красный → оранжевый → жёлтый → белый; уровень управляет только яркостью.
            const float brightness = std::clamp((column.decibels[bin] + 80.f) / 65.f, 0.f, 1.f);
            if (brightness <= 0) continue;
            const float green = std::min(1.f, heat * 2);
            const float blue = std::max(0.f, heat * 2 - 1);
            // Уровень задаёт прозрачность, цвет — частоту. Обычное смешивание по прозрачности
            // не выбеливает нижние частоты при накоплении, как аддитивное смешивание.
            SDL_SetRenderDrawColor(renderer_, 255,
                Uint8(255 * green), Uint8(255 * blue), Uint8(255 * brightness));
            const SDL_FRect cell{
                float(column.timeSeconds / CycleSeconds * Width) - cellWidth / 2,
                Height * (1 - heat) - cellHeight / 2, cellWidth + .5f, cellHeight + .5f};
            check(SDL_RenderFillRect(renderer_, &cell));
        }
    }
    check(SDL_SetRenderTarget(renderer_, nullptr));
    check(SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE));
    ++cycles_;
    correlation_ = frame.correlation;
}
// Только вывод накопления и подписей: эта функция не изменяет историю.
void Spectrogram::draw(int durationMs) {
    int w, h;
    check(SDL_GetRenderOutputSize(renderer_, &w, &h));
    SDL_SetRenderDrawColor(renderer_, 10, 15, 25, 255);
    check(SDL_RenderClear(renderer_));
    const SDL_FRect plot{80, 65, float(std::max(1, w - 115)), float(std::max(1, h - 145))};
    check(SDL_RenderTexture(renderer_, accumulation_, nullptr, &plot));
    SDL_SetRenderDrawColor(renderer_, 100, 110, 125, 255);
    for (int t = 0; t <= CycleMs; t += 100) {
        float x = plot.x + plot.w * t / CycleMs;
        SDL_RenderLine(renderer_, x, plot.y + plot.h, x, plot.y + plot.h + 5);
        char text[32]; std::snprintf(text, sizeof(text), "%d ms", t);
        SDL_RenderDebugText(renderer_, x - 15, plot.y + plot.h + 12, text);
    }
    for (int f = 0; f <= 1000; f += 200) {
        float y = plot.y + plot.h * (1 - f / 1000.f);
        char text[32]; std::snprintf(text, sizeof(text), "%d Hz", lowHz_ + f);
        SDL_RenderDebugText(renderer_, 8, y - 4, text);
    }
    // Линия отражает запрошенную в интерфейсе длительность, не измеренную границу эха.
    const float excitationEnd = plot.x + plot.w * durationMs / CycleMs;
    SDL_RenderLine(renderer_, excitationEnd, plot.y, excitationEnd, plot.y + plot.h);
    char status[192];
    std::snprintf(status, sizeof(status), "INPUT 2 | FFT %zu / hop %zu | cycles: %u | sync: %.3f | brightness: -80..-15 dBFS", SpectrumAnalyzer::FFTSize, SpectrumAnalyzer::HopSize, cycles_, correlation_);
    SDL_RenderDebugText(renderer_, 80, 20, status);
    SDL_RenderDebugText(renderer_, 80, float(h - 35), "UP/DOWN: frequency | LEFT/RIGHT: duration | ESC: exit | vertical line: excitation end");
    check(SDL_RenderPresent(renderer_));
}
}
