#pragma once
#include "Engine.h"
namespace stage {
inline void pedalTests(const std::function<void(bool,const juce::String&)>& check){
    auto instance=std::make_unique<Engine>();auto& e=*instance;juce::AudioBuffer<float> out(2,64);
    for(int i=0;i<4;++i)e.slots[(size_t)i].plugin=makeTestSynth();
    e.tailSeconds=.1f;e.transitionSeconds=.02f;e.prepare(48000,64);
    auto tick=[&](int n=1){for(int i=0;i<n;++i)e.render(out.getArrayOfWritePointers(),2,64);};
    auto cc=[&](int slot,int channel,int value){for(auto meta:e.slots[(size_t)slot].midi){auto m=meta.getMessage();if(m.isControllerOfType(64)&&m.getChannel()==channel&&m.getControllerValue()==value)return true;}return false;};
    e.control(SetLayer,1,1);e.control(SetIgnorePedal,1,1);e.midi(0xb0,64,127);e.midi(0x90,60,100);tick(10);
    check(e.slots[0].pedalDown&&!e.slots[1].pedalDown&&e.pedalDown,"Layered instruments independently accept or ignore sustain");
    e.midi(0xb1,64,90);tick();check(cc(0,2,90)&&!cc(1,2,90),"Ignored sustain is filtered on every MIDI channel");
    e.control(SetIgnorePedal,0,1);tick();check(cc(0,1,0)&&cc(0,2,0)&&!e.slots[0].pedalDown,"Disabling pedal while held releases all latched channels immediately");
    e.control(SetIgnorePedal,0,0);tick();check(cc(0,1,127)&&cc(0,2,90)&&e.slots[0].pedalDown,"Enabling pedal inherits the physical input values without waiting for a new press");
    e.midi(0xb0,64,32);tick();check(cc(0,1,32),"Half-pedal controller values are preserved");
    e.control(Select,2);e.control(SetLayer,1,0);tick();
    check(cc(0,2,0)&&e.slots[0].tailActive&&!e.slots[0].pedalDown&&e.slots[2].pedalDown,"Switch releases the previous pedal and exposes its bounded tail separately");
    e.midi(0xb1,64,127);tick();check(!cc(0,2,127)&&cc(2,2,127),"New pedal presses cannot relatch a retiring instrument");
    tick(160);check(!e.slots[0].running&&!e.slots[0].tailActive&&e.slots[0].tailRemaining==0&&!e.slots[1].running&&out.getMagnitude(0,64)==0,"Old instruments become silent with both physical keys and pedal still held");
    for(int mode=0;mode<3;++mode){e.mode=mode;e.control(Panic);e.control(Select,0);e.midi(0xb0,64,127);e.midi(0x90,60,100);tick(5);e.control(Select,2);tick(200);check(!e.slots[0].running&&!e.slots[0].pedalDown&&out.getMagnitude(0,64)==0,"Held-pedal switching stops safely in transition mode "+juce::String(mode));}
    e.mode=1;e.transitionSeconds=.5f;e.control(Panic);e.control(Select,0);e.midi(0x90,60,100);tick(10);e.control(Select,2);tick(100);check(e.slots[0].tailRemaining>.3f,"Crossfade countdown follows its fade duration independently of the release-tail limit");tick(400);e.mode=0;
    e.control(Panic);tick();e.scenes[0].parts[0]={0,59,-12,1,true,true};e.scenes[0].parts[1]={0,59,0,.5f,true,false};e.scenes[0].parts[2]={60,127,0,1,true,false};e.scenes[0].parts[3]={60,127,12,.5f,true,true};
    e.scenes[0].splitPoint=60;e.scenes[1].parts[0]={0,127,0,1,true,false};e.slots[1].ignorePedal=true;
    e.control(SelectScene,0);e.midi(0xb0,64,127);e.midi(0x90,59,100);e.midi(0x90,60,100);tick();
    check(!e.slots[0].pedalDown&&e.slots[1].pedalDown&&e.slots[2].pedalDown&&!e.slots[3].pedalDown,"A split with two layers on each side recalls four independent pedal policies");
    e.midi(0x80,59,0);e.midi(0x80,60,0);tick();e.control(SelectScene,1);tick();check(e.slots[0].pedalDown&&!e.slots[1].pedalDown&&!e.slots[2].pedalDown,"Scene recall reapplies pedal policy on shared and retiring instruments");
    check(e.slots[1].gain==.5f&&e.slots[1].keyHigh==59&&e.slots[2].keyLow==60,"Retiring parts retain their previous gain and key range without a level jump");
    tick(160);check(out.getMagnitude(0,64)==0,"A shared plugin cannot retain previous-scene sustain voices after recall");e.midi(0x90,62,100);tick(10);e.control(SelectScene,1);tick();check(out.getMagnitude(0,64)>0,"Selecting the current scene again leaves its held notes intact");
    Event edit;edit.type=Event::PedalSettings;edit.a=1;edit.b=0;edit.c=1;e.control(SlotGain,0,.25f);e.post(edit);tick();check(e.slots[0].ignorePedal&&!e.slots[0].pedalDown&&e.slots[0].gain==.25f,"Performance pedal control preserves the current live gain");
    e.control(Select,2);tick();check(e.activeScene==-1&&e.slots[1].ignorePedal,"Leaving a scene restores the standalone pedal configuration");
    std::array<Scene,sceneCount> restored;auto json=scenesJson(e.scenes);check(readScenes(json,restored)&&restored[0].parts[0].ignorePedal&&!restored[0].parts[1].ignorePedal&&restored[0].splitPoint==60,"Pedal policies and split boundary serialize independently for each scene");
    for(auto& scene:*json.getArray()){scene.getDynamicObject()->removeProperty("splitPoint");for(auto& part:*scene["parts"].getArray())part.getDynamicObject()->removeProperty("ignorePedal");}
    check(readScenes(json,restored)&&!restored[0].parts[0].ignorePedal&&restored[0].splitPoint==60,"Older scene files default to enabled pedal and middle-C split boundary");
    (*json.getArray())[0].getDynamicObject()->setProperty("splitPoint",128);check(!readScenes(json,restored),"Malformed split boundaries are rejected transactionally");
    e.audioDeviceStopped();check(!e.pedalDown&&!e.slots[0].tailActive,"Stopping the audio device clears pedal and tail telemetry");
}
}
