#include "Engine.h"
#include <cmath>
#include <algorithm>
namespace stage {
namespace {
class ReuseProbe final: public juce::AudioPluginInstance {
    std::unique_ptr<juce::AudioPluginInstance> synth=makeTestSynth();
public:
    int prepares=0,releases=0,calls=0,stateReads=0,stateWrites=0,allSoundOffs=0;
    std::array<int,128> noteOns{},noteOffs{};
    ReuseProbe():AudioPluginInstance(BusesProperties().withOutput("Stereo",juce::AudioChannelSet::stereo(),true)){}
    const juce::String getName()const override{return "Reuse instrumentation synth";}
    void fillInPluginDescription(juce::PluginDescription& d)const override{synth->fillInPluginDescription(d);}
    void prepareToPlay(double sr,int n)override{++prepares;setRateAndBufferSizeDetails(sr,n);synth->setRateAndBufferSizeDetails(sr,n);synth->prepareToPlay(sr,n);}
    void releaseResources()override{++releases;synth->releaseResources();}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi)override{++calls;for(auto event:midi){auto message=event.getMessage();if(message.isNoteOn())++noteOns[(size_t)message.getNoteNumber()];if(message.isNoteOff())++noteOffs[(size_t)message.getNoteNumber()];if(message.isAllSoundOff())++allSoundOffs;}synth->processBlock(b,midi);}
    bool acceptsMidi()const override{return true;}bool producesMidi()const override{return false;}double getTailLengthSeconds()const override{return 5;}
    bool hasEditor()const override{return false;}juce::AudioProcessorEditor* createEditor()override{return nullptr;}
    int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return "Default";}void changeProgramName(int,const juce::String&)override{}
    void getStateInformation(juce::MemoryBlock& data)override{++stateReads;synth->getStateInformation(data);}void setStateInformation(const void* data,int n)override{++stateWrites;synth->setStateInformation(data,n);}
};
}
int runReuseTests(const juce::File& report){
    using namespace juce;int failures=0;Array<var> results;
    auto check=[&](bool ok,String title){auto* item=new DynamicObject();item->setProperty("test",title);item->setProperty("passed",ok);results.add(var(item));if(!ok)++failures;};
    auto owner=std::make_unique<Engine>();auto& e=*owner;std::array<ReuseProbe*,slotCount> probes{};
    for(int i=0;i<slotCount;++i){auto source=std::make_unique<ReuseProbe>();probes[(size_t)i]=source.get();e.slots[(size_t)i].plugin=std::move(source);}
    for(int i=0;i<3;++i){e.scenes[(size_t)i].registered=true;e.scenes[(size_t)i].parts[0]={0,127,i==0?0:i==1?12:-5,1,true,i==1,true};}
    e.prepare(48000,128);AudioBuffer<float> out(2,128);auto tick=[&](int count=1){for(int n=0;n<count;++n)e.render(out.getArrayOfWritePointers(),2,128);};
    tick(32);bool idle=true;for(int i=1;i<slotCount;++i)idle&=probes[(size_t)i]->calls==0;
    check(idle&&probes[0]->calls==32,"Seven unselected preloaded instruments skip processing while selected source remains ready");
    check(std::all_of(probes.begin(),probes.end(),[](auto* p){return p->prepares==1;}),"Preloaded instrument instances prepare once before performance");
    e.control(SelectScene,0);e.midi(0x90,60,100);e.midi(0xb0,64,127);tick(20);e.midi(0x80,60,0);tick();
    check(e.pedalDown&&e.slots[0].pedalDown&&out.getMagnitude(0,128)>.001f,"Shared source actually sustains a released physical key before switching");
    auto* identity=e.slots[0].plugin.get();int oldAllOff=probes[0]->allSoundOffs;e.control(SelectScene,1);tick();
    check(e.slots[0].plugin.get()==identity&&probes[0]->prepares==1&&probes[0]->allSoundOffs>oldAllOff,"Transpose variant reuses plugin and clears old sustained voices without prepare");
    check(!e.slots[0].pedalDown&&e.slots[0].ignorePedal&&out.getMagnitude(0,128)==0,"Pedal-disabled variant is silent after shared old voice is released");
    e.midi(0x90,60,100);tick(8);check(probes[0]->noteOns[72]>0&&out.getMagnitude(0,128)>.001f,"New key in reused +12 variant reaches the same instance at pitch 72");
    e.control(SelectScene,2);tick();int releases72=probes[0]->noteOffs[72];e.midi(0x80,60,0);tick();
    check(probes[0]->noteOffs[72]>releases72,"Physical key-up after a second variant switch still releases its original transposed target");
    e.midi(0xb0,64,0);e.midi(0x90,60,100);tick(8);check(probes[0]->noteOns[55]>0,"Same shared instance receives independent -5 variant pitch");e.midi(0x80,60,0);tick();
    bool finite=true,pointers=true;std::array<double,1000> switchTimes{};float* storage=e.slots[0].buffer.getWritePointer(0);
    auto started=Time::getHighResolutionTicks();
    for(int n=0;n<1000;++n){auto begin=Time::getHighResolutionTicks();e.control(SelectScene,n%3);e.midi(0x90,60+n%12,90);tick();e.midi(0x80,60+n%12,0);tick();switchTimes[(size_t)n]=Time::highResolutionTicksToSeconds(Time::getHighResolutionTicks()-begin)*1000000.0;pointers&=e.slots[0].plugin.get()==identity&&e.slots[0].buffer.getWritePointer(0)==storage;for(int c=0;c<2;++c)for(int k=0;k<128;++k)finite&=std::isfinite(out.getSample(c,k));}
    auto wallMs=Time::highResolutionTicksToSeconds(Time::getHighResolutionTicks()-started)*1000.0;
    check(pointers,"1000 transposed scene switches preserve plugin identity and preallocated source audio storage");
    check(probes[0]->prepares==1&&probes[0]->releases==0&&probes[0]->stateReads==0&&probes[0]->stateWrites==0,"Repeated shared sound switching never prepares, releases, serializes or reloads its preset");
    check(finite&&e.dropped==0,"1000 switches with key input keep finite output and do not overflow event queue");
    e.control(Panic);tick();check(out.getMagnitude(0,128)==0&&!e.pedalDown,"Panic after repeated reuse clears audio and sustain");
    e.control(Select,1);tick(4000);check(!e.slots[0].running&&probes[1]->calls>0,"Switching to a different instance eventually retires shared source after bounded tail");int retiredCalls=probes[0]->calls;tick(100);check(probes[0]->calls==retiredCalls,"Retired shared source stops consuming process calls");
    e.audioDeviceStopped();check(std::all_of(probes.begin(),probes.end(),[](auto* p){return p->releases==1;}),"Device stop releases each preloaded instance once");
    std::sort(switchTimes.begin(),switchTimes.end());auto* root=new DynamicObject();root->setProperty("failures",failures);root->setProperty("results",results);root->setProperty("opensAudioDevice",false);root->setProperty("physicalMidiOrLatencyVerified",false);root->setProperty("benchmarkDescription","Offline built-in synth workload: each timed iteration queues one scene switch and renders two 128-frame blocks at 48 kHz. These are local software timings, not ASIO or physical MIDI latency.");root->setProperty("switches",1000);root->setProperty("sampleRate",48000);root->setProperty("blockSamples",128);root->setProperty("offlineAudioSeconds",2000.0*128/48000);root->setProperty("offlineWallMs",wallMs);root->setProperty("switchAndTwoBlocksMedianUs",switchTimes[500]);root->setProperty("switchAndTwoBlocksP95Us",switchTimes[950]);root->setProperty("switchAndTwoBlocksMaximumUs",switchTimes[999]);root->setProperty("sharedPluginInstances",1);root->setProperty("sharedInstrumentPrepareCalls",probes[0]->prepares);report.getParentDirectory().createDirectory();report.replaceWithText(JSON::toString(var(root)));return failures?1:0;
}
}
