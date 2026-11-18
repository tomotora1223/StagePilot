#pragma once
#include "Engine.h"
namespace stage {
inline void previewTests(const std::function<void(bool,const juce::String&)>& check){
    auto instance=std::make_unique<Engine>();auto& e=*instance;juce::AudioBuffer<float> out(2,64);
    for(int i=0;i<3;++i){e.slots[(size_t)i].plugin=makeTestSynth();e.slots[(size_t)i].plugin->getParameters()[1]->setValueNotifyingHost(.005f);}e.prepare(48000,64);
    auto tick=[&](int count=1){for(int n=0;n<count;++n)e.render(out.getArrayOfWritePointers(),2,64);};
    auto noteEvent=[&](int slot,int note,bool on,int channel=0){for(auto m:e.slots[(size_t)slot].midi){auto v=m.getMessage();if(v.getNoteNumber()==note&&(on?v.isNoteOn():v.isNoteOff())&&(channel==0||v.getChannel()==channel))return true;}return false;};
    e.bindings[0]={9,0,60,Select,2,0,0,1};e.learning=true;e.rangeLearning=true;e.preview(60);tick();
    check(e.selected==0&&e.learning&&e.rangeLearning&&e.learned==0&&e.rangeLearned==-1&&noteEvent(0,60,true),"Mouse audition bypasses MIDI mappings and learn without switching the instrument");
    check(e.previewKey==60&&e.inputKeys[60]==0&&out.getMagnitude(0,64)>0,"Mouse audition produces real audio and has separate keyboard telemetry");
    e.learning=false;e.rangeLearning=false;e.preview(-1,0);tick();check(noteEvent(0,60,false)&&e.previewKey==-1,"Releasing the mouse sends note-off to the preview's original target");tick(800);check(out.getMagnitude(0,64)==0,"A released preview finishes its natural tail and cannot sustain indefinitely");
    e.control(Panic);e.bindings.fill({});e.scenes[0].parts[0]={0,59,-12,.8f,true,true,true};e.scenes[0].parts[1]={0,59,12,.5f,true,false,true};e.scenes[0].parts[2]={60,127,0,1,true,false,true};e.control(SelectScene,0);e.preview(48);tick();
    check(noteEvent(0,36,true)&&noteEvent(1,60,true)&&!noteEvent(2,48,true)&&e.slots[0].meter.read().rawPeak[0]>0&&e.slots[1].meter.read().rawPeak[0]>0,"Audition uses the active split, stacked sources, transpose and per-source gain");
    e.preview(61);tick();check(noteEvent(0,36,false)&&noteEvent(1,60,false)&&noteEvent(2,61,true),"Dragging between keys releases the old layered pitches before playing the new zone");
    e.control(PerformanceTranspose,5);e.preview(-1,0);tick();check(noteEvent(2,61,false),"Changing transpose while auditioning still releases the original pitch");e.control(PerformanceTranspose,0);
    e.control(Panic);e.control(Select,0);e.midi(0x90,60,100);tick();e.preview(60);tick();check(noteEvent(0,60,true,16)&&e.inputKeys[60]!=0,"A preview of a physically held pitch borrows another free MIDI channel");
    e.preview(-1,0);tick();check(noteEvent(0,60,false,16)&&!noteEvent(0,60,false,1)&&e.inputKeys[60]!=0,"Mouse release leaves the same physical keyboard pitch held");e.midi(0x80,60,0);tick(900);check(out.getMagnitude(0,64)==0,"Physical and preview voices both stop after their independent releases");
    e.midi(0xb0,64,127);e.preview(55);tick();check(noteEvent(0,55,true,16)&&e.pedalDown&&e.slots[0].pedalDown,"Audition borrows a pedal-free channel while preserving the physical sustain pedal");e.preview(-1,0);tick(900);check(out.getMagnitude(0,64)==0&&e.pedalDown,"A physical pedal cannot latch the preview voice after mouse release");e.control(Panic);tick();
    e.preview(60);tick();e.midi(0x90,62,100);tick();check(noteEvent(0,60,false,1)&&noteEvent(0,62,true,1)&&e.previewKey==-1,"Physical notes take priority over a borrowed preview channel without hanging notes");e.control(Panic);tick();
    e.preview(60);tick();e.midi(0xb0,64,127);tick();check(noteEvent(0,60,false)&&e.previewKey==-1&&e.pedalDown,"Physical pedal presses release preview notes before enabling sustain");e.control(Panic);tick();
    e.preview(60);tick();e.control(Select,2);tick();check(e.previewKey==-1&&noteEvent(0,60,false),"Changing live instruments releases mouse preview ownership");
    e.preview(60);tick();e.control(SelectScene,0);tick();check(e.previewKey==-1&&noteEvent(2,60,false),"Changing combinations releases mouse preview ownership");e.preview(60);tick();e.control(Panic);tick();check(e.previewKey==-1&&out.getMagnitude(0,64)==0,"Panic clears preview voices and keyboard feedback immediately");
    e.preview(60);tick(7501);check(e.previewKey==-1,"The audio clock bounds a preview if a mouse-up event is lost");e.audioDeviceStopped();check(e.previewKey==-1,"Audio-device stop clears mouse preview telemetry");
    e.scenes[0].parts[1].enabled=false;e.scenes[0].parts[1].included=true;std::array<Scene,sceneCount> restored;check(readScenes(scenesJson(e.scenes),restored)&&restored[0].parts[1].included&&!restored[0].parts[1].enabled,"Muted sources remain members of the saved combination");
}
}
