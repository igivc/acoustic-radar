/**
 * @file sync.cpp
 * @brief Построение эталона и поиск корреляционных максимумов.
 */
#include "acoustic_radar/sync.hpp"
#include "acoustic_radar/chirp.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace acoustic_radar {

namespace {

/**
 * @brief Данные поступают в формате четырёх чередующихся входов.
 *
 * Номер канала 0
 * соответствует петле синхронизации; микрофонный канал здесь не анализируется.
 */
constexpr size_t InputChannels = 4;
constexpr size_t SyncChannel = 0;

constexpr size_t MarkerSamples =
    ChirpGenerator::SyncSamples;

constexpr size_t CycleSamples =
    ChirpGenerator::CycleSamples;

/**
 * @brief Порог относится к форме нормированного сигнала, а не его амплитуде.
 *
 * Увеличение порога уменьшает число ложных совпадений, но делает обнаружение
 * чувствительнее к шуму, фильтрации и искажениям аналогового тракта.
 */
constexpr double DetectionThreshold = 0.75;

/**
 * @brief Максимальное отклонение от ожидаемой позиции последующих маркеров: 512/48000 = 10,67 мс в
 * каждую сторону.
 *
 * Это допуск поиска, не точность
 * определения времени: внутри диапазона позиции перебираются по одному кадру.
 */
constexpr size_t SearchRadius = 512;

/**
 * @brief Положение и оценка одного корреляционного максимума.
 */
struct Peak
{
    size_t position;
    double correlation;
};

/**
 * @brief Генерируем эталон тем же кодом, что и реальный выход: частоты, длительность и окно
 * синхромаркера не должны расходиться.
 *
 * Используем только левый канал первых SyncSamples кадров: правая часть
 * генератора относится к возбуждению и никак не влияет на эталон.
 * При текущих настройках эталон содержит 480 отсчётов (10 мс при 48 кГц).
 *
 * @return Отсчёты синхромаркера левого выходного канала.
 */
std::vector<float> makeReference()
{
    ChirpGenerator generator;

    std::vector<float> reference;
    reference.reserve(MarkerSamples);

    for (size_t i = 0; i < MarkerSamples; ++i)
    {
        reference.push_back(
            generator.nextSample().left
        );
    }

    return reference;
}

/**
 * @brief Ищет одно наилучшее совпадение в диапазоне стартовых позиций [first,last] с включёнными границами.
 *
 * Для каждой позиции требуется полное окно эталона.
 * Пустой optional означает, что ни одна позиция не прошла порог обнаружения.
 *
 * @param input Входные кадры с четырьмя чередующимися каналами.
 * @param frames Число кадров во входном буфере.
 * @param reference Эталон синхромаркера длиной MarkerSamples.
 * @param first Первая проверяемая позиция в аудиокадрах.
 * @param last Последняя проверяемая позиция, включительно.
 * @pre input указывает на frames * InputChannels доступных отсчётов.
 * @return Лучший пик, прошедший порог, либо std::nullopt.
 */
std::optional<Peak> findPeak(
    const float* input,
    size_t frames,
    const std::vector<float>& reference,
    size_t first,
    size_t last)
{
    if (frames < MarkerSamples)
        return std::nullopt;

    // Последняя допустимая позиция гарантирует полное окно корреляции.
    const size_t maximumStart = frames - MarkerSamples;

    first = std::min(first, maximumStart);
    last = std::min(last, maximumStart);

    if (first > last)
        return std::nullopt;

    // Энергия здесь - сумма квадратов цифровых отсчётов, без перевода в ватты.
    // Эталон не меняется при переборе позиций, поэтому его энергию считаем
    // один раз на диапазон, а не заново для каждого возможного сдвига.
    double referenceEnergy = 0.0;

    for (float value : reference)
        referenceEnergy += double(value) * value;

    if (referenceEnergy == 0.0)
        return std::nullopt;

    // Начальное значение -∞ ниже любой конечной оценки. Если подходящих
    // кандидатов нет, оно не изменится и проверка порога отвергнет результат.
    // Сравнение NaN > best.correlation также не обновляет лучший результат.
    Peak best{0, -std::numeric_limits<double>::infinity()};

    // Прямой перебор позиций; вычислительно дорогой участок,
    // поэтому вызывается только в рабочем потоке, не в обработчике звука.
    // Для P позиций и M отсчётов эталона трудоёмкость порядка P*M.
    // Начальный поиск дороже последующих узких диапазонов ±SearchRadius.
    for (size_t position = first; position <= last; ++position)
    {
        double dot = 0.0;
        double signalEnergy = 0.0;

        for (size_t j = 0; j < MarkerSamples; ++j)
        {
            const double sample =
                input[(position + j) * InputChannels + SyncChannel];

            const double expected = reference[j];

            dot += sample * expected;
            signalEnergy += sample * sample;
        }

        // Практически нулевая энергия не содержит надёжного маркера и дала бы
        // деление на ноль либо усиление численной ошибки при нормировке.
        if (signalEnergy < 1e-12)
            continue;

        // Нормировка энергиями делает оценку нечувствительной к усилению.
        // При смене полярности оценка становится отрицательной и маркер не принимается.
        // Формула: r = sum(x[j]*s[j]) / sqrt(sum(x[j]^2)*sum(s[j]^2)).
        // Это нормированное скалярное произведение. Среднее значение не вычитаем,
        // поэтому постоянное смещение входа способно уменьшить оценку.
        // Точное положительное масштабирование x=a*s сокращается в дроби;
        // шум и изменение формы сигнала при этом не компенсируются.
        const double correlation =
            dot / std::sqrt(signalEnergy * referenceEnergy);

        // Выбираем максимум по всему диапазону, а не первое превышение порога.
        // При равных оценках остаётся более ранняя позиция (строгое сравнение >).
        if (correlation > best.correlation)
        {
            best = {
                position,
                correlation
            };
        }
    }

    // Одного локального максимума недостаточно: лучший кандидат также должен
    // пройти порог. Значение ровно DetectionThreshold принимается.
    if (best.correlation < DetectionThreshold)
        return std::nullopt;

    return best;
}

} // анонимное пространство имён

