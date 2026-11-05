#include "acoustic_radar/audio.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <fstream>
#include <limits>

namespace acoustic_radar {

namespace {

void writeU16(std::ostream& out, uint16_t value)
{
    out.put(static_cast<char>(value & 0xFF));
    out.put(static_cast<char>((value >> 8) & 0xFF));
}

void writeU32(std::ostream& out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        out.put(static_cast<char>(
            (value >> (8 * i)) & 0xFF
        ));
    }
}

void check(PaError error)
{
    if (error != paNoError)
        throw std::runtime_error(Pa_GetErrorText(error));
}

PaDeviceIndex findUR22C()
{
    const int count = Pa_GetDeviceCount();

    if (count < 0)
        check(count);

    for (int i = 0; i < count; ++i)
    {
        const auto* device = Pa_GetDeviceInfo(i);

        if (!device)
            continue;

        const auto* host = Pa_GetHostApiInfo(device->hostApi);

        if (!host || host->type != paALSA)
            continue;

        if (std::string_view(device->name).find("UR22C")
                == std::string_view::npos)
            continue;

        if (device->maxInputChannels >= 4 &&
            device->maxOutputChannels >= 2)
        {
            return i;
        }
    }

    throw std::runtime_error(
        "Steinberg UR22C not found"
    );
}

} // namespace

AudioDevice::AudioDevice()
{
    check(Pa_Initialize());

    try
    {
        recordingBuffer_.resize(
            RecordingCapacity * InputChannels
        );

        const PaDeviceIndex deviceIndex = findUR22C();
        const auto* device = Pa_GetDeviceInfo(deviceIndex);

        PaStreamParameters input{};
        input.device = deviceIndex;
        input.channelCount = InputChannels;
        input.sampleFormat = paFloat32;
        input.suggestedLatency = device->defaultLowInputLatency;

        PaStreamParameters output{};
        output.device = deviceIndex;
        output.channelCount = OutputChannels;
        output.sampleFormat = paFloat32;
        output.suggestedLatency = device->defaultLowOutputLatency;

        check(Pa_OpenStream(
            &stream_,
            &input,
            &output,
            SampleRate,
            FramesPerBuffer,
            paNoFlag,
            &AudioDevice::callback,
            this
        ));
    }
    catch (...)
    {
        Pa_Terminate();
        throw;
    }
}

AudioDevice::~AudioDevice()
{
    if (stream_)
        Pa_CloseStream(stream_);

    Pa_Terminate();
}

void AudioDevice::start()
{
    check(Pa_StartStream(stream_));
}

void AudioDevice::stop()
{
    if (stream_ && Pa_IsStreamActive(stream_) == 1)
        check(Pa_StopStream(stream_));
}

int AudioDevice::callback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags statusFlags,
    void* userData) noexcept
{
    auto* self = static_cast<AudioDevice*>(userData);

    const auto* inputSamples = static_cast<const float*>(input);

    const uint64_t recorded =
        self->recordedFrames_.load(std::memory_order_relaxed);

    if (recorded < RecordingCapacity)
    {
        const uint64_t remaining = RecordingCapacity - recorded;

        const uint64_t framesToCopy =
            std::min<uint64_t>(frameCount, remaining);

        float* destination =
            self->recordingBuffer_.data() +
            recorded * InputChannels;

        const size_t sampleCount =
            framesToCopy * InputChannels;

        if (inputSamples)
        {
            std::copy_n(
                inputSamples,
                sampleCount,
                destination
            );
        }
        else
        {
            std::fill_n(
                destination,
                sampleCount,
                0.0f
            );
        }

        self->recordedFrames_.store(
            recorded + framesToCopy,
            std::memory_order_release
        );
    }

    // Два выходных канала, interleaved.
    auto* samples = static_cast<float*>(output);

    for (unsigned long i = 0; i < frameCount; ++i)
    {
        const StereoSample sample =
            self->chirp_.nextSample();

        if (samples)
        {
            samples[i * 2 + 0] = sample.left;
            samples[i * 2 + 1] = sample.right;
        }
    }

    self->frames_.fetch_add(
        frameCount, std::memory_order_relaxed);

    self->callbacks_.fetch_add(
        1, std::memory_order_relaxed);

    if (statusFlags & paInputOverflow)
    {
        self->inputOverflows_.fetch_add(
            1, std::memory_order_relaxed);
    }

    if (statusFlags & paOutputUnderflow)
    {
        self->outputUnderflows_.fetch_add(
            1, std::memory_order_relaxed);
    }

    return paContinue;
}

void AudioDevice::saveRecording(
    const std::string& filename) const
{
    if (Pa_IsStreamStopped(stream_) != 1)
    {
        throw std::runtime_error(
            "Audio stream must be stopped before saving"
        );
    }

    static_assert(sizeof(float) == 4);

    constexpr uint16_t channels = InputChannels;
    constexpr uint16_t bitsPerSample = 32;
    constexpr uint16_t format = 3; // IEEE floating point

    constexpr uint32_t sampleRate =
        static_cast<uint32_t>(SampleRate);

    constexpr uint16_t blockAlign =
        channels * sizeof(float);

    constexpr uint32_t byteRate =
        sampleRate * blockAlign;

    const uint64_t frames =
        recordedFrames_.load(std::memory_order_acquire);

    const uint64_t dataBytes64 = frames * blockAlign;

    if (dataBytes64 > std::numeric_limits<uint32_t>::max() - 36)
    {
        throw std::runtime_error("WAV recording is too large");
    }

    const uint32_t dataBytes =
        static_cast<uint32_t>(dataBytes64);

    std::ofstream out(filename, std::ios::binary);

    if (!out)
    {
        throw std::runtime_error(
            "Cannot open recording file: " + filename
        );
    }

    // RIFF header
    out.write("RIFF", 4);
    writeU32(out, 36 + dataBytes);
    out.write("WAVE", 4);

    // Format chunk
    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, format);
    writeU16(out, channels);
    writeU32(out, sampleRate);
    writeU32(out, byteRate);
    writeU16(out, blockAlign);
    writeU16(out, bitsPerSample);

    // Data chunk
    out.write("data", 4);
    writeU32(out, dataBytes);

    out.write(
        reinterpret_cast<const char*>(recordingBuffer_.data()),
        static_cast<std::streamsize>(dataBytes)
    );

    if (!out)
    {
        throw std::runtime_error(
            "Failed to write recording: " + filename
        );
    }
}

uint64_t AudioDevice::recordedFrames() const noexcept
{
    return recordedFrames_.load(std::memory_order_acquire);
}

std::vector<SyncMarker>
AudioDevice::findRecordedSyncMarkers() const
{
    if (Pa_IsStreamStopped(stream_) != 1)
    {
        throw std::runtime_error(
            "Stop audio stream before sync detection"
        );
    }

    SyncDetector detector;

    const size_t frames = static_cast<size_t>(
        recordedFrames_.load(std::memory_order_acquire)
    );

    return detector.detect(
        recordingBuffer_.data(),
        frames
    );
}

AudioDevice::Statistics
AudioDevice::statistics() const noexcept
{
    return {
        frames_.load(std::memory_order_relaxed),
        callbacks_.load(std::memory_order_relaxed),
        inputOverflows_.load(std::memory_order_relaxed),
        outputUnderflows_.load(std::memory_order_relaxed)
    };
}

} // namespace acoustic_radar
