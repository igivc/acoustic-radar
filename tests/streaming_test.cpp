#include "acoustic_radar/processing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace acoustic_radar;
void require(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
    SpscRing<uint64_t, 31> ring;
    for(uint64_t i=0;i<31;++i) require(ring.push(i),"ring capacity");
    require(!ring.push(31),"full queue must reject");
    for(uint64_t i=0;i<31;++i){uint64_t value;require(ring.pop(value)&&value==i,"FIFO");}
    std::jthread producer([&]{for(uint64_t i=0;i<100000;++i)while(!ring.push(i))std::this_thread::yield();});
    for(uint64_t i=0;i<100000;++i){uint64_t value;while(!ring.pop(value))std::this_thread::yield();require(value==i,"concurrent FIFO");}
    producer.join();
    CycleProcessor processor;
    ChirpGenerator generator;
    unsigned count=0;
    uint64_t last=0;
    // Начальная тишина длиннее цикла; синхронизация должна установиться позже и сохраняться после пяти секунд.
    constexpr uint64_t prefix=ChirpGenerator::CycleSamples + 11200;
    for(uint64_t i=0;i<48000*14;++i){
        InputFrame input{i,{}};
        if(i>=prefix)input.samples[0]=generator.nextSample().left;
        input.samples[1]=float(.2*std::sin(2*std::acos(-1.)*2400*i/48000));
        for(auto& frame:processor.append(input)){
            require(frame.markerSample>=prefix,"false sync");
            require((frame.markerSample-prefix)%ChirpGenerator::CycleSamples==0,"sync drift");
            require(frame.markerSample>last,"duplicate cycle");last=frame.markerSample;++count;
        }
    }
    require(count >= (48000*14-prefix)/ChirpGenerator::CycleSamples - 1,"stream stopped or skipped cycles");
    // Имитируем потерю кадров: ни один цикл БПФ не должен пересекать разрыв данных.
    generator.reset();
    constexpr uint64_t restart=1000000;
    unsigned recovered=0;
    for(uint64_t i=0;i<ChirpGenerator::CycleSamples*4;++i){
        InputFrame input{restart+i,{}};input.samples[0]=generator.nextSample().left;
        for(auto& frame:processor.append(input)){require(frame.markerSample>=restart,"cycle crosses gap");++recovered;}
    }
    require(recovered>=2,"no resynchronization after gap");
    std::cout<<"Ring FIFO, wrap, concurrency; "<<count<<" streaming cycles; gap recovery passed\n";
}
