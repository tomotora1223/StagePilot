#include "Engine.h"
#include "PluginHosting.h"
#include <cmath>
#include <limits>
namespace stage {
namespace {
class ProbeFx final:public juce::AudioPluginInstance{
public:
    float gain=1,offset=0;int calls=0,midiEvents=0,resets=0;bool mono=false,invalid=false;float previous[2]{};bool delay=false;
    ProbeFx(float g=1,float o=0,bool m=false):AudioPluginInstance(BusesProperties().withInput("In",m?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo(),true).withOutput("Out",m?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo(),true)),gain(g),offset(o),mono(m){}
    const juce::String getName()const override{return "FX probe";}void fillInPluginDescription(juce::PluginDescription& d)const override{d.name=getName();d.pluginFormatName="Internal";d.fileOrIdentifier="test:fx-probe";d.isInstrument=false;d.numInputChannels=d.numOutputChannels=mono?1:2;}
    bool isBusesLayoutSupported(const BusesLayout& l)const override{return l.getMainInputChannelSet()==(mono?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo())&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
    void prepareToPlay(double sr,int n)override{setRateAndBufferSizeDetails(sr,n);previous[0]=previous[1]=0;}void releaseResources()override{}void reset()override{++resets;previous[0]=previous[1]=0;}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi)override{++calls;midiEvents+=midi.getNumEvents();for(int ch=0;ch<b.getNumChannels();++ch)for(int n=0;n<b.getNumSamples();++n){float input=b.getSample(ch,n);float value=invalid?std::numeric_limits<float>::quiet_NaN():delay?previous[ch]:input*gain+offset;previous[ch]=input;b.setSample(ch,n,value);}midi.addEvent(juce::MidiMessage::noteOn(1,60,(juce::uint8)100),0);}
    bool acceptsMidi()const override{return true;}bool producesMidi()const override{return true;}double getTailLengthSeconds()const override{return delay?.1:0;}bool hasEditor()const override{return false;}juce::AudioProcessorEditor* createEditor()override{return nullptr;}
    int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return "Default";}void changeProgramName(int,const juce::String&)override{}
    void getStateInformation(juce::MemoryBlock& b)override{juce::MemoryOutputStream out(b,false);out.writeFloat(gain);out.writeFloat(offset);}void setStateInformation(const void* b,int n)override{if(n>=8){juce::MemoryInputStream in(b,(size_t)n,false);gain=in.readFloat();offset=in.readFloat();}}
};
ProbeFx* put(FxChain& chain,int index,float gain=1,float offset=0){auto plugin=std::make_unique<ProbeFx>(gain,offset);auto* raw=plugin.get();plugin->fillInPluginDescription(chain.nodes[(size_t)index].description);chain.nodes[(size_t)index].plugin=std::move(plugin);return raw;}
void constant(juce::AudioBuffer<float>& b,float value){for(int ch=0;ch<b.getNumChannels();++ch)juce::FloatVectorOperations::fill(b.getWritePointer(ch),value,b.getNumSamples());}
bool near(float value,float expected){return std::abs(value-expected)<.00005f;}
void fillSynth(Engine& e){e.slots[0].plugin=makeTestSynth();e.slots[0].plugin->fillInPluginDescription(e.slots[0].description);}
}
int runFxTests(const juce::File& report,const juce::String& pluginPath){
    using namespace juce;int failures=0;Array<var> results;auto check=[&](bool ok,String title){auto* o=new DynamicObject();o->setProperty("test",title);o->setProperty("passed",ok);results.add(var(o));if(!ok)++failures;};
    AudioBuffer<float> block(2,128);FxChain chain;constant(block,.4f);chain.prepare(48000,128);chain.process(block);check(chain.count()==0&&near(block.getSample(0,0),.4f),"Empty FX chain leaves input unchanged");
    auto synth=makeTestSynth();check(FxChain::configureStereo(*synth).isNotEmpty(),"Instrument cannot enter an audio FX chain");ProbeFx mono(1,0,true);check(FxChain::configureStereo(mono).isNotEmpty(),"Unsupported mono-only FX rejected before mutation");
    auto* first=put(chain,0,.5f);check(FxChain::configureStereo(*first).isEmpty(),"Stereo audio FX accepts primary input/output layout");chain.prepare(48000,128);constant(block,.4f);chain.process(block);check(chain.count()==1&&near(block.getSample(0,0),.2f)&&near(block.getSample(1,127),.2f),"Stereo gain effect receives both audio channels");
    auto* second=put(chain,1,1,.1f);chain.prepare(48000,128);constant(block,.4f);chain.process(block);check(near(block.getSample(0,127),.3f),"FX order processes gain before offset");check(first->midiEvents==0&&second->midiEvents==0,"No instrument MIDI or preceding effect MIDI enters audio FX");
    chain.moveNode(1,0);constant(block,.4f);chain.process(block);check(near(block.getSample(0,0),.25f),"Moving an effect changes serial processing order");
    int before=second->calls;chain.nodes[0].bypassed=true;constant(block,.4f);chain.process(block);check(near(block.getSample(0,0),.2f)&&second->calls==before,"Bypassed effect neither processes nor changes audio");
    first->setLatencySamples(32);second->setLatencySamples(64);check(chain.latencySamples()==32,"Reported chain latency excludes bypassed effect");chain.nodes[0].bypassed=false;check(chain.latencySamples()==96,"Reported chain latency sums active effects");
    chain.remove(0);check(chain.count()==1&&chain.nodes[0].plugin.get()==first&&!chain.nodes[0].bypassed,"Removing effect compacts nodes without losing remaining state");
    put(chain,1);put(chain,2);put(chain,3);check(chain.count()==fxCapacity,"Four effect stages are available per chain");FxChain pending;auto* replacement=put(pending,0,.25f);chain.swapWith(pending);check(chain.count()==1&&chain.nodes[0].plugin.get()==replacement&&pending.count()==4,"Detached chain swap retains old chain until replacement succeeds");chain.prepare(48000,128);
    replacement->invalid=true;constant(block,.4f);chain.process(block);check(block.getMagnitude(0,128)==0,"Nonfinite external FX output becomes silence before downstream stages");replacement->invalid=false;
    float* address=block.getWritePointer(0);for(int n=0;n<50;++n){constant(block,.4f);chain.process(block);}check(block.getWritePointer(0)==address&&near(block.getSample(0,127),.1f),"Repeated prepared processing retains supplied audio storage");
    chain.clear();check(chain.count()==0&&chain.latencySamples()==0,"Clearing FX releases all stages");
    auto gate=makeTestFx();PluginDescription gateDescription;gate->fillInPluginDescription(gateDescription);check(gateDescription.fileOrIdentifier=="stage:gate"&&!gateDescription.isInstrument&&FxChain::configureStereo(*gate).isEmpty(),"Built-in noise gate is a valid restorable stereo FX");
    gate->getParameters()[0]->setValueNotifyingHost(.4f);gate->getParameters()[1]->setValueNotifyingHost(.7f);MemoryBlock state;gate->getStateInformation(state);auto restored=makeTestFx();restored->setStateInformation(state.getData(),(int)state.getSize());check(near(restored->getParameters()[0]->getValue(),.4f)&&near(restored->getParameters()[1]->getValue(),.7f),"Built-in FX parameter state restores exactly");
    gate->prepareToPlay(48000,128);MidiBuffer midi;constant(block,.001f);for(int i=0;i<30;++i)gate->processBlock(block,midi);check(block.getMagnitude(0,128)<.00001f,"Gate suppresses audio below threshold");for(int i=0;i<40;++i){constant(block,.4f);gate->processBlock(block,midi);}check(near(block.getSample(0,127),.28f),"Gate opens above threshold and applies output gain");gate->reset();constant(block,0);gate->processBlock(block,midi);check(block.getMagnitude(0,128)==0,"Gate reset leaves silence with no generated tone");
    auto dryOwner=std::make_unique<Engine>(),wetOwner=std::make_unique<Engine>();Engine& dry=*dryOwner;Engine& wet=*wetOwner;fillSynth(dry);fillSynth(wet);put(wet.slots[0].fx,0,.5f);put(wet.masterFx,0,.5f);dry.prepare(48000,128);wet.prepare(48000,128);dry.midi(0x90,60,100);wet.midi(0x90,60,100);AudioBuffer<float> clean(2,128),processed(2,128);float error=0;
    for(int n=0;n<100;++n){dry.render(clean.getArrayOfWritePointers(),2,128);wet.render(processed.getArrayOfWritePointers(),2,128);for(int k=0;k<128;++k)error=jmax(error,std::abs(processed.getSample(0,k)-clean.getSample(0,k)*.25f));}
    check(error<.00001f,"Per-source FX and master FX both process final instrument sound");check(near(wet.slots[0].peak,dry.slots[0].peak*.5f),"Source meter measures source FX result before master FX");check(near(wet.outputPeak,dry.outputPeak*.25f),"Final output meter measures master FX result");
    auto* idle=put(wet.slots[1].fx,0);wet.slots[1].plugin=makeTestSynth();wet.prepare(48000,128);wet.render(processed.getArrayOfWritePointers(),2,128);check(idle->calls==0,"Inactive source FX never run on the audio callback");
    auto clipDryOwner=std::make_unique<Engine>(),clipWetOwner=std::make_unique<Engine>();Engine& clipDry=*clipDryOwner;Engine& clipWet=*clipWetOwner;clipDry.clips[0].data=makeDemoClip(0);clipWet.clips[0].data=makeDemoClip(0);put(clipWet.masterFx,0,.5f);clipDry.prepare(48000,128);clipWet.prepare(48000,128);clipDry.control(ClipPlay,0,1);clipWet.control(ClipPlay,0,1);clipDry.render(clean.getArrayOfWritePointers(),2,128);clipWet.render(processed.getArrayOfWritePointers(),2,128);check(near(processed.getMagnitude(0,128),clean.getMagnitude(0,128)*.5f),"Master FX also processes effects-pad audio");
    auto tailOwner=std::make_unique<Engine>();Engine& tail=*tailOwner;auto* delay=put(tail.masterFx,0);delay->delay=true;tail.prepare(48000,128);tail.clips[0].data=makeDemoClip(0);tail.control(ClipPlay,0,1);tail.render(processed.getArrayOfWritePointers(),2,128);int prior=delay->calls;tail.control(Panic);tail.render(processed.getArrayOfWritePointers(),2,128);check(delay->resets>0&&processed.getMagnitude(0,128)==0&&delay->calls==prior+1,"Panic clears master delay state without leaving audio ringing");
    wet.masterFx.nodes[0].bypassed=true;wet.control(Panic);wet.render(processed.getArrayOfWritePointers(),2,128);wet.audioDeviceStopped();dry.audioDeviceStopped();clipDry.audioDeviceStopped();clipWet.audioDeviceStopped();tail.audioDeviceStopped();
    String vendorName,vendorFormat;float vendorPeak=0;
    if(pluginPath.isNotEmpty()){
        AudioPluginFormatManager formats;addPluginFormats(formats);OwnedArray<PluginDescription> types;scanPlugin(File(pluginPath),types);check(!types.isEmpty(),"Installed FX plugin discovery");
        if(!types.isEmpty()){
            String error;auto plugin=formats.createPluginInstance(*types[0],48000,128,error);check(plugin!=nullptr,"Installed FX plugin construction: "+error);
            if(plugin){vendorName=plugin->getName();vendorFormat=types[0]->pluginFormatName;auto configured=FxChain::configureStereo(*plugin);check(configured.isEmpty(),"Installed FX accepts stereo layout: "+configured);
                if(configured.isEmpty()){
                    FxChain vendor;vendor.nodes[0].description=*types[0];vendor.nodes[0].plugin=std::move(plugin);vendor.prepare(48000,128);bool finite=true;double phase=0;
                    for(int frame=0;frame<250;++frame){for(int n=0;n<128;++n){float value=(float)(.2*std::sin(phase));phase+=MathConstants<double>::twoPi*440/48000;block.setSample(0,n,value);block.setSample(1,n,value);}vendor.process(block);for(int ch=0;ch<2;++ch)for(int n=0;n<128;++n){finite&=std::isfinite(block.getSample(ch,n));vendorPeak=jmax(vendorPeak,std::abs(block.getSample(ch,n)));}}
                    check(finite&&vendorPeak>0,"Installed FX processes finite nonzero stereo audio offline");MemoryBlock state;vendor.nodes[0].plugin->getStateInformation(state);vendor.nodes[0].plugin->setStateInformation(state.getData(),(int)state.getSize());check(state.getSize()>0,"Installed FX exposes serializable plugin state");vendor.nodes[0].bypassed=true;constant(block,.13f);vendor.process(block);check(near(block.getSample(0,0),.13f)&&near(block.getSample(1,127),.13f),"Installed FX bypass passes stereo input unchanged");vendor.release();
                }
            }
        }
    }
    auto* summary=new DynamicObject();summary->setProperty("failures",failures);summary->setProperty("results",results);summary->setProperty("opensAudioDevice",false);summary->setProperty("latencyCompensation",false);summary->setProperty("vendorName",vendorName);summary->setProperty("vendorFormat",vendorFormat);summary->setProperty("vendorPeak",vendorPeak);report.getParentDirectory().createDirectory();report.replaceWithText(JSON::toString(var(summary)));return failures?1:0;
}
}