std::vector<SyncMarker> SyncDetector::detect(
    const float* input,
    size_t frames) const
{
    std::vector<SyncMarker> markers;

    if (!input || frames < MarkerSamples)
        return markers;

    const auto reference = makeReference();

    // Возвращается наиболее коррелированный маркер в поисковом диапазоне,
    // он не обязательно хронологически самый ранний.
    // Первый маркер ищем на протяжении
    // одного полного периода. При коротком входе findPeak ограничивает диапазон
    // последней позицией, где эталон помещается целиком; нулевого дополнения нет.
    // Если начальное совпадение не найдено, следующие периоды здесь не сканируются:
    // потоковый сборщик позже передвинет буфер и снова вызовет detect().
    const auto first = findPeak(
        input,
        frames,
        reference,
        0,
        CycleSamples - 1
    );

    if (!first)
        return markers;

    markers.push_back({
        first->position,
        first->correlation
    });

    // Остальные маркеры должны повторяться
    // через CycleSamples отсчётов. Ожидания привязаны к первому найденному
    // маркеру, не к предыдущему пику: ошибка одного пика не сдвигает следующие ожидания,
    // но если фактический период постепенно меняется, маркер может выйти за допуск.
    for (size_t cycle = 1; ; ++cycle)
    {
        const size_t expected =
            first->position + cycle * CycleSamples;

        // Без полного эталона в ожидаемой позиции текущий и более поздние
        // периоды не проверяются. Детектор не дописывает недостающие отсчёты.
        if (expected >= frames ||
            frames - expected < MarkerSamples)
            break;

        const size_t begin =
            expected > SearchRadius
                ? expected - SearchRadius
                : 0;

        const size_t end = expected + SearchRadius;

        const auto peak = findPeak(
            input,
            frames,
            reference,
            begin,
            end
        );

        // Пропущенный маркер не добавляет заглушку в вектор; цикл поиска
        // продолжается, поэтому соседние результаты могут быть через два периода.
        if (peak)
        {
            markers.push_back({
                peak->position,
                peak->correlation
            });
        }
    }

    return markers;
}

} // пространство имён acoustic_radar
