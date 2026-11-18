#pragma once
#include "Engine.h"
namespace stage {
class MeterFixture final:public juce::AudioPluginInstance {
    float left,right;
public:
    MeterFixture(float l,float r):AudioPluginInstance(BusesProperties().withOutput("Stereo",juce::AudioChannelSet::stereo(),true)),left(l),right(r){}
    const juce::String getName()const override{return "Meter fixture";}
    void fillInPluginDescription(juce::PluginDescription&)const override{}
    void prepareToPlay(double,int)override{}
    void releaseResources()override{}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{b.clear();for(int k=0;k<b.getNumSamples();++k){b.setSample(0,k,left);b.setSample(1,k,right);}}
    bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}
    double getTailLengthSeconds()const override{return 0;}
    bool hasEditor()const override{return false;}juce::AudioProcessorEditor* createEditor()override{return nullptr;}
    int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}
    const juce::String getProgramName(int)override{return {};};void changeProgramName(int,const juce::String&)override{}
    void getStateInformation(juce::MemoryBlock&)override{}void setStateInformation(const void*,int)override{}
};
inline void meterTests(const std::function<void(bool,const juce::String&)>& check){
    auto instance=std::make_unique<Engine>();auto& e=*instance;juce::AudioBuffer<float> out(2,64);
    e.slots[0].plugin=std::make_unique<MeterFixture>(.25f,.75f);e.masterValue=.5f;e.prepare(48000,64);
    auto tick=[&](int count=1){for(int n=0;n<count;++n)e.render(out.getArrayOfWritePointers(),2,64);};
    auto near=[](float a,float b){return std::abs(a-b)<.00001f;};tick();auto source=e.slots[0].meter.read(),master=e.outputMeter.read();
    check(near(source.rawPeak[0],.25f)&&near(source.rawPeak[1],.75f)&&near(source.rawRms[0],.25f)&&near(source.rawRms[1],.75f),"Source meters measure stereo audio after the source fader");
    check(near(master.rawPeak[0],out.getMagnitude(0,0,64))&&near(master.rawPeak[1],out.getMagnitude(1,0,64))&&near(master.rawRms[1],out.getRMSLevel(1,0,64)),"Output meter peak and RMS agree with the actual delivered buffers");
    e.control(SlotGain,0,.4f);tick(20);source=e.slots[0].meter.read();master=e.outputMeter.read();check(near(source.rawPeak[0],.1f)&&near(master.rawPeak[0],.05f),"Source gain precedes its meter, and master gain affects only the final output meter");
    e.control(Master,0,0);tick(30);source=e.slots[0].meter.read();master=e.outputMeter.read();check(near(master.rawPeak[0],0)&&source.rawPeak[0]>.09f,"Master mute leaves the selected-source meter independent");
    e.clips[0].data=makeDemoClip(0);e.control(Master,0,.5f);e.control(SlotGain,0,0);tick(30);e.control(ClipPlay,0,1);tick(10);check(e.outputMeter.read().rawPeak[0]>0&&e.slots[0].meter.read().rawPeak[0]==0,"Final-output meters include sample effects");
    e.control(ClipStopAll);tick(20);e.sequence.plugin=std::make_unique<MeterFixture>(.3f,.6f);e.sequence.gain=.5f;e.sequence.prepare(48000,64);e.control(SequencePlay);tick(10);check(near(e.sequence.meter.read().rawPeak[1],.3f)&&near(e.outputMeter.read().rawPeak[1],.15f),"Dedicated sequence source and gain are included in final-output meters");
    e.control(SequenceStop);tick(2300);e.slots[0].plugin=std::make_unique<MeterFixture>(2,4);e.slots[0].gain=1;e.slots[0].trim.setCurrentAndTargetValue(1);e.masterValue=1;e.prepare(48000,64);tick();master=e.outputMeter.read();check(master.clipped&&master.rawPeak[0]==1&&master.rawPeak[1]==1&&e.slots[0].meter.read().rawPeak[1]==4,"Clipping indication uses pre-limit overload while output bars measure the post-limit signal");
    e.slots[0].plugin=std::make_unique<MeterFixture>(NAN,.4f);e.prepare(48000,64);tick();master=e.outputMeter.read();check(master.rawPeak[0]==0&&std::isfinite(master.rawRms[0])&&near(master.rawPeak[1],.4f),"Non-finite plugin samples cannot poison meter telemetry or the other channel");
    e.audioDeviceStopped();check(e.outputMeter.read().peak[1]==0&&e.slots[0].meter.read().hold[1]==0&&!e.outputMeter.read().clipped,"Audio-device stop clears meter and clipping state");
    AudioMeter meter;meter.push({.8f,.2f},{.64,.04},1,48000,true);for(int n=0;n<375;++n)meter.push({}, {},64,48000);auto held=meter.read();check(held.hold[0]>=.79f&&held.peak[0]>.2f&&held.clipped,"A transient remains visible between GUI polls through peak hold and audio-time release");
    for(int n=0;n<750;++n)meter.push({}, {},64,48000);check(meter.read().hold[0]<.5f&&!meter.read().clipped,"Peak hold releases and clipping indication expires after silence");
    AudioMeter small,large;for(int n=0;n<750;++n)small.push({.5f,.5f},{16,16},64,48000);for(int n=0;n<75;++n)large.push({.5f,.5f},{160,160},640,48000);check(std::abs(small.read().rms[0]-large.read().rms[0])<.00002f,"RMS averaging uses elapsed audio time independently of buffer size");
    check(near(juce::Decibels::gainToDecibels(.5f),-6.0206f),"dBFS meter scaling uses 20 log10 of amplitude");
}
}
