/**
 * @file chirp.cpp
 * @brief Расчёт фазы chirp и амплитудных огибающих.
 */
#include "acoustic_radar/chirp.hpp"

#include <algorithm>
#include <cmath>

namespace acoustic_radar {

namespace {

constexpr double pi = std::numbers::pi;

/**
 * @brief Вычисляет фазу линейного chirp в радианах.
 *
 * Для линейного изменения частоты f(t)=f0+k*t, где k=(f1-f0)/duration, фаза в радианах равна 2*pi*интеграл(f(t)dt) = 2*pi*(f0*t+k*t²/2).
 *
 * В sin() подставляем фазу. Формула sin(2*pi*f(t)*t) неверна для этой развёртки:
 * её производная дала бы частоту f0+2*k*t вместо заданной f0+k*t.
 * t и duration измеряются в секундах, частоты - в герцах, k - в Гц/с.
 * На границе t=duration частота достигает f1; сама граница не генерируется,
 * поскольку отсчёты берутся на полуинтервале [0, duration).
 * Вычисление фазы из времени не накапливает ошибку суммирования приращений.
 *
 * @param t Время от начала сигнала, в секундах.
 * @param f0 Начальная частота в герцах.
 * @param f1 Конечная частота в герцах.
 * @param duration Длительность сигнала в секундах.
 * @pre duration больше нуля; t находится в [0, duration).
 * @return Фаза сигнала в момент t.
 */
double chirpPhase(
    double t,
    double f0,
    double f1,
    double duration) noexcept
{
    const double sweepRate = (f1 - f0) / duration;

    return 2.0 * pi *
        (f0 * t + 0.5 * sweepRate * t * t);
}

/**
 * @brief Симметричное окно Ханна w(n)=0,5*(1-cos(2*pi*n/(N-1))).
 *
 * На первом и последнем отсчётах w=0, около середины w≈1. Плавные края
 * уменьшают резкие скачки и спектральную утечку короткого маркера.
 * Деление на N-1 делает последний коэффициент окна нулевым. Если в окне
 * меньше двух отсчётов, возвращаем ноль, не выполняя деление.
 *
 * @param position Индекс отсчёта внутри окна.
 * @param count Число отсчётов окна.
 * @pre При count больше 1 индекс position меньше count.
 * @return Коэффициент окна; при count не больше 1 возвращается ноль.
 */
double hannWindow(
    uint64_t position,
    uint64_t count) noexcept
{
    if (count <= 1)
        return 0.0;

    return 0.5 * (
        1.0 - std::cos(
            2.0 * pi *
            static_cast<double>(position) /
            static_cast<double>(count - 1)
        )
    );
}

/**
 * @brief Огибающая возбуждения состоит из трёх частей: косинусное нарастание от 0 до 1 за первые 10 мс,
 * постоянный уровень 1 и симметричный спад до 0 за последние 10 мс.
 *
 * В отличие от окна Ханна на весь сигнал,
 * средняя часть сохраняет полную амплитуду для возбуждения резонансов.
 * Аргумент t находится внутри [0, duration); допустимая duration>=20 мс
 * гарантирована ограничениями настроек. При 20 мс плоского участка нет.
 *
 * @param t Время внутри возбуждающего сигнала, в секундах.
 * @param duration Длительность возбуждения, в секундах.
 * @pre duration не меньше 0,020 с; t находится в [0, duration).
 * @return Коэффициент огибающей от 0 до 1.
 */
double acousticEnvelope(double t, double duration) noexcept
{
    constexpr double fade = 0.010;

    if (t < fade)
    {
        return 0.5 *
            (1.0 - std::cos(pi * t / fade));
    }

    if (t > duration - fade)
    {
        return 0.5 *
            (1.0 - std::cos(
                pi * (duration - t) / fade));
    }

    return 1.0;
}

} // анонимное пространство имён

void ChirpGenerator::setAcousticParameters(unsigned startHz, unsigned durationMs) noexcept
{
    startHz = std::clamp(startHz, MinStartHz, MaxStartHz);
    durationMs = std::clamp(durationMs, MinDurationMs, MaxDurationMs);
    // Одна атомарная переменная не даёт прочитать новую частоту вместе со старой
    // длительностью. Сдвиг на 32 бита упаковывает обе величины в одно слово.
    // Порядка memory_order_relaxed достаточно: атомарность пары обеспечивается,
    // а синхронизировать с ней другие массивы или указатели не требуется.
    requestedParameters_.store(
        (uint64_t{startHz} << 32) | durationMs, std::memory_order_relaxed);
}

StereoSample ChirpGenerator::nextSample() noexcept
{
    // Остаток от деления задаёт позицию в цикле; глобальный счётчик продолжает
    // расти. Например, кадры 0 и 48000 начинают соседние секундные циклы.
    const uint64_t position =
        sampleIndex_++ % CycleSamples;

    // Настройки фиксируются на весь цикл. Интерфейс может менять запрос в любой
    // момент, но фаза текущего возбуждения при этом не разрывается.
    // Одно атомарное чтение возвращает частоту и длительность из одного запроса;
    // сохраняем их в обычных полях и используем до конца цикла.
    if (position == 0)
    {
        const auto parameters = requestedParameters_.load(std::memory_order_relaxed);
        startHz_ = static_cast<unsigned>(parameters >> 32);
        durationMs_ = static_cast<unsigned>(parameters & 0xffffffffULL);
    }

    // Фаза вычисляется по локальному времени от начала цикла. Это одновременно
    // начало обоих сигналов: синхромаркер не добавляет задержку к возбуждению.
    const double duration = durationMs_ / 1000.0;
    // При 48 кГц в каждой миллисекунде ровно 48 отсчётов.
    const uint64_t acousticSamples = uint64_t{durationMs_} * 48;
    const double t =
        static_cast<double>(position) / SampleRate;

    // После окончания возбуждения оба канала молчат до следующего цикла.
    StereoSample result{0.0f, 0.0f};

    // Левый канал: синхромаркер 8–12 кГц.
    if (position < SyncSamples)
    {
        const double phase = chirpPhase(
            t,
            SyncStartFrequency,
            SyncEndFrequency,
            SyncDuration
        );

        const double window = hannWindow(
            position,
            SyncSamples
        );

        // Амплитуда * огибающая * синус фазы. Маркер не смешивается с правым
        // выходом: каналы имеют независимые назначения и частотные диапазоны.
        result.left = static_cast<float>(
            SyncAmplitude *
            window *
            std::sin(phase)
        );
    }

    // Правый канал: акустический ЛЧМ-сигнал с настраиваемыми параметрами.
    if (position < acousticSamples)
    {
        const double phase = chirpPhase(
            t,
            startHz_,
            startHz_ + BandwidthHz,
            duration
        );

        const double envelope = acousticEnvelope(t, duration);

        // Верхняя граница следует за нижней с фиксированной шириной полосы.
        // За пределами acousticSamples сохраняется исходный ноль результата:
        // там микрофон регистрирует свободное затухание и фон помещения.
        result.right = static_cast<float>(
            AcousticAmplitude *
            envelope *
            std::sin(phase)
        );
    }

    return result;
}

} // пространство имён acoustic_radar
