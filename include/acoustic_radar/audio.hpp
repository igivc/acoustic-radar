#pragma once

#include <portaudio.h>

#include <atomic>
#include <cstdint>

#include "acoustic_radar/chirp.hpp"
#include "acoustic_radar/ring.hpp"
#include <memory>

namespace acoustic_radar {

// RAII-обёртка PortAudio и источник данных кольцевого буфера.
// start/stop вызываются управляющим потоком, readInput — единственным потоком чтения.
class AudioDevice final
{
public:
    static constexpr double SampleRate = 48000.0;
    static constexpr unsigned long FramesPerBuffer = 512;

    AudioDevice();
    ~AudioDevice();

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    void start();
    void stop();
    bool readInput(InputFrame& frame) noexcept { return inputRing_->pop(frame); }
    void setAcousticParameters(unsigned startHz, unsigned durationMs) noexcept
    {
        chirp_.setAcousticParameters(startHz, durationMs);
    }

    // Счётчики PortAudio считают события обработчика звука; droppedInputFrames —
    // отдельные кадры, отвергнутые нашим кольцевым буфером.
    struct Statistics
    {
        uint64_t frames;
        uint64_t callbacks;
        uint64_t inputOverflows;
        uint64_t outputUnderflows;
        uint64_t droppedInputFrames;
    };

    [[nodiscard]] Statistics statistics() const noexcept;

    static constexpr unsigned InputChannels = 4;
    static constexpr unsigned OutputChannels = 2;

private:
    static int callback(
        const void* input,
        void* output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData
    ) noexcept;

    PaStream* stream_ = nullptr;

    std::atomic<uint64_t> frames_{0};
    std::atomic<uint64_t> callbacks_{0};
    std::atomic<uint64_t> inputOverflows_{0};
    std::atomic<uint64_t> outputUnderflows_{0};
    ChirpGenerator chirp_;
    // Ёмкость ~2,73 с при 48 кГц; в динамической памяти, чтобы не занимать большой стек.
    std::unique_ptr<SpscRing<InputFrame, 131072>> inputRing_;
    std::atomic<uint64_t> droppedInputFrames_{0};


};

} // пространство имён acoustic_radar
