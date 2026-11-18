#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
namespace stage {
struct MeterReading {
    std::array<float,2> peak{},rms{},hold{},rawPeak{},rawRms{};
    bool clipped=false;
};
// Single audio-thread writer; the GUI only reads lock-free scalar telemetry.
// No audio pointers, allocation or GUI calls cross the callback boundary.
class AudioMeter {
    static_assert(std::atomic<float>::is_always_lock_free);
    std::array<std::atomic<float>,2> peak{},rms{},hold{},rawPeak{},rawRms{};
    std::atomic<bool> clipped{false};
    std::array<float,2> envelope{},power{},held{};
    std::array<int64_t,2> holdLeft{};
    int64_t clipLeft=0;
public:
    void reset(){envelope.fill(0);power.fill(0);held.fill(0);holdLeft.fill(0);clipLeft=0;for(int c=0;c<2;++c){peak[(size_t)c]=0;rms[(size_t)c]=0;hold[(size_t)c]=0;rawPeak[(size_t)c]=0;rawRms[(size_t)c]=0;}clipped=false;}
    void push(const std::array<float,2>& maxima,const std::array<double,2>& squares,int samples,double rate,bool overloaded=false)noexcept{
        if(samples<=0||rate<=0)return;
        const double dt=samples/rate;
        const float release=(float)std::pow(10.0,-dt); // 20 dB/s release, instant attack
        const float holdRelease=(float)std::pow(10.0,-.6*dt); // 12 dB/s after one second
        const float average=(float)std::exp(-dt/.3); // 300 ms power averaging
        for(int c=0;c<2;++c){auto n=(size_t)c;float p=std::isfinite(maxima[n])?juce::jmax(0.0f,maxima[n]):0;
            float mean=std::isfinite(squares[n])?(float)juce::jmax(0.0,squares[n]/samples):0;
            rawPeak[n].store(p,std::memory_order_relaxed);rawRms[n].store(std::sqrt(mean),std::memory_order_relaxed);
            envelope[n]=juce::jmax(p,envelope[n]*release);power[n]=average*power[n]+(1-average)*mean;
            if(p>=held[n]){held[n]=p;holdLeft[n]=(int64_t)rate;}else{holdLeft[n]-=samples;if(holdLeft[n]<=0)held[n]=juce::jmax(p,held[n]*holdRelease);}
            peak[n].store(envelope[n],std::memory_order_relaxed);rms[n].store(std::sqrt(power[n]),std::memory_order_relaxed);hold[n].store(held[n],std::memory_order_relaxed);
        }
        clipLeft=overloaded?(int64_t)rate:juce::jmax((int64_t)0,clipLeft-samples);clipped.store(clipLeft>0,std::memory_order_relaxed);
    }
    MeterReading read()const noexcept{MeterReading r;for(int c=0;c<2;++c){auto n=(size_t)c;r.peak[n]=peak[n].load(std::memory_order_relaxed);r.rms[n]=rms[n].load(std::memory_order_relaxed);r.hold[n]=hold[n].load(std::memory_order_relaxed);r.rawPeak[n]=rawPeak[n].load(std::memory_order_relaxed);r.rawRms[n]=rawRms[n].load(std::memory_order_relaxed);}r.clipped=clipped.load(std::memory_order_relaxed);return r;}
};
}
