#pragma once
#include "acoustic_radar/spectrum.hpp"
#include <SDL3/SDL.h>
namespace acoustic_radar {
// Вызывать только из SDL-потока. Объект отрисовки принадлежит main и должен
// пережить этот объект; накопительная текстура принадлежит Spectrogram.
class Spectrogram final {
public:
    explicit Spectrogram(SDL_Renderer* renderer);
    ~Spectrogram();
    Spectrogram(const Spectrogram&) = delete;
    Spectrogram& operator=(const Spectrogram&) = delete;
    void setRange(int lowHz);
    void add(const SpectrumFrame& frame);
    void draw(int durationMs);
private:
    SDL_Renderer* renderer_;
    SDL_Texture* accumulation_ = nullptr;
    int lowHz_ = 1900;
    unsigned cycles_ = 0;
    double correlation_ = 0;
    void clear();
};
}
