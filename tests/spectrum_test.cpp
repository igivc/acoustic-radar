/**
 * @file spectrum_test.cpp
 * @brief Проверки спектра, временных координат и синхронизации на известных сигналах.
 */
#include "acoustic_radar/spectrum.hpp"
#include "acoustic_radar/chirp.hpp"
#include "acoustic_radar/sync.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
/**
 * @brief Проверки остаются активными и в Release, в отличие от assert с NDEBUG.
 *
 * Исключение делает код завершения ненулевым, а CTest сообщает причину сбоя.
 *
 * @param value Проверяемое условие.
 * @param reason Сообщение об ошибке.
 * @throws std::runtime_error Условие value не выполнено.
 */
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
/**
 * @brief Проверка DSP на известных синтетических данных без аудиоустройства и SDL.
 *
 * Генератор создаёт эталон синхронизации, но тестовый синус вычисляется отдельно:
 * его ожидаемые частота и амплитуда известны до вызова анализатора.
 *
 * @return 0 при успешном завершении всех проверок.
 */
int main() {
    using namespace acoustic_radar;
    SpectrumAnalyzer analyzer;
    // Ненулевое смещение проверяет отсчёт времени от маркера, а не от буфера.
    // Смещение не кратно шагу STFT: алгоритм не должен требовать выравнивания.
    constexpr size_t marker = 1253;
    std::vector<float> input((marker + ChirpGenerator::CycleSamples)*4);
    // Заполняем только микрофонный канал с индексом 1; остальные остаются нулевыми.
    // Частота точно совпадает с бином 100 (1171,875 Гц для FFT 4096), чтобы
    // проверять масштаб амплитуды на тоне, который не лежит между бинами.
    // Частота намеренно не обязана входить в полосу отображения: FFT считает
    // весь спектр, а диапазон 1900–2900 Гц отбирается уже визуализацией.
    const double frequency = 100 * 48000. / SpectrumAnalyzer::FFTSize;
    for (size_t i = marker; i < input.size()/4; ++i)
        input[i*4+1] = .5 * std::sin(2*std::acos(-1.)*frequency*(i-marker)/48000);
    // В буфере есть ровно один полный цикл после маркера. correlation=.9
    // передаётся как метаданные, не вычисляется анализатором по этому синусу.
    auto frame = analyzer.analyze(input.data(), input.size()/4, marker, .9);
    // Ожидаем только полные окна: floor((CycleSamples-FFTSize)/HopSize)+1.
    // Дополнительное окно означало бы выход за цикл или дополнение нулями.
    require(frame.columns.size() == (ChirpGenerator::CycleSamples - SpectrumAnalyzer::FFTSize) / SpectrumAnalyzer::HopSize + 1, "FFT column count");
    // Первый центр окна: FFTSize/2 отсчётов от маркера, а не marker+FFTSize/2.
    // Малый допуск относится к вычислению double-координаты времени.
    require(std::abs(frame.columns.front().timeSeconds - (SpectrumAnalyzer::FFTSize / 2.)/48000) < 1e-10, "center timestamp");
    // Проверяем каждый столбец стационарного тона. Неверный канал даст тишину,
    // неверный размер FFT или адресация изменят положение спектрального пика.
    for (const auto& column : frame.columns) {
        auto peak = std::max_element(column.decibels.begin(), column.decibels.end())-column.decibels.begin();
        require(peak == 100, "wrong frequency or microphone channel");
        // 20*log10(0,5)≈-6,0206 dBFS. Допуск 0,01 дБ учитывает Float32
        // и окно Ханна, но выявляет пропущенное удвоение или неверную нормировку.
        require(std::abs(column.decibels[100] + 6.0206) < .01, "amplitude calibration");
    }
    // Убираем один аудиокадр: цикл уже неполный, даже если ранние окна помещаются.
    // По правилам analyze() неполный цикл должен вернуть пустой список столбцов.
    require(analyzer.analyze(input.data(), input.size()/4-1, marker, 1).columns.empty(), "partial cycle accepted");
    // Повторно используем тот же FFTW-план после ненулевого сигнала.
    // Если рабочие массивы сохранили старый сигнал, проверка тишины это обнаружит.
    std::fill(input.begin(), input.end(), 0);
    frame = analyzer.analyze(input.data(), input.size()/4, marker, .9);
    // Тишина должна давать нижний численный предел около -200 dBFS вместо log(0).
    // Это проверка одного бина первого окна, не всех спектральных значений.
    require(frame.columns.front().decibels[100] <= -199, "silence floor");
    // Теперь проверяем синхронизацию отдельно: помещаем идеальный маркер
    // в канал 0 с известным смещением. Шума, инверсии и аналогового тракта нет,
    // поэтому ожидается точное совпадение индекса, без допуска в отсчётах.
    ChirpGenerator chirp;
    for(size_t i=marker;i<input.size()/4;++i) input[i*4] = chirp.nextSample().left;
    auto markers = SyncDetector{}.detect(input.data(),input.size()/4);
    require(!markers.empty() && markers[0].sampleIndex == marker, "sync alignment");
    std::cout << "FFT frequency, amplitude, timestamps, silence, partial cycles, sync: passed\n";
}
