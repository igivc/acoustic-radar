#pragma once

#include <portaudio.h>

#include <atomic>
#include <cstdint>

#include "acoustic_radar/chirp.hpp"
#include "acoustic_radar/ring.hpp"
#include <memory>

namespace acoustic_radar {

// Открывает PortAudio при создании и закрывает при уничтожении (принцип RAII).
// Передаёт входные кадры в кольцевой буфер.
// start/stop вызываются основным потоком; readInput — только рабочим потоком.
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
    // число входных кадров, которые не поместились в кольцевой буфер.
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
    // Вмещает около 2,73 с звука при 48 кГц. Размещаем в динамической памяти,
    // чтобы большой массив не занимал стек вызвавшего потока.
    std::unique_ptr<SpscRing<InputFrame, 131072>> inputRing_;
    std::atomic<uint64_t> droppedInputFrames_{0};


};

} // пространство имён acoustic_radar
