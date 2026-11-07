#include "acoustic_radar/audio.hpp"
#include "acoustic_radar/spectrum.hpp"
#include "acoustic_radar/visualization.hpp"
#include "acoustic_radar/processing.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace {
// Читаем прежний диагностический формат WAV: фиксированные блоки fmt/data,
// Float32 с младшим байтом первым, максимум 60 с. Другие варианты WAV не поддерживаются.
std::vector<float> readRecording(const char* path) {
    std::ifstream file(path, std::ios::binary);
    unsigned char header[44]{};
    file.read(reinterpret_cast<char*>(header), 44);
    auto u16 = [&](int i) { return unsigned(header[i]) | unsigned(header[i+1]) << 8; };
    auto u32 = [&](int i) { return unsigned(header[i]) | unsigned(header[i+1]) << 8 |
                                  unsigned(header[i+2]) << 16 | unsigned(header[i+3]) << 24; };
    auto tag = [&](int i, const char* text) { return std::equal(header+i, header+i+4, text); };
    if (!file || !tag(0,"RIFF") || !tag(8,"WAVE") || !tag(12,"fmt ") ||
        u32(16)!=16 || u16(20)!=3 || u16(22)!=4 || u32(24)!=48000 ||
        u16(32)!=16 || u16(34)!=32 || !tag(36,"data") || u32(40)%16 ||
        u32(40)>48000*16*60)
        throw std::runtime_error("Replay requires a 4-channel 48 kHz Float32 diagnostic WAV (max 60 s)");
    std::vector<float> data(u32(40)/4);
    file.read(reinterpret_cast<char*>(data.data()), u32(40));
    if (!file) throw std::runtime_error("Truncated WAV recording");
    return data;
}
// Вычисляем спектры всего WAV-файла. Звук с устройства обрабатывает ProcessingWorker.
std::vector<acoustic_radar::SpectrumFrame> process(
    std::vector<float> samples, std::optional<uint64_t> lastMarker) {
    acoustic_radar::SyncDetector detector;
    acoustic_radar::SpectrumAnalyzer analyzer;
    std::vector<acoustic_radar::SpectrumFrame> result;
    const auto frames = samples.size()/4;
    for (auto marker : detector.detect(samples.data(), frames)) {
        if (lastMarker && marker.sampleIndex <= *lastMarker) continue;
        auto spectrum = analyzer.analyze(samples.data(), frames, marker.sampleIndex, marker.correlation);
        if (!spectrum.columns.empty()) result.push_back(std::move(spectrum));
    }
    return result;
}
}
int main(int argc, char** argv) {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--replay")) {
        std::cerr << "Usage: acoustic_radar [--replay recording.wav]\n";
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Acoustic Radar",1280,720,SDL_WINDOW_RESIZABLE,&window,&renderer)) {
        std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 1;
    }
    int result = 0;
    try {
        const bool replay = argc == 3;
        std::vector<float> replayData;
        std::unique_ptr<acoustic_radar::AudioDevice> audio;
        if (replay) replayData = readRecording(argv[2]);
        else audio = std::make_unique<acoustic_radar::AudioDevice>();
        acoustic_radar::Spectrogram view(renderer);
        using Generator = acoustic_radar::ChirpGenerator;
        int startHz = 1900, durationMs = 250;
        // Генератор применяет настройки на границе цикла; ось интерфейса меняется сразу.
        // В режиме просмотра записи частота меняет лишь отображение, исходный звук не изменяется.
        const auto update = [&] {
            if (audio) audio->setAcousticParameters(startHz, durationMs);
            view.setRange(startHz);
            const std::string title = "Acoustic Radar | " + std::to_string(startHz) + "-" +
                std::to_string(startHz+1000) + " Hz | " + std::to_string(durationMs) +
                " ms | " + (replay ? "REPLAY (original excitation unchanged)" : "LIVE: continuous");
            SDL_SetWindowTitle(window, title.c_str());
        };
        update();
        // Рабочий поток создаётся до старта PortAudio и уничтожается раньше AudioDevice.
        std::unique_ptr<acoustic_radar::ProcessingWorker> worker;
        if (audio) {
            worker = std::make_unique<acoustic_radar::ProcessingWorker>(*audio);
            audio->start();
        }
        std::future<std::vector<acoustic_radar::SpectrumFrame>> pending;
        std::optional<uint64_t> lastMarker;
        std::vector<acoustic_radar::SpectrumFrame> ready;
        size_t nextFrame = 0;
        bool replaySubmitted = false;
        auto nextAnalysis = Clock::now();
        auto nextDraw = Clock::now();
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type != SDL_EVENT_KEY_DOWN) continue;
                switch (event.key.key) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_UP: startHz += 100; break;
                case SDLK_DOWN: startHz -= 100; break;
                case SDLK_LEFT: if (!replay) durationMs -= 10; break;
                case SDLK_RIGHT: if (!replay) durationMs += 10; break;
                default: continue;
                }
                startHz = std::clamp(startHz, int(Generator::MinStartHz), int(Generator::MaxStartHz));
                durationMs = std::clamp(durationMs, int(Generator::MinDurationMs), int(Generator::MaxDurationMs));
                update();
            }
            // Неблокирующая проверка фонового анализа записи сохраняет отзывчивость SDL.
            if (pending.valid() && pending.wait_for(0ms) == std::future_status::ready) {
                auto frames = pending.get();
                if (!frames.empty()) lastMarker = frames.back().markerSample;
                if (replay) ready = std::move(frames);
                else for (const auto& frame : frames) view.add(frame);
            }
            // SDL-текстура меняется исключительно в основном потоке.
            if (worker) {
                for (const auto& frame : worker->take()) view.add(frame);
            }
            const auto now = Clock::now();
            if (replay && !pending.valid() && !replaySubmitted && now >= nextAnalysis) {
                replaySubmitted = true;
                pending = std::async(std::launch::async, process, std::move(replayData), lastMarker);
            }
            // Пакет уже вычислен, но показываем по одному циклу за период.
            if (replay && nextFrame < ready.size() && now >= nextDraw) {
                view.add(ready[nextFrame++]); nextDraw = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(Generator::CycleDuration));
            }
            view.draw(durationMs);
            SDL_Delay(16);

        }
        if (audio) {
            // Сначала останавливаем поступление звука, затем ждём рабочий поток.
            audio->stop();
            std::cout << "Dropped display cycles: " << worker->droppedResults() << '\n';
            worker.reset();
            auto stats = audio->statistics();
            std::cout << "Processed " << stats.frames << " frames; input overflows: "
                << stats.inputOverflows << "; output underflows: " << stats.outputUnderflows << "; dropped ring frames: " << stats.droppedInputFrames << '\n';
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return result;
}
