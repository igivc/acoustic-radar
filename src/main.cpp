#include "acoustic_radar/audio.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <exception>
#include <iostream>

int main()
{
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    if (!SDL_CreateWindowAndRenderer(
            "Acoustic Radar",
            1280,
            720,
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer))
    {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    int result = 0;

    try
    {
        acoustic_radar::AudioDevice audio;

        audio.start();

        std::cout << "Audio stream started\n";

        const auto start = Clock::now();
        bool running = true;

        while (running && Clock::now() - start < 30s)
        {
            SDL_Event event;

            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                    running = false;

                if (event.type == SDL_EVENT_KEY_DOWN &&
                    event.key.key == SDLK_ESCAPE)
                    running = false;
            }

            SDL_SetRenderDrawColor(
                renderer, 10, 15, 25, 255);

            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);

            SDL_Delay(10);
        }

        audio.stop();

        audio.saveRecording("capture.wav");
        const auto markers = audio.findRecordedSyncMarkers();

        std::cout
            << "Recorded frames: "
            << audio.recordedFrames()
            << '\n';
        const auto stats = audio.statistics();

        std::cout << "Sync markers found: "
                << markers.size() << '\n';

        for (size_t i = 0; i < markers.size(); ++i)
        {
            const auto& marker = markers[i];

            std::cout
                << "Marker " << i
                << ": sample=" << marker.sampleIndex
                << ", time="
                << marker.sampleIndex / acoustic_radar::AudioDevice::SampleRate
                << " s"
                << ", correlation=" << marker.correlation;

            if (i > 0)
            {
                std::cout
                    << ", interval="
                    << marker.sampleIndex
                    - markers[i - 1].sampleIndex;
            }

            std::cout << '\n';
        }

        std::cout
            << "\nAudio statistics:\n"
            << "  Frames:           " << stats.frames << '\n'
            << "  Callbacks:        " << stats.callbacks << '\n'
            << "  Input overflows:  " << stats.inputOverflows << '\n'
            << "  Output underflows:" << stats.outputUnderflows << '\n'
            << "  Audio duration:   "
            << stats.frames /
               acoustic_radar::AudioDevice::SampleRate
            << " s\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Audio error: " << e.what() << '\n';
        result = 1;
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return result;
}
