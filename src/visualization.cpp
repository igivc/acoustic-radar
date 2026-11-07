#include "acoustic_radar/visualization.hpp"
#include "acoustic_radar/chirp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace acoustic_radar {
namespace {
// Координаты накопления независимы от размера окна: при изменении окна
// масштабируется готовая текстура, а не пересчитываются исходные FFT-спектры.
constexpr int Width = 1100, Height = 540;
constexpr double CycleSeconds = ChirpGenerator::CycleDuration;
constexpr int CycleMs = static_cast<int>(CycleSeconds * 1000);
// SDL3 возвращает bool; превращаем ошибки проверяемых операций в исключение,
// которое перехватывает main и после этого освобождает графические ресурсы.
void check(bool ok) { if (!ok) throw std::runtime_error(SDL_GetError()); }
}
Spectrogram::Spectrogram(SDL_Renderer* renderer): renderer_(renderer) {
    // Оконный буфер не хранит историю между кадрами. Отдельная целевая
    // текстура хранит историю и масштабируется при изменении размера окна.
    accumulation_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, Width, Height);
    if (!accumulation_) throw std::runtime_error(SDL_GetError());
    // Если конструктор выбросит исключение, деструктор Spectrogram не вызовется,
    // поэтому уже созданную текстуру освобождаем явно перед повторным throw.
    try { clear(); } catch (...) { SDL_DestroyTexture(accumulation_); throw; }
}
Spectrogram::~Spectrogram() { SDL_DestroyTexture(accumulation_); }
void Spectrogram::clear() {
    // Цель отрисовки — текстура, не окно. SDL_RenderClear заполняет её целиком;
    // nullptr затем возвращает цель к стандартному оконному буферу.
    check(SDL_SetRenderTarget(renderer_, accumulation_));
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    check(SDL_RenderClear(renderer_));
    check(SDL_SetRenderTarget(renderer_, nullptr));
    cycles_ = 0;
}
void Spectrogram::setRange(int lowHz) {
    // Удаляем следы старого диапазона: на новой шкале они показывали бы другую частоту.
    if (lowHz != lowHz_) { lowHz_ = lowHz; clear(); }
}
void Spectrogram::add(const SpectrumFrame& frame) {
    if (frame.columns.empty()) return;
    check(SDL_SetRenderTarget(renderer_, accumulation_));
    check(SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND));
    // Затемняем историю один раз на новый спектральный кадр.
    // Повторный вывод того же изображения через draw() не меняет его яркость.
    // Чёрный слой alpha=102/255 оставляет 60% предыдущего изображения.
    // Для обычного смешивания RGB_new = alpha*RGB_src + (1-alpha)*RGB_old.
    // При RGB_src=0 остаётся 0,6*RGB_old. Без новых ячеек после n циклов
    // вклад прежнего следа примерно равен 0,6^n (с учётом округления в 8 битах).
    // Передача nullptr в RenderFillRect означает всю текущую цель отрисовки.
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 102); // Уменьшаем яркость предыдущих циклов на 40%.
    check(SDL_RenderFillRect(renderer_, nullptr));
    check(SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND));
    // Геометрия ячейки соответствует шагу STFT и ширине частотного бина,
    // а не полной длительности перекрывающегося окна FFT.
    // Ширина = Width*(HopSize/SampleRate)/CycleDuration, высота =
    // Height*(SampleRate/FFTSize)/1000. При 4096/1024 и 48 кГц это примерно
    // 23,47×6,33 пикселя исходной текстуры. Рисовать окно шириной 85,33 мс
    // было бы неверно: перекрывающиеся окна многократно закрашивали бы соседей.
    const float cellWidth = Width * (float(SpectrumAnalyzer::HopSize) / ChirpGenerator::SampleRate) / CycleSeconds;
    const float cellHeight = Height * (ChirpGenerator::SampleRate / SpectrumAnalyzer::FFTSize) / 1000.f;
    for (const auto& column : frame.columns) {
        // Постоянная составляющая (бин 0) не отображается. Частотная полоса
        // отбирается здесь: SpectrumAnalyzer возвращает спектр до Найквиста.
        for (size_t bin = 1; bin < column.decibels.size(); ++bin) {
            const float frequency = bin * ChirpGenerator::SampleRate / SpectrumAnalyzer::FFTSize;
            if (frequency < lowHz_ || frequency > lowHz_ + 1000) continue;
            // Нормированная координата частоты: 0 на нижней границе, 1 на верхней.
            // Это одновременно параметр палитры и положение по вертикали.
            const float frequencyPosition = (frequency - lowHz_) / 1000.f;
            // Частота задаёт оттенок: красный → жёлтый → зелёный → голубой → синий.
            // Четыре линейных участка соответствуют оттенкам HSV от 0° до 240°.
            // Линейно переводим уже логарифмический уровень dBFS в [0,1]:
            // -80 и ниже → непрозрачность 0, -15 и выше → непрозрачность 1.
            // Это фиксированный визуальный масштаб, не автоматическая регулировка.
            // std::clamp не заменяет NaN конечным числом: конечность уровня пока предполагается;
            // преобразование NaN в Uint8 ниже вызывает UB — это известная ошибка.
            const float brightness = std::clamp((column.decibels[bin] + 80.f) / 65.f, 0.f, 1.f);
            if (brightness <= 0) continue;
            // hue=0: красный (1,0,0); 1: жёлтый (1,1,0);
            // 2: зелёный (0,1,0); 3: голубой (0,1,1); 4: синий (0,0,1).
            // Между опорными значениями RGB меняется линейно.
            const float hue = frequencyPosition * 4;
            const float red = std::clamp(2.f - hue, 0.f, 1.f);
            const float green = std::clamp(std::min(hue, 4.f - hue), 0.f, 1.f);
            const float blue = std::clamp(hue - 2.f, 0.f, 1.f);
            // Частота задаёт цвет, уровень — непрозрачность. Обычное смешивание
            // сохраняет оттенок при накоплении. Простое сложение цветов могло бы
            // превратить красные и зелёные участки в белые.
            SDL_SetRenderDrawColor(renderer_, Uint8(255 * red),
                Uint8(255 * green), Uint8(255 * blue), Uint8(255 * brightness));
            // Общие границы округляются к пикселям текстуры: соседние ячейки
            // не перекрываются и не усиливают яркость на стыках при смешивании.
            // В SDL начало координат сверху слева, Y растёт вниз. Поэтому
            // частота отображается как 1-frequencyPosition: высокая сверху.
            // Ячейку центрируем по времени измерения и центральной частоте бина.
            const float centerX = float(column.timeSeconds / CycleSeconds * Width);
            const float centerY = Height * (1 - frequencyPosition);
            const float left = std::round(centerX - cellWidth / 2);
            const float right = std::round(centerX + cellWidth / 2);
            const float top = std::round(centerY - cellHeight / 2);
            const float bottom = std::round(centerY + cellHeight / 2);
            const SDL_FRect cell{left, top, right - left, bottom - top};
            check(SDL_RenderFillRect(renderer_, &cell));
        }
    }
    // Возвращаем отрисовку в окно. Оси и подписи рисуются отдельно и потому
    // не попадают в историю и не тускнеют вместе со старыми спектрами.
    check(SDL_SetRenderTarget(renderer_, nullptr));
    check(SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE));
    ++cycles_;
    correlation_ = frame.correlation;
}
// Только вывод накопления и подписей: эта функция не изменяет историю.
void Spectrogram::draw(int durationMs) {
    // Размер поверхности вывода может отличаться от логического размера окна
    // на экранах с высокой плотностью пикселей; используем размер renderer.
    int w, h;
    check(SDL_GetRenderOutputSize(renderer_, &w, &h));
    SDL_SetRenderDrawColor(renderer_, 10, 15, 25, 255);
    check(SDL_RenderClear(renderer_));
    // Поля зарезервированы для подписей осей и статуса. Ограничение размером
    // хотя бы 1×1 не допускает отрицательную область при очень маленьком окне.
    const SDL_FRect plot{80, 65, float(std::max(1, w - 115)), float(std::max(1, h - 145))};
    check(SDL_RenderTexture(renderer_, accumulation_, nullptr, &plot));
    SDL_SetRenderDrawColor(renderer_, 100, 110, 125, 255);
    // Метки времени каждые 100 мс; координаты масштабируются вместе с графиком.
    for (int t = 0; t <= CycleMs; t += 100) {
        float x = plot.x + plot.w * t / CycleMs;
        SDL_RenderLine(renderer_, x, plot.y + plot.h, x, plot.y + plot.h + 5);
        char text[32]; std::snprintf(text, sizeof(text), "%d ms", t);
        SDL_RenderDebugText(renderer_, x - 15, plot.y + plot.h + 12, text);
    }
    // Метки частот каждые 200 Гц отсчитываются от текущей нижней границы.
    for (int f = 0; f <= 1000; f += 200) {
        float y = plot.y + plot.h * (1 - f / 1000.f);
        char text[32]; std::snprintf(text, sizeof(text), "%d Hz", lowHz_ + f);
        SDL_RenderDebugText(renderer_, 8, y - 4, text);
    }
    // Линия отражает запрошенную в интерфейсе длительность, не измеренную границу эха.
    // Текущий интерфейс передаёт новое значение сразу, а генератор принимает его
    // на границе цикла: во время перестройки линия может опережать данные кадра.
    const float excitationEnd = plot.x + plot.w * durationMs / CycleMs;
    SDL_RenderLine(renderer_, excitationEnd, plot.y, excitationEnd, plot.y + plot.h);
    char status[192];
    std::snprintf(status, sizeof(status), "INPUT 2 | FFT %zu / hop %zu | cycles: %u | sync: %.3f | brightness: -80..-15 dBFS", SpectrumAnalyzer::FFTSize, SpectrumAnalyzer::HopSize, cycles_, correlation_);
    SDL_RenderDebugText(renderer_, 80, 20, status);
    SDL_RenderDebugText(renderer_, 80, float(h - 35), "UP/DOWN: frequency | LEFT/RIGHT: duration | ESC: exit | vertical line: excitation end");
    // Выводим собранный оконный кадр. Сама история остаётся в accumulation_,
    // поэтому очистка оконного буфера в следующем draw() её не уничтожает.
    check(SDL_RenderPresent(renderer_));
}
}
