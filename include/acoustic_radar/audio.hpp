/**
 * @file audio.hpp
 * @brief Аудиоустройство PortAudio, входной буфер и статистика обмена.
 */
#pragma once

#include <portaudio.h>

#include <atomic>
#include <cstdint>

#include "acoustic_radar/chirp.hpp"
#include "acoustic_radar/ring.hpp"
#include <memory>

namespace acoustic_radar {

/**
 * @brief Открывает PortAudio при создании и закрывает при уничтожении (принцип RAII).
 *
 * Передаёт входные кадры в кольцевой буфер.
 * start/stop вызываются основным потоком; readInput - только рабочим потоком.
 */
class AudioDevice final
{
public:
    /**
     * @brief Частота дискретизации в герцах.
     */
    static constexpr double SampleRate = 48000.0;
    /**
     * @brief Запрошенный размер блока callback в аудиокадрах.
     */
    static constexpr unsigned long FramesPerBuffer = 512;

    /**
     * @brief Инициализирует PortAudio, открывает устройство и выделяет кольцевой буфер.
     * @throws std::runtime_error Не удалось найти или открыть звуковое устройство.
     * @note Поток запускается отдельно методом start().
     */
    AudioDevice();
    /**
     * @brief Закрывает поток и завершает работу PortAudio.
     * @pre Рабочий поток обработки больше не обращается к устройству.
     */
    ~AudioDevice();

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    /**
     * @brief Запускает аудиопоток.
     * @throws std::runtime_error PortAudio не смог запустить поток.
     * @note Вызывается из основного потока.
     */
    void start();
    /**
     * @brief Останавливает активный аудиопоток.
     * @throws std::runtime_error PortAudio не смог остановить поток.
     * @note Повторный вызов для остановленного потока ничего не меняет.
     */
    void stop();
    /**
     * @brief Извлекает самый старый входной кадр без ожидания.
     * @param[out] frame Полученный кадр; при пустом кольце не меняется.
     * @return true, если кадр прочитан; false, если кольцо пусто.
     * @note Вызывается только одним рабочим потоком.
     */
    bool readInput(InputFrame& frame) noexcept { return inputRing_->pop(frame); }
    /**
     * @brief Передаёт генератору запрос на перестройку возбуждения.
     * @param startHz Нижняя частота в герцах; полоса имеет ширину 1000 Гц.
     * @param durationMs Длительность возбуждения в миллисекундах.
     * @see ChirpGenerator::setAcousticParameters()
     */
    void setAcousticParameters(unsigned startHz, unsigned durationMs) noexcept
    {
        chirp_.setAcousticParameters(startHz, durationMs);
    }

    /**
     * @brief Счётчики PortAudio считают события обработчика звука; droppedInputFrames - число входных
     * кадров, которые не поместились в кольцевой буфер.
     */
    struct Statistics
    {
        /**
         * @brief Общее число входных аудиокадров, включая отброшенные кольцом.
         */
        uint64_t frames;
        /**
         * @brief Число вызовов callback.
         */
        uint64_t callbacks;
        /**
         * @brief Число callback с флагом переполнения входа PortAudio.
         */
        uint64_t inputOverflows;
        /**
         * @brief Число callback с флагом нехватки выходных данных PortAudio.
         */
        uint64_t outputUnderflows;
        /**
         * @brief Число входных кадров, не поместившихся в кольцо.
         */
        uint64_t droppedInputFrames;
    };

    /**
     * @brief Читает атомарные счётчики обмена.
     * @return Статистика на момент последовательного чтения отдельных счётчиков.
     * @note Во время работы значения не являются единым синхронным снимком.
     */
    [[nodiscard]] Statistics statistics() const noexcept;

    /**
     * @brief Число входных каналов в одном аудиокадре.
     */
    static constexpr unsigned InputChannels = 4;
    /**
     * @brief Число выходных каналов в одном аудиокадре.
     */
    static constexpr unsigned OutputChannels = 2;

private:
    /**
     * @brief Копирует вход в кольцо и генерирует два выходных канала.
     * @param input Входной блок Float32; nullptr означает отсутствующий вход.
     * @param output Выходной блок Float32 либо nullptr.
     * @param frameCount Число аудиокадров в этом вызове.
     * @param timeInfo Временные метки PortAudio; в реализации не используются.
     * @param statusFlags Флаги overflow и underflow для статистики.
     * @param userData Указатель на AudioDevice, переданный при открытии потока.
     * @return paContinue для продолжения звукового потока.
     * @note Здесь нет FFT, корреляции, мьютексов и файловых операций.
     */
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
    /**
     * @brief Вмещает около 2,73 с звука при 48 кГц.
     *
     * Размещаем в динамической памяти,
     * чтобы большой массив не занимал стек вызвавшего потока.
     */
    std::unique_ptr<SpscRing<InputFrame, 131072>> inputRing_;
    std::atomic<uint64_t> droppedInputFrames_{0};


};

} // пространство имён acoustic_radar
