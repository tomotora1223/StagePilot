#include "Sequence.h"
#include <cmath>

namespace stage {
bool Sequencer::valid(const Pattern& p) {
    if(p.bpm<40||p.bpm>240||p.bars<1||p.bars>4)return false;
    for(size_t i=0;i<p.notes.size();++i){auto a=p.notes[i];if(!a.enabled)continue;
        if(a.pitch<0||a.pitch>127||a.start<0||a.length<1||a.start+a.length>p.bars*16||a.velocity<1||a.velocity>127)return false;
        for(size_t j=0;j<i;++j){auto b=p.notes[j];if(b.enabled&&a.pitch==b.pitch&&a.start<b.start+b.length&&b.start<a.start+a.length)return false;}
    }return true;
}
juce::var Sequencer::toJson(const Pattern& p) {
    auto* o=new juce::DynamicObject();o->setProperty("bpm",p.bpm);o->setProperty("bars",p.bars);o->setProperty("loop",p.loop);
    juce::Array<juce::var> notes;
    for(auto n:p.notes)if(n.enabled){auto* v=new juce::DynamicObject();v->setProperty("pitch",n.pitch);v->setProperty("start",n.start);v->setProperty("length",n.length);v->setProperty("velocity",n.velocity);notes.add(juce::var(v));}
    o->setProperty("notes",notes);return juce::var(o);
}
bool Sequencer::fromJson(const juce::var& v,Pattern& result) {
    auto* list=v["notes"].getArray();if(!list||list->size()>sequenceNoteCount)return false;
    Pattern p;p.bpm=(int)v["bpm"];p.bars=(int)v["bars"];p.loop=(bool)v["loop"];
    for(int i=0;i<list->size();++i){auto n=(*list)[i];p.notes[(size_t)i]={(int)n["pitch"],(int)n["start"],(int)n["length"],(int)n["velocity"],true};}
    if(!valid(p))return false;result=p;return true;
}
void Sequencer::reset() {
    meter.reset();playing=false;sounding=false;position=0;frame=tail=previewLeft=0;previewPitch=-1;held.fill(0);midi.clear();for(auto& l:lit)l=false;
}
void Sequencer::prepare(double sr,int block) {
    reset();sampleRate=sr;maximumBlock=block;
    buffer.setSize(plugin?juce::jmax(2,plugin->getTotalNumInputChannels(),plugin->getTotalNumOutputChannels()):2,block);
    midi.ensureSize(65536);trim.reset(sr,.01);trim.setCurrentAndTargetValue(gain.load());tailGate.reset(sr,0.01);tailGate.setCurrentAndTargetValue(1);
    if(plugin){plugin->setRateAndBufferSizeDetails(sr,block);plugin->prepareToPlay(sr,block);}
}
int64_t Sequencer::stepFrame(int step) const {return (int64_t)std::llround(step*sampleRate*15.0/pattern.bpm);}
int64_t Sequencer::durationFrames() const {return stepFrame(pattern.bars*16);}
void Sequencer::noteOn(int pitch,int velocity,int offset) {
    if(pitch<0||pitch>127)return;
    if(held[(size_t)pitch]++==0)midi.addEvent(juce::MidiMessage::noteOn(1,pitch,(juce::uint8)velocity),offset);
    lit[(size_t)pitch]=true;
}
void Sequencer::noteOff(int pitch,int offset) {
    if(pitch<0||pitch>127||held[(size_t)pitch]==0)return;
    if(--held[(size_t)pitch]==0){midi.addEvent(juce::MidiMessage::noteOff(1,pitch),offset);lit[(size_t)pitch]=false;}
}
void Sequencer::stop(bool immediate,int offset) {
    playing=false;position=0;previewPitch=-1;previewLeft=0;
    for(int i=0;i<128;++i)if(held[(size_t)i]){held[(size_t)i]=1;noteOff(i,offset);}
    if(immediate){midi.addEvent(juce::MidiMessage::allSoundOff(1),offset);tail=0;tailGate.setCurrentAndTargetValue(0);}
    else {tail=(int64_t)(sampleRate*3);tailGate.setCurrentAndTargetValue(1);}
    // One block is still processed so VSTs receive the release messages.
    sounding=plugin!=nullptr;
}
void Sequencer::start() {
    if(!plugin)return;stop(true);frame=0;position=0;playing=true;sounding=true;tailGate.setCurrentAndTargetValue(1);
}
void Sequencer::preview(int pitch,int velocity) {
    if(!plugin||pitch<0||pitch>127)return;
    if(playing)return;
    if(previewPitch>=0)noteOff(previewPitch,0);
    previewPitch=pitch;previewLeft=(int64_t)(sampleRate*0.35);tail=(int64_t)(sampleRate*3);
    tailGate.setCurrentAndTargetValue(1);sounding=true;noteOn(pitch,juce::jlimit(1,127,velocity),0);
}
void Sequencer::edit(int index,SequenceNote n) {
    if(index<0||index>=sequenceNoteCount)return;
    auto candidate=pattern;candidate.notes[(size_t)index]=n;if(!valid(candidate))return;
    stop();pattern=candidate;
}
void Sequencer::settings(int bpm,int bars,bool loop) {
    stop();pattern.bpm=juce::jlimit(40,240,bpm);pattern.bars=juce::jlimit(1,4,bars);pattern.loop=loop;
    for(auto& n:pattern.notes){if(n.start>=pattern.bars*16)n.enabled=false;else n.length=juce::jmin(n.length,pattern.bars*16-n.start);}
}
void Sequencer::render(float* const* output,int channels,int samples) {
    if(samples<=0||samples>maximumBlock)return;if(!plugin||!sounding){meter.push({}, {},samples,sampleRate);return;}trim.setTargetValue(gain.load());
    if(playing){
        int offset=0;
        while(offset<samples&&playing){
            const auto length=durationFrames();
            if(frame==length){
                for(auto n:pattern.notes)if(n.enabled&&stepFrame(n.start+n.length)==length)noteOff(n.pitch,offset);
                if(pattern.loop)frame=0;else {stop(false,offset);break;}
            }
            int count=(int)juce::jmin((int64_t)(samples-offset),length-frame);
            struct Scheduled {int at,pitch,velocity;};std::array<Scheduled,sequenceNoteCount*2> events{};int used=0;
            for(auto n:pattern.notes)if(n.enabled){
                auto end=stepFrame(n.start+n.length),start=stepFrame(n.start);
                if(end>=frame&&end<frame+count)events[(size_t)used++]={offset+(int)(end-frame),n.pitch,0};
                if(start>=frame&&start<frame+count)events[(size_t)used++]={offset+(int)(start-frame),n.pitch,n.velocity};
            }
            std::sort(events.begin(),events.begin()+used,[](auto a,auto b){return a.at==b.at?a.velocity<b.velocity:a.at<b.at;});
            for(int i=0;i<used;++i){auto e=events[(size_t)i];if(e.velocity)noteOn(e.pitch,e.velocity,e.at);else noteOff(e.pitch,e.at);}
            frame+=count;offset+=count;
        }
        if(playing)position=frame*pattern.bars*16.0/durationFrames();
    }
    if(previewPitch>=0){if(previewLeft<samples){noteOff(previewPitch,(int)previewLeft);previewPitch=-1;}else previewLeft-=samples;}
    if(!playing&&previewPitch<0&&tail<=(int64_t)(sampleRate*.01))tailGate.setTargetValue(0);
    if(!playing&&previewPitch<0&&tail<=samples)midi.addEvent(juce::MidiMessage::allSoundOff(1),juce::jlimit(0,samples-1,(int)tail-1));
    buffer.clear();juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(),buffer.getNumChannels(),samples);plugin->processBlock(block,midi);
    std::array<float,2> maxima{};std::array<double,2> squares{};
    for(int k=0;k<samples;++k){const float gain=tailGate.getNextValue()*trim.getNextValue();for(int c=0;c<juce::jmin(channels,2);++c)if(output[c]){int pc=plugin->getTotalNumOutputChannels()==1?0:c;float v=block.getSample(pc,k)*gain;if(std::isfinite(v)){output[c][k]+=v;maxima[(size_t)c]=juce::jmax(maxima[(size_t)c],std::abs(v));squares[(size_t)c]+=(double)v*v;}}}
    meter.push(maxima,squares,samples,sampleRate,juce::jmax(maxima[0],maxima[1])>1);
    if(!playing&&previewPitch<0){tail-=samples;if(tail<=0&&!tailGate.isSmoothing()){sounding=false;}}
}
}
