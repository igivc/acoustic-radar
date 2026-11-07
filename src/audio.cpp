#include "acoustic_radar/audio.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace acoustic_radar {

namespace {

void check(PaError error)
{
    if (error != paNoError)
        throw std::runtime_error(Pa_GetErrorText(error));
}

// Выбираем физическое ALSA-устройство с нужным числом каналов,
// а не системное устройство по умолчанию, которое может менять маршрутизацию.
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

} // анонимное пространство имён

AudioDevice::AudioDevice()
    : inputRing_(std::make_unique<SpscRing<InputFrame, 131072>>())
{
    check(Pa_Initialize());

    try
    {
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

// Обработчик PortAudio копирует вход в кольцо и генерирует выходной звук.
// При передаче данных он не ждёт рабочий поток и не захватывает мьютексы.
// FFT, корреляция, SDL и файловые операции выполняются в других потоках.
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

    // Абсолютный индекс растёт даже при отказе кольца. Потребитель увидит
    // пропуск и не склеит участки по разные стороны потерянных кадров.
    const auto firstIndex = self->frames_.load(std::memory_order_relaxed);
    uint64_t dropped = 0;
    for (unsigned long i = 0; i < frameCount; ++i) {
        InputFrame frame{firstIndex + i, {}};
        if (inputSamples) std::copy_n(inputSamples + i * InputChannels, InputChannels, frame.samples.begin());
        if (!self->inputRing_->push(frame)) ++dropped;
    }
    self->droppedInputFrames_.fetch_add(dropped, std::memory_order_relaxed);

    // Два выходных канала с чередованием левого и правого отсчётов.
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

    // Эти счётчики нужны только для статистики. Они не разрешают доступ к данным
    // буфера, поэтому синхронизация через них не нужна: достаточно relaxed.
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

AudioDevice::Statistics
AudioDevice::statistics() const noexcept
{
    return {
        frames_.load(std::memory_order_relaxed),
        callbacks_.load(std::memory_order_relaxed),
        inputOverflows_.load(std::memory_order_relaxed),
        outputUnderflows_.load(std::memory_order_relaxed),
        droppedInputFrames_.load(std::memory_order_relaxed)
    };
}

} // пространство имён acoustic_radar
