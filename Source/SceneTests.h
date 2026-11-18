#pragma once
#include "Engine.h"
namespace stage {
inline void sceneTests(const std::function<void(bool,const juce::String&)>& check){
    auto instance=std::make_unique<Engine>();auto& e=*instance;juce::AudioBuffer<float> out(2,64);
    for(int i=0;i<3;++i)e.slots[(size_t)i].plugin=makeTestSynth();
    e.slots[2].layer=true;e.slots[2].transpose=5;
    e.scenes[0].name="Left / Right";e.scenes[0].parts[0]={0,59,12,1,true};e.scenes[0].parts[1]={60,127,0,.3f,true};
    e.scenes[1].parts[0]={60,127,-12,.8f,true};e.scenes[1].parts[2]={0,127,7,.6f,true};
    e.prepare(48000,64);auto tick=[&](int n=1){for(int i=0;i<n;++i)e.render(out.getArrayOfWritePointers(),2,64);};
    auto has=[&](int slot,int pitch,bool on){for(auto meta:e.slots[(size_t)slot].midi){auto m=meta.getMessage();if((on?m.isNoteOn():m.isNoteOff())&&m.getNoteNumber()==pitch)return true;}return false;};
    e.control(SelectScene,0);e.midi(0x90,59,100);e.midi(0x90,60,100);tick();
    check(has(0,71,true)&&!has(0,72,true)&&has(1,60,true)&&!has(1,59,true)&&!has(2,64,true),"Split includes both boundary keys exactly once and filters before transpose");
    check(e.activeScene==0&&e.slots[1].gain==.3f,"Scene recalls enabled parts, transpose and individual gain together");
    e.midi(0xb0,64,127);e.control(SelectScene,1);tick();
    check(e.slots[1].running&&e.slots[0].transpose==-12&&e.slots[2].sceneEnabled,"Scene switching allows a release tail on the previous instrument");
    e.midi(0x80,59,0);tick();check(has(0,71,false),"Note-off reaches the old transposed pitch after scene and range changes");
    e.midi(0x90,60,100);tick();check(has(0,48,true)&&has(2,67,true),"Overlapping key zones layer multiple instruments in the next scene");
    e.midi(0x80,60,0);e.midi(0xb0,64,0);tick();check(has(0,48,false)&&has(2,67,false),"Layered split notes release all remembered owners");
    e.control(Panic);tick();check(e.activeScene==1&&e.slots[2].sceneEnabled&&out.getMagnitude(0,64)==0,"Panic silences notes while preserving the selected combination");
    e.control(Select,0);tick();check(e.activeScene==-1&&e.slots[2].layer&&e.slots[2].transpose==5&&e.slots[0].keyHigh==127,"Single-instrument selection restores the previous standalone layer configuration");
    e.control(NextScene);tick();bool first=e.activeScene==0;e.control(NextScene);tick();bool second=e.activeScene==1;e.control(NextScene);tick();bool wrapped=e.activeScene==0;e.control(PreviousScene);tick();check(first&&second&&wrapped&&e.activeScene==1,"Next and previous combination skip empty groups and wrap");
    Event binding;binding.type=Event::Bind;binding.mapping={9,10,53,SelectScene,0,0,0,1};e.post(binding);e.midi(0x99,53,100);tick();check(e.activeScene==0&&!has(0,65,true),"MIDI pad recalls a combination without sounding its trigger note");
    e.rangeLearning=true;e.rangeLearned=-1;e.midi(0x90,66,100);tick();check(e.rangeLearned==66&&!has(1,66,true),"Split range learn captures a real key and consumes its note-on");e.midi(0x80,66,0);tick();
    Event edit;edit.type=Event::SceneEdit;edit.a=0;edit.b=0;edit.part={40,70,-3,.5f,true};e.post(edit);tick();check(e.slots[0].keyLow==40&&e.slots[0].keyHigh==70&&e.slots[0].transpose==-3,"Editing the active combination updates routing without reloading a plugin");
    edit.part={80,20,0,1,true};e.post(edit);tick();check(e.slots[0].keyHigh==70,"Invalid inverted split range is rejected by the audio thread");
    std::array<Scene,sceneCount> roundTrip;check(readScenes(scenesJson(e.scenes),roundTrip)&&roundTrip[0].parts[0].low==40&&roundTrip[1].parts[2].transpose==7,"All combinations serialize and restore independent part settings");
    auto json=scenesJson(e.scenes);(*(*json.getArray())[0]["parts"].getArray())[0].getDynamicObject()->setProperty("low",128);check(!readScenes(json,roundTrip),"Malformed scene data is rejected transactionally");
    auto* plugin=e.slots[0].plugin.get();bool finite=true;
    for(int i=0;i<1000;++i){e.control(SelectScene,i%2);if(i%8==0)e.midi(0x90,65,100);if(i%8==4)e.midi(0x80,65,0);tick();for(int n=0;n<64;++n)finite&=std::isfinite(out.getSample(0,n));}
    check(finite&&e.slots[0].plugin.get()==plugin&&e.dropped==0,"1000 combination switches retain preloaded plugins and finite output");
    e.control(Panic);e.control(SelectScene,0);e.control(PerformanceTranspose,0,2);e.midi(0x90,59,100);tick();
    check(has(0,58,true)&&e.slots[0].keyLow==40,"Global transpose adds to part transpose and leaves physical split boundaries unchanged");
    e.control(PerformanceTranspose,0,-5);e.midi(0x80,59,0);tick();check(has(0,58,false),"Changing global transpose while holding a key still releases its original pitch");
    e.control(PerformanceTranspose,0,5);e.midi(0x90,60,100);tick();check(has(0,62,true)&&has(1,65,true),"Global transpose applies consistently to layered parts");
    e.control(PerformanceTranspose,0,100);tick();check(e.performanceTranspose==24,"Global transpose clamps to the supported range");
    e.control(PerformanceTranspose,0,0);e.control(Panic);tick();e.audioDeviceStopped();
}
}
