#pragma once

#include <portaudio.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "acoustic_radar/chirp.hpp"
#include "acoustic_radar/sync.hpp"

namespace acoustic_radar {

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

    struct Statistics
    {
        uint64_t frames;
        uint64_t callbacks;
        uint64_t inputOverflows;
        uint64_t outputUnderflows;
    };

    [[nodiscard]] Statistics statistics() const noexcept;

    static constexpr unsigned InputChannels = 4;
    static constexpr unsigned OutputChannels = 2;

    static constexpr unsigned RecordingSeconds = 5;

    static constexpr uint64_t RecordingCapacity =
        static_cast<uint64_t>(SampleRate) * RecordingSeconds;

    // Сохранять только после остановки аудиопотока.
    void saveRecording(const std::string& filename) const;

    [[nodiscard]] uint64_t recordedFrames() const noexcept;
    [[nodiscard]] std::vector<SyncMarker> findRecordedSyncMarkers() const;
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

    // Буфер выделяется заранее, до запуска PortAudio.
    std::vector<float> recordingBuffer_;

    // Количество записанных аудиокадров.
    std::atomic<uint64_t> recordedFrames_{0};
};

} // namespace acoustic_rada
