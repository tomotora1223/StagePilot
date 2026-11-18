#include "Engine.h"
#include <thread>
#include <vector>
#include "SequenceTests.h"
#include "SceneTests.h"
#include "PedalTests.h"
#include "MeterTests.h"
#include "PreviewTests.h"
#include "PluginHosting.h"
#include "InstrumentBrowser.h"

namespace stage {
int runTests(const juce::File& report,const juce::String& pluginPath) {
    juce::Array<juce::var> results;int failed=0;
    auto check=[&](bool ok,const juce::String& name) {
        auto* o=new juce::DynamicObject();o->setProperty("test",name);o->setProperty("passed",ok);results.add(juce::var(o));if(!ok)++failed;
        report.getParentDirectory().createDirectory();report.replaceWithText(juce::JSON::toString(juce::var(results)));
    };
    {
        Queue<int,1024> q;constexpr int total=20000;std::atomic<int> finished{0};std::atomic<int64_t> sum{0};
        std::vector<std::thread> writers;
        for(int t=0;t<4;++t)writers.emplace_back([&,t]{for(int i=1;i<=total;++i){int x=t*total+i;while(!q.push(x))std::this_thread::yield();}++finished;});
        int received=0;int value=0;
        while(finished.load()<4||received<total*4){if(q.pop(value)){sum+=value;++received;}else std::this_thread::yield();}
        for(auto& t:writers)t.join();
        check(received==total*4&&sum.load()==(int64_t)(total*4)*(total*4+1)/2,"Concurrent queue: 4 producers, 80000 events, no loss");
    }
    Engine e;juce::AudioBuffer<float> out(2,64);
    for(int i=0;i<3;++i)e.slots[(size_t)i].plugin=makeTestSynth();
    e.prepare(48000,64);
    auto tick=[&](int blocks=1){for(int n=0;n<blocks;++n)e.render(out.getArrayOfWritePointers(),2,64);};
    auto peak=[&]{return out.getMagnitude(0,64);};
    e.midi(0x90,60,100);tick(20);check(peak()>0.01f,"MIDI note produces stereo audio");
    e.control(Select,1);tick();check(e.selected==1&&e.slots[0].running,"Tail switch keeps old instrument running");
    e.tailSeconds=0.1f;e.midi(0x80,60,0);tick(160);check(!e.slots[0].running,"Note off reaches old slot and tail retires");
    e.midi(0x90,62,100);e.midi(0xb0,64,127);tick(10);e.control(Select,2);tick();e.midi(0x80,62,0);tick(160);
    check(!e.slots[1].running&&e.slots[2].pedalDown,"Held sustain cannot keep the previous instrument running beyond its tail limit");e.midi(0xb0,64,0);tick(160);check(!e.slots[1].running,"Pedal up releases retired slot");
    e.midi(0x90,67,100);tick(20);e.fadeSeconds=0.01f;e.control(FadeOut);tick(20);check(peak()<0.000001f,"Master fade reaches exact silence");
    e.control(Restore);tick(30);check(peak()>0.01f,"Restore fades back to saved master gain");
    e.control(Panic);tick();check(peak()==0,"Panic silences held notes on every channel");
    Event binding;binding.type=Event::Bind;binding.a=0;binding.mapping={11,2,21,Parameter,2,0,0.2f,0.8f};e.post(binding);e.midi(0xb0,21,127);tick();
    check(std::abs(e.slots[2].plugin->getParameters()[0]->getValue()-0.3f)<0.001f,"Mapping respects MIDI channel filter");
    e.midi(0xb1,21,127);tick();check(std::abs(e.slots[2].plugin->getParameters()[0]->getValue()-0.8f)<0.001f,"CC maps to normalized VST parameter with range");
    binding.a=1;binding.mapping={9,0,36,Select,0,0,0,1};e.post(binding);e.midi(0x99,36,100);tick(10);
    check(e.selected==0&&peak()==0,"Mapped drum pad switches slot without playing its note");e.midi(0x89,36,0);tick();
    e.learning=true;e.midi(0x90,72,110);tick();check(e.learned.load()!=0&&peak()==0,"MIDI learn captures and consumes note");e.midi(0x80,72,0);tick();
    e.mode=1;e.transitionSeconds=0.02f;e.midi(0x90,60,100);tick(10);e.control(Select,1);e.midi(0x90,64,100);tick(40);
    check(!e.slots[0].running&&e.slots[1].running&&peak()>0,"Crossfade retires old slot and plays new slot");
    e.mode=2;e.control(Select,2);tick(10);check(!e.slots[1].running,"Fast cut completes within a short ramp");
    e.control(Panic);tick();
    for(int i=0;i<5000;++i)e.midi(0x90,60,100);tick();check(e.dropped>0&&peak()==0,"Queue overflow triggers panic instead of hanging notes");
    e.control(Select,7);tick();check(e.selected==2,"Selecting an empty slot does not interrupt current selection");
    juce::MemoryBlock state;e.slots[2].plugin->getStateInformation(state);auto restored=makeTestSynth();restored->setStateInformation(state.getData(),(int)state.getSize());
    check(std::abs(restored->getParameters()[0]->getValue()-0.8f)<0.001f,"Instrument state round trip preserves parameters");
    bool finite=true;e.mode=0;e.tailSeconds=0.1f;
    const auto start=juce::Time::getMillisecondCounterHiRes();
    for(int n=0;n<3000;++n){if(n%30==0){e.control(Select,(n/30)%3);e.midi(0x90,60,100);}if(n%30==20)e.midi(0x80,60,0);tick();for(int c=0;c<2;++c)for(int s=0;s<64;++s)finite&=std::isfinite(out.getSample(c,s));}
    auto elapsed=juce::Time::getMillisecondCounterHiRes()-start;
    check(finite,"3000 blocks of repeated switching remain finite");
    {
        auto layered=std::make_unique<Engine>();auto& x=*layered;
        for(int i=0;i<3;++i)x.slots[(size_t)i].plugin=makeTestSynth();x.prepare(48000,64);
        auto tickLayer=[&](int blocks=1){for(int n=0;n<blocks;++n)x.render(out.getArrayOfWritePointers(),2,64);};
        auto hasMidi=[&](int slot,bool on,int note){for(auto event:x.slots[(size_t)slot].midi){auto m=event.getMessage();if((on?m.isNoteOn():m.isNoteOff())&&m.getNoteNumber()==note)return true;}return false;};
        x.control(SetLayer,1,1);x.control(SetTranspose,1,12);x.midi(0x90,60,100);tickLayer();
        check(hasMidi(0,true,60)&&hasMidi(1,true,72)&&!hasMidi(2,true,60),"Layer routes a key to primary plus pinned instrument with independent transpose");
        x.control(SetTranspose,1,-12);x.midi(0x80,60,0);tickLayer();
        check(hasMidi(1,false,72)&&hasMidi(0,false,60),"Changing transpose while held releases the original pitches");
        x.control(Select,2);x.midi(0x90,64,110);tickLayer();
        check(hasMidi(2,true,64)&&hasMidi(1,true,52),"Pinned layer follows selection of a different primary instrument");
        x.control(Select,1);x.midi(0x90,65,100);tickLayer();int count=0;for(auto event:x.slots[1].midi)if(event.getMessage().isNoteOn())++count;
        check(count==1,"Selecting the pinned layer does not double-trigger notes");
        x.control(Panic);tickLayer();x.control(SetTranspose,1,24);x.midi(0x90,120,100);tickLayer();bool any=false;for(auto event:x.slots[1].midi)any|=event.getMessage().isNoteOn();
        check(!any,"Out-of-range transposed notes are ignored without wrapping");
        x.control(SetTranspose,1,0);x.control(Select,0);x.midi(0xb0,64,127);x.midi(0x90,60,100);tickLayer();
        x.control(SetLayer,1,0);x.midi(0x80,60,0);tickLayer(100);check(x.slots[1].running,"Removing a layer allows a bounded release tail");
        x.tailSeconds=0.1f;x.midi(0xb0,64,0);tickLayer(200);check(!x.slots[1].running,"Pedal release retires an unpinned layer");
        x.control(SetLayer,1,1);x.control(Panic);tickLayer();x.midi(0x90,60,100);tickLayer();check(hasMidi(0,true,60)&&hasMidi(1,true,60),"Panic preserves the layer configuration for subsequent playing");
        x.audioDeviceStopped();
    }
    {
        auto sound=std::make_unique<Engine>();auto& x=*sound;
        x.clips[0].data=makeDemoClip(0);x.clips[1].data=makeDemoClip(1);x.prepare(48000,64);
        auto tickSound=[&](int blocks=1){for(int n=0;n<blocks;++n)x.render(out.getArrayOfWritePointers(),2,64);};
        x.control(ClipPlay,0,1);tickSound(10);check(x.clips[0].playing&&peak()>0,"Sample effect plays without a VST instrument");
        tickSound(400);check(!x.clips[0].playing&&peak()==0,"One-shot effect stops at the end of its audio");
        x.clips[0].loop=true;x.control(ClipPlay,0,1);x.control(ClipPlay,1,1);tickSound(430);
        check(x.clips[0].playing&&!x.clips[1].playing,"Effects can play together and looping is per pad");
        x.control(ClipStop,0);tickSound(10);check(!x.clips[0].playing&&peak()==0,"Stopping a looping effect completes its short fade");
        Event b;b.type=Event::Bind;b.mapping={9,10,48,ClipPlay,0,0,0,1};x.post(b);x.midi(0x99,48,100);tickSound(10);x.midi(0x89,48,0);tickSound();
        check(x.clips[0].playing,"Mapped keyboard note triggers an effect and key release does not truncate it");
        x.control(ClipStopAll);tickSound(10);check(!x.clips[0].playing&&!x.clips[1].playing,"Stop-all-effects silences the sample bank");
        x.control(ClipPlay,0,1);tickSound(5);x.fadeSeconds=0.01f;x.control(FadeOut);tickSound(20);check(peak()==0&&x.clips[0].playing,"Master fade includes effects while retaining playback position");
        x.control(Restore);tickSound(30);check(peak()>0,"Master restore resumes audible effect output");
        x.control(Panic);tickSound();check(!x.clips[0].playing&&peak()==0,"Panic stops effects immediately");
        x.clips[0].data.audio.setSize(2,3200);for(int c=0;c<2;++c)for(int n=0;n<3200;++n)x.clips[0].data.audio.setSample(c,n,0.2f);
        x.clips[0].data.sampleRate=32000;x.clips[0].loop=false;x.control(ClipPlay,0,1);tickSound(60);bool beforeEnd=x.clips[0].playing;tickSound(20);
        check(beforeEnd&&!x.clips[0].playing,"Sample rate conversion preserves duration at 32 kHz source / 48 kHz output");
        x.clips[0].loop=true;for(int i=0;i<200;++i){x.control(ClipPlay,0,1);tickSound();}bool finite=true;for(int n=0;n<64;++n)finite&=std::isfinite(out.getSample(0,n));
        check(finite,"Rapid sample retriggers remain finite and bounded");
        x.slots[0].plugin=makeTestSynth();x.prepare(48000,64);x.midi(0x90,60,100);x.control(ClipPlay,0,1);tickSound(20);x.control(ClipStopAll);tickSound(20);
        check(!x.clips[0].playing&&peak()>0.01f,"Stopping effects leaves the live instrument playing");
        x.control(Panic);tickSound();x.clips[0].data=makeDemoClip(0);x.clips[0].loop=false;
        b.a=1;b.mapping={11,1,20,ClipPlay,0,0,0,1};x.post(b);x.midi(0xb0,20,127);tickSound(20);double position=0;for(auto& v:x.clips[0].voices)if(v.active)position=v.position;
        x.midi(0xb0,20,0);tickSound();double after=0;for(auto& v:x.clips[0].voices)if(v.active)after=v.position;
        check(after>position,"Releasing a mapped CC trigger does not retrigger the effect");
        x.audioDeviceStopped();
    }
    sequenceTests(check);
    sceneTests(check);
    pedalTests(check);
    meterTests(check);
    previewTests(check);
    {
        auto root=report.getSiblingFile("discovery-"+juce::Uuid().toString());
        auto bundle=root.getChildFile("Nested/Piano.vst3");bundle.getChildFile("Contents/x86_64-win").createDirectory();
        bundle.getChildFile("Contents/x86_64-win/helper.dll").replaceWithText("helper");
        root.getChildFile("not-a-plugin.dll").replaceWithText("invalid");
        auto found=findPluginCandidates({root.getFullPathName(),bundle.getParentDirectory().getFullPathName()},[]{return false;});
        check(found.size()==1&&found[0]==bundle,"Discovery de-duplicates overlapping roots and treats VST3 bundles as one plugin");
        check(findPluginCandidates({root.getFullPathName()},[]{return true;}).isEmpty(),"Discovery cancellation stops directory traversal");
        root.deleteRecursively();
    }
    {
        auto file=report.getSiblingFile("vst2-header-test.dll");
        auto header=[&](int machine){juce::MemoryOutputStream data;data.writeShort(0x5a4d);data.writeRepeatedByte(0,58);data.writeInt(128);data.writeRepeatedByte(0,64);data.writeInt(0x4550);data.writeShort((short)machine);data.writeRepeatedByte(0,18);file.replaceWithData(data.getData(),data.getDataSize());};
        header(0x14c);check(checkPluginFile(file).contains("32bit"),"VST2 preflight explains why 32-bit DLLs cannot load");
        header(0x8664);check(checkPluginFile(file).isEmpty(),"VST2 preflight accepts an x64 PE header without executing the DLL");
        file.replaceWithText("invalid");check(checkPluginFile(file).isNotEmpty(),"Malformed DLL is rejected before scanning");file.deleteFile();
    }
    auto* root=new juce::DynamicObject();root->setProperty("offlineRenderAudioSeconds",4.0);root->setProperty("offlineRenderWallMs",elapsed);
    root->setProperty("juceVersion",juce::SystemStats::getJUCEVersion());
    if(pluginPath.isNotEmpty()) {
        juce::AudioPluginFormatManager fm;addPluginFormats(fm);juce::OwnedArray<juce::PluginDescription> types;
        scanPlugin(juce::File(pluginPath),types);check(!types.isEmpty(),"Installed VST discovery");
        if(!types.isEmpty()) {
            juce::String error;auto p=fm.createPluginInstance(*types[0],48000,64,error);check(p!=nullptr,"Installed VST instantiation: "+error);
            if(p){Engine vst;vst.slots[0].plugin=std::move(p);vst.slots[0].plugin->enableAllBuses();vst.prepare(48000,64);
                vst.midi(0x90,60,100);float max=0;bool valid=true;
                for(int n=0;n<750;++n){vst.render(out.getArrayOfWritePointers(),2,64);max=juce::jmax(max,out.getMagnitude(0,64));for(int s=0;s<64;++s)valid&=std::isfinite(out.getSample(0,s));}
                check(valid&&max>0,"Installed VST renders MIDI to nonzero finite audio");root->setProperty("probePlugin",types[0]->name);root->setProperty("probePeak",max);
                vst.slots[1].plugin=makeTestSynth();vst.tailSeconds=.1f;vst.prepare(48000,64);vst.midi(0xb0,64,127);vst.midi(0x90,60,100);for(int n=0;n<20;++n)vst.render(out.getArrayOfWritePointers(),2,64);vst.control(Select,1);for(int n=0;n<160;++n)vst.render(out.getArrayOfWritePointers(),2,64);check(!vst.slots[0].running&&!vst.slots[0].pedalDown&&out.getMagnitude(0,64)==0,"Installed VST switches to silence with sustain and key still held");
                vst.control(Panic);vst.scenes[0].parts[0]={0,127,0,1,true,false};vst.scenes[1].parts[0]={0,127,12,1,true,false};vst.control(SelectScene,0);vst.midi(0xb0,64,127);vst.midi(0x90,60,100);for(int n=0;n<20;++n)vst.render(out.getArrayOfWritePointers(),2,64);vst.midi(0x80,60,0);vst.control(SelectScene,1);for(int n=0;n<160;++n)vst.render(out.getArrayOfWritePointers(),2,64);root->setProperty("sharedVstPeakAfter213ms",out.getMagnitude(0,64));for(int n=0;n<1500;++n)vst.render(out.getArrayOfWritePointers(),2,64);root->setProperty("sharedVstPeakAfter2213ms",out.getMagnitude(0,64));check(vst.slots[0].running&&vst.slots[0].pedalDown&&out.getMagnitude(0,64)<.00001f,"Installed VST clears old sustain voices when reused by another scene");
                vst.audioDeviceStopped();
            }
        }
    }
    e.audioDeviceStopped();root->setProperty("failures",failed);root->setProperty("results",results);
    report.getParentDirectory().createDirectory();report.replaceWithText(juce::JSON::toString(juce::var(root)));
    return failed?1:0;
}
}
