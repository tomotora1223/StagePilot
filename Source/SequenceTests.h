#pragma once
#include "Engine.h"
namespace stage {
inline void sequenceTests(const std::function<void(bool,const juce::String&)>& check) {
    auto instance=std::make_unique<Engine>();auto& e=*instance;auto& seq=e.sequence;
    juce::AudioBuffer<float> out(2,64);auto tick=[&](int blocks=1){for(int i=0;i<blocks;++i)e.render(out.getArrayOfWritePointers(),2,64);};
    seq.pattern.bars=1;seq.pattern.bpm=137;seq.pattern.loop=false;
    seq.pattern.notes[0]={60,1,1,100,true};seq.pattern.notes[1]={60,2,1,80,true};
    e.prepare(48000,64);e.control(SequencePlay);
    std::vector<int64_t> ons,offs;float maximum=0;
    for(int64_t at=0;at<90000;at+=64){tick();maximum=juce::jmax(maximum,out.getMagnitude(0,64));for(auto metadata:seq.midi){auto m=metadata.getMessage();if(m.isNoteOn())ons.push_back(at+metadata.samplePosition);if(m.isNoteOff())offs.push_back(at+metadata.samplePosition);}}
    check(ons==std::vector<int64_t>{5255,10511}&&offs==std::vector<int64_t>{10511,15766},"Piano roll schedules adjacent notes at exact sample offsets at 137 BPM");
    check(maximum>0&&!seq.playing,"Piano roll renders sound and one-shot transport stops at the bar boundary");
    tick(2400);check(!seq.sounding&&out.getMagnitude(0,64)==0,"Sequence tail fades out and idle instrument processing stops");
    seq.pattern.bpm=120;seq.pattern.loop=true;seq.pattern.notes.fill({});seq.pattern.notes[0]={67,0,16,100,true};e.prepare(48000,64);e.control(SequencePlay);ons.clear();offs.clear();
    for(int64_t at=0;at<193000;at+=64){tick();for(auto m:seq.midi){if(m.getMessage().isNoteOn())ons.push_back(at+m.samplePosition);if(m.getMessage().isNoteOff())offs.push_back(at+m.samplePosition);}}
    check(ons==std::vector<int64_t>{0,96000,192000}&&offs==std::vector<int64_t>{96000,192000},"Loop boundaries release then retrigger full-bar notes without drift");
    e.control(SequencePlay);tick();check(seq.position>0&&seq.position<1&&seq.lit[67],"Restart returns the sequencer to the first step");
    e.control(Panic);tick();check(!seq.playing&&!seq.lit[67]&&out.getMagnitude(0,64)==0,"Global panic immediately silences sequence and clears key indicators");
    e.slots[0].plugin=makeTestSynth();e.prepare(48000,64);e.midi(0x90,67,100);e.control(SequencePlay);tick(20);e.control(SequenceStop);tick();
    bool releasedLive=false;for(auto m:e.slots[0].midi)releasedLive|=m.getMessage().isNoteOff()||m.getMessage().isAllNotesOff();tick(2400);
    check(!releasedLive&&e.slots[0].peak>0.01f&&!seq.sounding,"Stopping the sequence leaves the same live keyboard pitch held");
    e.control(Panic);tick();Event preview;preview.type=Event::SequencePreview;preview.a=72;preview.b=100;e.post(preview);tick(20);
    check(seq.lit[72]&&out.getMagnitude(0,64)>0,"On-screen piano preview produces sound with no MIDI device");tick(2700);
    check(!seq.lit[72]&&!seq.sounding,"Preview notes release automatically and return to idle");
    e.control(SequencePlay);tick();Event edit;edit.type=Event::SequenceEdit;edit.a=0;edit.note={64,2,4,75,true};e.post(edit);tick();
    check(!seq.playing&&!seq.lit[67]&&seq.pattern.notes[0].pitch==64,"Editing a playing note stops transport and releases the previous pitch");
    auto encoded=Sequencer::toJson(seq.pattern);Pattern decoded;
    check(Sequencer::fromJson(encoded,decoded)&&decoded.notes[0].pitch==64&&decoded.notes[0].velocity==75&&decoded.loop,"Pattern JSON round trip preserves position, length, velocity and loop");
    auto invalid=decoded;invalid.notes[1]={64,3,1,100,true};check(!Sequencer::valid(invalid),"Overlapping notes of the same pitch are rejected");invalid=decoded;invalid.bpm=0;check(!Sequencer::valid(invalid),"Invalid tempo is rejected before it reaches the audio scheduler");
    e.control(SequencePlay);tick();e.fadeSeconds=.01f;e.control(FadeOut);tick(20);check(seq.playing&&out.getMagnitude(0,64)==0,"Master fade includes sequencer output");
    e.control(Restore);tick(20);e.control(Panic);tick();
    Event binding;binding.type=Event::Bind;binding.mapping={9,10,56,SequencePlay,0,0,0,1};e.post(binding);e.midi(0x99,56,100);tick();
    check(seq.playing,"MIDI pad can start the piano-roll phrase");binding.a=1;binding.mapping={9,10,57,SequenceStop,0,0,0,1};e.post(binding);e.midi(0x99,57,100);tick();check(!seq.playing,"MIDI pad can stop the piano-roll phrase");
    e.control(Panic);e.midi(0x90,60,100);e.midi(0x91,60,100);tick();e.midi(0x80,60,0);tick();
    check(e.inputKeys[60]==2,"Piano input display retains a pitch held on another MIDI channel");e.control(Panic);tick();check(e.inputKeys[60]==0,"Panic clears the live MIDI keyboard display");
    e.audioDeviceStopped();
    auto synth=makeTestSynth();synth->prepareToPlay(48000,64);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,60,(juce::uint8)100),32);synth->processBlock(out,midi);
    check(out.getMagnitude(0,32)==0&&out.getMagnitude(33,31)>0,"Internal synth honours note sample offsets instead of playing at block start");
    // A block containing both ends catches note ownership errors hidden by small ASIO buffers.
    seq.pattern.notes.fill({});seq.pattern.notes[0]={60,0,1,100,true};seq.pattern.bpm=240;seq.pattern.bars=1;seq.pattern.loop=false;seq.prepare(48000,16384);
    juce::AudioBuffer<float> large(2,16384);seq.start();seq.render(large.getArrayOfWritePointers(),2,16384);bool sawOff=false;
    for(auto m:seq.midi)if(m.getMessage().isNoteOff()&&m.samplePosition==3000)sawOff=true;
    check(sawOff&&!seq.lit[60],"Large block schedules both note-on and note-off in chronological order");
}
}
