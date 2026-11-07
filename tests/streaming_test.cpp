#include "acoustic_radar/processing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace acoustic_radar;
// Проверки не отключаются в Release. Тесту не нужно реальное звуковое устройство:
// аудиокадры создаются непосредственно здесь и передаются в CycleProcessor.
void require(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
    // Небольшая ёмкость 31 не является степенью двойки: обычный переход индекса
    // к началу массива должен работать через %, без предположения о битовой маске.
    // Это не проверка переполнения самого 64-битного счётчика.
    SpscRing<uint64_t, 31> ring;
    // Все 31 ячейка доступны; 32-й элемент должен быть отвергнут без порчи
    // непрочитанных значений. Следующее чтение проверяет сохранение порядка FIFO.
    for(uint64_t i=0;i<31;++i) require(ring.push(i),"ring capacity");
    require(!ring.push(31),"full queue must reject");
    for(uint64_t i=0;i<31;++i){uint64_t value;require(ring.pop(value)&&value==i,"FIFO");}
    // Передаём 100000 последовательных чисел из одного потока в другой.
    // Запись много раз переходит от конца массива к началу. Потеря, повтор
    // или перестановка числа нарушит проверку value==i в потоке чтения.
    // yield позволяет другому потоку получить процессорное время. Данные должны
    // передаваться правильно благодаря acquire/release внутри кольца, а не паузам в тесте.
    // Здесь поток записи повторяет push при заполнении, чтобы передать все числа;
    // реальный звуковой callback вместо ожидания отбрасывает кадр и считает потерю.
    std::jthread producer([&]{for(uint64_t i=0;i<100000;++i)while(!ring.push(i))std::this_thread::yield();});
    for(uint64_t i=0;i<100000;++i){uint64_t value;while(!ring.pop(value))std::this_thread::yield();require(value==i,"concurrent FIFO");}
    // Дожидаемся завершения передачи между потоками. Дальше проверяем
    // обработку звука в одном потоке.
    // Следующие сценарии вызывают append напрямую, без ProcessingWorker и SDL.
    producer.join();
    CycleProcessor processor;
    ChirpGenerator generator;
    unsigned count=0;
    uint64_t last=0;
    // Начальная тишина длиннее цикла; синхронизация должна установиться позже и сохраняться после пяти секунд.
    // Дополнительные 11200 кадров сдвигают маркер относительно границ проверки
    // и заставляют сборщик передвинуть поиск после начального периода без маркера.
    constexpr uint64_t prefix=ChirpGenerator::CycleSamples + 11200;
    // 14 секунд моделируемого сигнала, а не 14 секунд ожидания по часам.
    // Такой интервал проверяет работу после прежнего пятисекундного ограничения.
    // Все четыре входа изначально нулевые; синхросигнал появляется только после
    // prefix, а независимый микрофонный тон не должен подменять синхронизацию.
    for(uint64_t i=0;i<48000*14;++i){
        InputFrame input{i,{}};
        if(i>=prefix)input.samples[0]=generator.nextSample().left;
        input.samples[1]=float(.2*std::sin(2*std::acos(-1.)*2400*i/48000));
        // Результат появляется только после накопления целого цикла.
        // Проверяем, что в тишине не найден ложный маркер, а найденные маркеры
        // стоят на ожидаемых позициях и один цикл не выдаётся повторно.
        for(auto& frame:processor.append(input)){
            require(frame.markerSample>=prefix,"false sync");
            require((frame.markerSample-prefix)%ChirpGenerator::CycleSamples==0,"sync drift");
            require(frame.markerSample>last,"duplicate cycle");last=frame.markerSample;++count;
        }
    }
    // Сравниваем с числом полных периодов после начальной тишины, допуская
    // один цикл запаса на пакетную проверку и незавершённый хвост потока.
    // Проверяем, что обработка продолжалась; точное число выданных циклов не задаём.
    require(count >= (48000*14-prefix)/ChirpGenerator::CycleSamples - 1,"stream stopped or skipped cycles");
    // Имитируем потерю кадров: ни один цикл БПФ не должен пересекать разрыв данных.
    generator.reset();
    // Новый индекс заведомо не продолжает предыдущую последовательность.
    // Генератор сброшен: первый новый маркер находится точно в restart.
    // Старый незавершённый хвост должен быть отброшен, а не склеен с новым.
    constexpr uint64_t restart=1000000;
    unsigned recovered=0;
    // Четырёх периодов достаточно для повторного захвата и выдачи нескольких
    // полных циклов. Проверка frame.markerSample>=restart исключает выдачу
    // результата с маркером из участка до разрыва.
    for(uint64_t i=0;i<ChirpGenerator::CycleSamples*4;++i){
        InputFrame input{restart+i,{}};input.samples[0]=generator.nextSample().left;
        // Каждый выданный результат должен начинаться уже после разрыва.
        // Результат с прежним маркером означал бы, что сборщик не сбросил состояние.
        for(auto& frame:processor.append(input)){require(frame.markerSample>=restart,"cycle crosses gap");++recovered;}
    }
    // Требуем не только сброс состояния, но и возобновление полезной обработки.
    // Здесь не проверяются акустические искажения, пропуски внутри драйвера,
    // очередь результатов ProcessingWorker или точность графического вывода.
    require(recovered>=2,"no resynchronization after gap");
    std::cout<<"Ring FIFO, wrap, concurrency; "<<count<<" streaming cycles; gap recovery passed\n";
}
