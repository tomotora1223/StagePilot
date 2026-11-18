#include "Engine.h"
#include <cmath>

namespace stage {
bool readClip(const juce::File& file,ClipData& result,juce::String& error) {
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader){error=juce::String::fromUTF8("音声ファイルを読み込めません。WAV・AIFF・FLACを選んでください。");return false;}
    if(reader->sampleRate<=0||reader->lengthInSamples<=0||reader->numChannels<1||reader->numChannels>2
       ||reader->lengthInSamples/reader->sampleRate>120||reader->lengthInSamples>8*1024*1024){
        error=juce::String::fromUTF8("効果音は120秒以内、モノラルまたはステレオにしてください。");return false;
    }
    ClipData pending;pending.audio.setSize(2,(int)reader->lengthInSamples);
    if(!reader->read(&pending.audio,0,pending.audio.getNumSamples(),0,true,true)){error=juce::String::fromUTF8("音声データが壊れているため読み込めません。");return false;}
    if(reader->numChannels==1)pending.audio.copyFrom(1,0,pending.audio,0,0,pending.audio.getNumSamples());
    for(int c=0;c<2;++c)for(int n=0;n<pending.audio.getNumSamples();++n)if(!std::isfinite(pending.audio.getSample(c,n)))pending.audio.setSample(c,n,0);
    pending.sampleRate=reader->sampleRate;pending.file=file;pending.name=file.getFileNameWithoutExtension();result=std::move(pending);return true;
}
ClipData makeDemoClip(int index) {
    ClipData d;d.sampleRate=48000;d.builtin=index;d.name=juce::String::fromUTF8("内蔵チャイム ")+juce::String(index+1);d.audio.setSize(2,24000);
    for(int n=0;n<24000;++n){double t=n/d.sampleRate;float sample=(float)(0.35*std::sin(juce::MathConstants<double>::twoPi*(600+index*100)*t)*std::exp(-t*12));for(int c=0;c<2;++c)d.audio.setSample(c,n,sample);}
    return d;
}
class SynthParameter final : public juce::HostedAudioProcessorParameter {
    juce::String id, name;
    float low, high, initial;
    std::atomic<float> value;
public:
    SynthParameter(juce::String key,juce::String title,float min,float max,float start)
        : id(key),name(title),low(min),high(max),initial((start-min)/(max-min)),value(initial) {}
    juce::String getParameterID() const override {return id;}
    juce::String getName(int length) const override {return name.substring(0,length);}
    juce::String getLabel() const override {return {};}
    float getValue() const override {return value.load();}
    float getDefaultValue() const override {return initial;}
    void setValue(float v) override {value=juce::jlimit(0.0f,1.0f,v);}
    juce::String getText(float v,int) const override {return juce::String(low+v*(high-low),2);}
    float getValueForText(const juce::String& text) const override {return juce::jlimit(0.0f,1.0f,(text.getFloatValue()-low)/(high-low));}
    float get() const {return low+value.load()*(high-low);}
};
class TestSynth final : public juce::AudioPluginInstance {
    struct Voice { bool on = false, down = false; int note = 0, channel = 1; double phase = 0, step = 0; float amp = 0, velocity = 0; };
    std::array<Voice, 64> voices{};
    std::array<bool,16> pedal{};
    std::array<double,16> bend{};
    SynthParameter *brightness, *release;
public:
    TestSynth() : AudioPluginInstance(BusesProperties().withOutput("Stereo", juce::AudioChannelSet::stereo(), true)) {
        auto b=std::make_unique<SynthParameter>("brightness",juce::String::fromUTF8("音の明るさ"),0.0f,1.0f,0.3f);brightness=b.get();addHostedParameter(std::move(b));
        auto r=std::make_unique<SynthParameter>("release",juce::String::fromUTF8("余韻の長さ"),0.02f,5.0f,0.6f);release=r.get();addHostedParameter(std::move(r));
    }
    const juce::String getName() const override { return juce::String::fromUTF8("内蔵シンセ"); }
    void fillInPluginDescription(juce::PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "Internal"; d.fileOrIdentifier = "stage:sine"; d.isInstrument = true; d.numOutputChannels = 2; }
    void prepareToPlay(double sr, int n) override { setRateAndBufferSizeDetails(sr,n); for(auto& v: voices) v = {}; pedal.fill(false); bend.fill(1.0); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override {
        b.clear();
        auto applyMidi = [&](const juce::MidiMessage& m) {
            int ch = m.getChannel();
            if (m.isNoteOn()) {
                Voice* v = &voices[0];
                for (auto& candidate : voices) if (!candidate.on) { v = &candidate; break; }
                *v = {true,true,m.getNoteNumber(),ch,0,juce::MathConstants<double>::twoPi*juce::MidiMessage::getMidiNoteInHertz(m.getNoteNumber())/getSampleRate(),0,m.getFloatVelocity()};
            } else if (m.isPitchWheel()) {
                bend[(size_t)ch-1]=std::exp2((m.getPitchWheelValue()-8192)/(8192.0*6.0));
            } else if (m.isNoteOff()) {
                for(auto& v: voices) if(v.note == m.getNoteNumber() && v.channel == ch) v.down = false;
            } else if (m.isSustainPedalOn()) pedal[(size_t)ch-1] = true;
            else if (m.isSustainPedalOff()) pedal[(size_t)ch-1] = false;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { for(auto& v: voices) if(v.channel==ch) v = {}; pedal[(size_t)ch-1] = false; }
        }
        ;
        auto nextMidi=midi.begin();
        const auto sr = getSampleRate();
        const float decay = std::exp(-1.0f / ((float)sr * release->get()));
        const auto tone=brightness->get()*0.25;
        for (int n = 0; n < b.getNumSamples(); ++n) {
            while(nextMidi!=midi.end()&&(*nextMidi).samplePosition<=n){applyMidi((*nextMidi).getMessage());++nextMidi;}
            float sum = 0;
            for(auto& v: voices) if(v.on) {
                bool hold = v.down || pedal[(size_t)v.channel-1];
                v.amp = hold ? std::min(1.0f,v.amp + 1.0f/(float)(sr*0.005)) : v.amp * decay;
                if (v.amp < 0.00001f && !hold) { v.on = false; continue; }
                sum += (float)(std::sin(v.phase) + tone * std::sin(3*v.phase)) * v.amp * v.velocity * 0.12f;
                v.phase += v.step*bend[(size_t)v.channel-1];
                if(v.phase > juce::MathConstants<double>::twoPi) v.phase -= juce::MathConstants<double>::twoPi;
            }
            for(int c=0;c<b.getNumChannels();++c) b.setSample(c,n,sum);
        }
    }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 5; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& data) override { juce::MemoryOutputStream s(data,false); s.writeFloat(brightness->getValue()); s.writeFloat(release->getValue()); }
    void setStateInformation(const void* data,int n) override { if(n>=8) { juce::MemoryInputStream s(data,(size_t)n,false); brightness->setValueNotifyingHost(s.readFloat()); release->setValueNotifyingHost(s.readFloat()); } }
};
std::unique_ptr<juce::AudioPluginInstance> makeTestSynth() { return std::make_unique<TestSynth>(); }

Engine::Engine() { master.setCurrentAndTargetValue(0.7f);sequence.plugin=makeTestSynth();sequence.plugin->fillInPluginDescription(sequence.description);sequence.name=sequence.description.name;for(int i=0;i<sceneCount;++i)scenes[(size_t)i].name=juce::String::fromUTF8("組み合わせ ")+juce::String(i+1); }
bool Engine::post(const Event& e) { if(queue.push(e)) return true; ++dropped; overflowPanic.store(true); return false; }
void Engine::control(int a,int s,float v,int p) { Event e; e.type=Event::Control;e.a=a;e.b=s;e.c=p;e.value=v;post(e); }
void Engine::midi(int s,int a,int b) { Event e;e.a=s;e.b=a;e.c=b;post(e); }
void Engine::preview(int note,int velocity){Event e;e.type=Event::LivePreview;e.a=note;e.b=velocity;post(e);}
void Engine::releasePreview(int i){auto& v=previewVoices[(size_t)i];if(v.target>=0)dispatch(i,juce::MidiMessage::noteOff(v.channel+1,v.target));v={};bool playing=false;for(auto voice:previewVoices)playing|=voice.target>=0;if(!playing)previewKey=-1;}
void Engine::stopPreview(){for(int i=0;i<slotCount;++i)releasePreview(i);previewKey=-1;previewLeft=0;}
void Engine::startPreview(int note,int velocity){
    stopPreview();if(note<0||note>127||velocity<=0)return;
    for(int i=0;i<slotCount;++i)if(wanted(i)){
        auto& s=slots[(size_t)i];if(note<s.keyLow||note>s.keyHigh)continue;
        int target=note+s.transpose.load()+performanceTranspose.load();if(target<0||target>127)continue;
        // Borrow a silent, pedal-free channel. Physical MIDI takes priority if it arrives later.
        int channel=-1;for(int n=0;n<16;++n){int ch=n==0?0:16-n;if(sustain[(size_t)ch]!=0||forwardedPedal[(size_t)i][(size_t)ch]!=0)continue;
            bool held=false;for(auto pitch:noteTargets[(size_t)i][(size_t)ch])held|=pitch>=0;if(!held){channel=ch;break;}}
        if(channel<0)continue;previewVoices[(size_t)i]={target,channel};dispatch(i,juce::MidiMessage::noteOn(channel+1,target,(juce::uint8)juce::jlimit(1,127,velocity)));previewKey=note;
    }
    if(previewKey>=0)previewLeft=(int64_t)(rate.load()*10);
}
void Engine::resetRouting() {
    remotePitch=8192;
    remotePitchSent.fill(8192);
    sequence.reset();outputMeter.reset();outputPeak=0;
    for(auto& key:inputKeys)key=0;
    previewVoices.fill({});previewKey=-1;previewLeft=0;
    Event e; while(queue.pop(e)) {}
    for(auto& row:owners) row.fill(0);
    for(auto& slot:noteTargets)for(auto& row:slot)row.fill(-1);
    for(auto& row:consumed) row.fill(false);
    sustain.fill(0);pedalDown=false;pressed.fill(false);
    for(auto& row:forwardedPedal)row.fill(0);
    for(auto& s:slots) { s.midi.clear(); s.running=false;s.retiring=false;s.tailLeft=0;s.tailActive=false;s.tailRemaining=0;s.pedalDown=false;s.peak=0;s.meter.reset(); }
    for(auto& c:clips){for(auto& v:c.voices)v.active=false;c.playing=false;}
}
void Engine::prepare(double sr,int n) {
    movie.requestStop(Recorder::Reason::DeviceChanged);
    if(recorder.isCapturing()&&recorder.getSampleRate()!=sr)recorder.requestStop(Recorder::Reason::DeviceChanged);
    rate=sr;blockSize=n;masterFxBuffer.setSize(2,n);masterFx.prepare(sr,n);resetRouting();restoreScene();sequence.prepare(sr,n);master.reset(sr,0.02);master.setCurrentAndTargetValue(masterValue.load());
    for(auto& s:slots) {
        s.buffer.setSize(s.plugin ? std::max(2,std::max(s.plugin->getTotalNumInputChannels(),s.plugin->getTotalNumOutputChannels())) : 2,n);
        s.midi.ensureSize(262144);s.gate.reset(sr,0.01);s.gate.setCurrentAndTargetValue(0);
        auto gain=s.trim.getTargetValue();s.trim.reset(sr,0.01);s.trim.setCurrentAndTargetValue(gain);
        if(s.plugin) { s.plugin->setRateAndBufferSizeDetails(sr,n);s.plugin->prepareToPlay(sr,n); }s.fx.prepare(sr,n);
    }
    for(int i=0;i<slotCount;++i)if(wanted(i)){slots[(size_t)i].running=true;slots[(size_t)i].gate.setCurrentAndTargetValue(1);}
}
void Engine::dispatch(int i,const juce::MidiMessage& m) { if(slots[(size_t)i].plugin) slots[(size_t)i].midi.addEvent(m,0); }
void Engine::silence(int i,bool immediate) {
    releasePreview(i);
    for(int ch=1;ch<=16;++ch) {
        sendPedal(i,ch-1,0);
        dispatch(i,juce::MidiMessage::allNotesOff(ch));
        if(immediate) dispatch(i,juce::MidiMessage::allSoundOff(ch));
        for(auto& mask:owners[(size_t)ch-1]) mask &= (uint8_t)~(1<<i);
        noteTargets[(size_t)i][(size_t)ch-1].fill(-1);
    }
}
void Engine::sendPedal(int i,int ch,int value) {
    dispatch(i,juce::MidiMessage::controllerEvent(ch+1,64,value));
    auto& row=forwardedPedal[(size_t)i];row[(size_t)ch]=value;
    slots[(size_t)i].pedalDown=std::any_of(row.begin(),row.end(),[](int v){return v>=64;});
}
void Engine::syncPedal(int i) {
    auto& s=slots[(size_t)i];
    if(!s.plugin||!s.running)return;
    for(int ch=0;ch<16;++ch){int value=wanted(i)&&!s.ignorePedal?sustain[(size_t)ch]:0;
        if(forwardedPedal[(size_t)i][(size_t)ch]!=value)sendPedal(i,ch,value);}
}
void Engine::releaseForSwitch(int i) {
    // Release voices naturally, even if the player's keys or sustain are still held.
    // Retain note targets until physical key-up so transposed releases stay correct.
    releasePreview(i);for(int ch=0;ch<16;++ch){sendPedal(i,ch,0);
        for(auto target:noteTargets[(size_t)i][(size_t)ch])
            if(target>=0)dispatch(i,juce::MidiMessage::noteOff(ch+1,target));}
}
void Engine::releaseSharedForSwitch(const std::array<bool,slotCount>& previous) {
    // A shared plugin cannot distinguish old sustain voices from the new scene.
    // Clear its old voices before restoring the new scene's pedal policy.
    for(int i=0;i<slotCount;++i)if(previous[(size_t)i]&&wanted(i)){
        releaseForSwitch(i);
        for(int ch=1;ch<=16;++ch)dispatch(i,juce::MidiMessage::allSoundOff(ch));
    }
}
void Engine::switchTo(int i) {
    if(i<0 || i>=slotCount || !slots[(size_t)i].plugin) return;
    stopPreview();bool leaving=activeScene>=0;std::array<bool,slotCount> previous{};if(leaving)for(int n=0;n<slotCount;++n)previous[(size_t)n]=wanted(n);
    if(leaving){activeScene=-1;for(int n=0;n<slotCount;++n){auto p=standalone[(size_t)n];applyScenePart(n,p,n!=i&&!p.enabled);slots[(size_t)n].layer=p.enabled;}}
    selected=i;if(leaving)releaseSharedForSwitch(previous);refreshActive();
}
void Engine::applyScenePart(int i,ScenePart p,bool preserveTail) {
    auto& s=slots[(size_t)i];s.sceneEnabled=p.enabled;
    if(preserveTail&&!p.enabled&&s.running)return;
    s.transpose=p.transpose;s.keyLow=p.low;s.keyHigh=p.high;s.gain=p.gain;s.ignorePedal=p.ignorePedal;s.trim.setTargetValue(p.gain);
}
void Engine::restoreScene(){if(activeScene>=0&&activeScene<sceneCount)for(int i=0;i<slotCount;++i)applyScenePart(i,scenes[(size_t)activeScene.load()].parts[(size_t)i],true);}
void Engine::selectScene(int index) {
    if(index<0||index>=sceneCount||activeScene==index)return;
    stopPreview();std::array<bool,slotCount> previous{};for(int i=0;i<slotCount;++i)previous[(size_t)i]=wanted(i);
    if(activeScene<0)for(int i=0;i<slotCount;++i){auto& s=slots[(size_t)i];standalone[(size_t)i]={s.keyLow,s.keyHigh,s.transpose,s.gain,s.layer,s.ignorePedal};}
    activeScene=index;restoreScene();releaseSharedForSwitch(previous);refreshActive();
}
void Engine::refreshActive() {
    for(int i=0;i<slotCount;++i){auto& s=slots[(size_t)i];
        if(wanted(i)){
            if(!s.running||s.retiring){s.running=true;s.retiring=false;s.tailActive=false;s.tailRemaining=0;s.gate.reset(rate.load(),mode.load()==1?transitionSeconds.load():0.005);s.gate.setTargetValue(1);}
            syncPedal(i);
            syncRemotePitch(i);
        }else if(s.running&&!s.retiring){s.retiring=true;s.tailActive=true;s.tailLeft=(int64_t)((mode.load()==0?tailSeconds.load():mode.load()==1?transitionSeconds.load():.005f)*rate.load());releaseForSwitch(i);syncRemotePitch(i);
            if(mode.load()!=0){s.gate.reset(rate.load(),mode.load()==1?transitionSeconds.load():0.005);s.gate.setTargetValue(0);silence(i,false);}}
    }
}
void Engine::releaseNote(int ch,int note) {
    inputKeys[(size_t)note].fetch_and((uint16_t)~(1<<ch));
    for(int i=0;i<slotCount;++i){auto& target=noteTargets[(size_t)i][(size_t)ch][(size_t)note];
        if(target>=0){dispatch(i,juce::MidiMessage::noteOff(ch+1,target));target=-1;}}
    owners[(size_t)ch][(size_t)note]=0;
}
void Engine::playClip(int i,float velocity) {
    if(i<0||i>=clipCount)return;auto& c=clips[(size_t)i];if(c.data.audio.getNumSamples()==0)return;
    int free=0;
    for(int n=0;n<2;++n)if(!c.voices[(size_t)n].active)free=n;
    for(int n=0;n<2;++n)if(n!=free&&c.voices[(size_t)n].active){auto& v=c.voices[(size_t)n];v.envelope.reset(rate.load(),0.005);v.envelope.setTargetValue(0);}
    auto& v=c.voices[(size_t)free];v.position=0;v.velocity=juce::jlimit(0.0f,1.0f,velocity);v.active=true;
    v.envelope.reset(rate.load(),0.003);v.envelope.setCurrentAndTargetValue(0);v.envelope.setTargetValue(1);c.playing=true;
}
void Engine::stopClip(int i,bool immediate) {
    if(i<0||i>=clipCount)return;auto& c=clips[(size_t)i];
    for(auto& v:c.voices){if(immediate)v.active=false;else{v.envelope.reset(rate.load(),0.005);v.envelope.setTargetValue(0);}}
    if(immediate)c.playing=false;
}
void Engine::renderClips(float* const* out,int channels,int n) {
    for(auto& c:clips){int length=c.data.audio.getNumSamples();bool playing=false;
        if(length==0){c.playing=false;continue;}
        const double step=c.data.sampleRate/rate.load();const float gain=c.gain.load();const bool loop=c.loop.load();
        for(auto& v:c.voices)if(v.active){
            for(int k=0;k<n;++k){
                if(v.position>=length){if(loop)v.position=std::fmod(v.position,(double)length);else{v.active=false;break;}}
                float env=v.envelope.getNextValue();if(env==0&&!v.envelope.isSmoothing()){v.active=false;break;}
                int p=(int)v.position;int next=p+1<length?p+1:(loop?0:p);float f=(float)(v.position-p);
                // Short edge fade suppresses clicks on one-shot ends and loop boundaries.
                float edge=juce::jlimit(0.0f,1.0f,(float)(std::min(v.position,(double)length-v.position)/(c.data.sampleRate*0.002)));
                for(int ch=0;ch<std::min(channels,2);++ch)if(out[ch]){const float* data=c.data.audio.getReadPointer(ch);out[ch][k]+=(data[p]+f*(data[next]-data[p]))*env*edge*v.velocity*gain;}
                v.position+=step;
            }
            playing|=v.active;
        }c.playing=playing;
    }
}
void Engine::syncRemotePitch(int slot) {
    const int value=wanted(slot)?remotePitch:8192;
    auto& sent=remotePitchSent[(size_t)slot];
    if(value==sent)return;
    for(int ch=1;ch<=16;++ch)dispatch(slot,juce::MidiMessage::pitchWheel(ch,value));
    sent=value;
}
void Engine::setRemotePitch(int value) {
    remotePitch=value;
    remotePitchUntil=juce::Time::getMillisecondCounter()+600;
    for(int i=0;i<slotCount;++i)syncRemotePitch(i);
}
void Engine::execute(int a,int i,float value,int p) {
    switch(a) {
    case SelectScene:selectScene(i);break;
    case NextScene:case PreviousScene:
        for(int n=1;n<=sceneCount;++n){int origin=activeScene.load();if(origin<0)origin=a==NextScene?-1:0;int target=(origin+(a==NextScene?n:-n)+sceneCount)%sceneCount;bool usable=false;for(int j=0;j<slotCount;++j)usable|=scenes[(size_t)target].parts[(size_t)j].enabled&&slots[(size_t)j].plugin!=nullptr;if(usable){selectScene(target);break;}}break;
    case NextPerformance:case PreviousPerformance:{
        int origin=activeScene>=0?slotCount+activeScene.load():selected.load();
        for(int n=1;n<=slotCount+sceneCount;++n){int target=(origin+(a==NextPerformance?n:-n)+slotCount+sceneCount)%(slotCount+sceneCount);if(target<slotCount){if(slots[(size_t)target].plugin){switchTo(target);break;}}else if(scenes[(size_t)(target-slotCount)].registered){selectScene(target-slotCount);break;}}
        break;}
    case Select:switchTo(i);break;
    case Next:case Previous:execute(a==Next?NextPerformance:PreviousPerformance,0,0,0);break;
    case FadeOut:master.reset(rate.load(),fadeSeconds.load());master.setTargetValue(0);break;
    case Restore:master.reset(rate.load(),0.02);master.setTargetValue(masterValue.load());break;
    case Panic:
        setRemotePitch(8192);stopPreview();sequence.stop(true);rangeLearning=false;rangeLearned=-1;masterFx.reset();for(auto& slot:slots)slot.fx.reset();
        for(auto& key:inputKeys)key=0;
        for(int s=0;s<slotCount;++s) { silence(s,true);slots[(size_t)s].retiring=!wanted(s);slots[(size_t)s].tailLeft=0; }
        for(int c=0;c<clipCount;++c)stopClip(c,true);
        sustain.fill(0);pedalDown=false;break;
    case RemotePitch:setRemotePitch((int)value);break;
    case MasterStep:if(value!=0)execute(Master,0,masterValue.load()+value,0);break;
    case Master:masterValue=juce::jlimit(0.0f,1.0f,value);master.reset(rate.load(),0.02);master.setTargetValue(masterValue.load());break;
    case SlotGain:if(i>=0&&i<slotCount) {slots[(size_t)i].gain=juce::jlimit(0.0f,1.0f,value);slots[(size_t)i].trim.setTargetValue(slots[(size_t)i].gain.load());}break;
    case Parameter:
        if(i>=0&&i<slotCount) if(auto* plugin=slots[(size_t)i].plugin.get()) {
            auto& params=plugin->getParameters();if(p>=0&&p<params.size()) params[p]->setValueNotifyingHost(juce::jlimit(0.0f,1.0f,value));
        }break;
    case LayerToggle:case SetLayer:if(i>=0&&i<slotCount&&slots[(size_t)i].plugin){if(activeScene>=0)slots[(size_t)i].sceneEnabled=a==LayerToggle?!slots[(size_t)i].sceneEnabled.load():value>=0.5f;else slots[(size_t)i].layer=a==LayerToggle?!slots[(size_t)i].layer.load():value>=0.5f;refreshActive();}break;
    case SetTranspose:if(i>=0&&i<slotCount)slots[(size_t)i].transpose=juce::jlimit(-24,24,(int)std::round(value));break;
    case PerformanceTranspose:performanceTranspose=juce::jlimit(-24,24,(int)std::round(value));break;
    case PerformanceTransposeStep:performanceTranspose=juce::jlimit(-24,24,performanceTranspose.load()+(int)std::round(value));break;
    case SetIgnorePedal:if(i>=0&&i<slotCount){slots[(size_t)i].ignorePedal=value>=.5f;if(activeScene>=0)scenes[(size_t)activeScene.load()].parts[(size_t)i].ignorePedal=value>=.5f;syncPedal(i);}break;
    case SequenceGain:sequence.gain=juce::jlimit(0.0f,1.0f,value);break;
    case SequencePlay:sequence.start();break;
    case SequenceStop:sequence.stop();break;
    case ClipPlay:playClip(i,value);break;
    case ClipStop:stopClip(i);break;
    case ClipStopAll:for(int c=0;c<clipCount;++c)stopClip(c);break;
    }
}
void Engine::route(const Event& e) {
    const int kind=e.a>>4,ch=e.a&15,num=e.b;
    if(kind<8 || kind>14 || num<0 || num>127 || e.c<0 || e.c>127) return;
    lastMidi=(e.a<<16)|(e.b<<8)|e.c;
    const bool release=(kind==8 || (kind==9&&e.c==0));
    if(release && consumed[(size_t)ch][(size_t)num]) { consumed[(size_t)ch][(size_t)num]=false;releaseNote(ch,num);return; }
    if(rangeLearning.load()&&kind==9&&e.c>0){rangeLearned=num;rangeLearning=false;consumed[(size_t)ch][(size_t)num]=true;return;}
    if(learning.load() && (kind==9&&e.c>0 || kind==11 || kind==12)) {
        learned=(e.a<<16)|(e.b<<8)|e.c;learning=false;
        if(kind==9) consumed[(size_t)ch][(size_t)num]=true;
        return;
    }
    bool matched=false;
    for(size_t j=0;j<bindings.size();++j) {
        auto& b=bindings[j];
        if(b.kind!=kind || (b.channel!=0&&b.channel!=ch+1) || b.number!=num) continue;
        matched=true;
        const float v=(kind==12?1.0f:(float)e.c/127.0f);
        bool continuous=b.action==Master||b.action==SlotGain||b.action==Parameter;
        bool down=kind==12 || e.c>0;
        if(continuous || (down && (kind!=11 || !pressed[j]))) execute(b.action,b.slot,b.minimum+v*(b.maximum-b.minimum),b.parameter);
        pressed[j]=down;
        if(kind==9&&down) consumed[(size_t)ch][(size_t)num]=true;
    }
    if(matched) return;
    if((kind==9&&e.c>0)||(kind==11&&(num==64||num==120||num==123)))for(auto voice:previewVoices)if(voice.target>=0&&voice.channel==ch){stopPreview();break;}
    uint8_t raw[3]={(uint8_t)e.a,(uint8_t)e.b,(uint8_t)e.c};
    juce::MidiMessage m(raw,(kind==12||kind==13)?2:3,0.0);
    if(kind==9 && e.c>0) {
        releaseNote(ch,num);
        inputKeys[(size_t)num].fetch_or((uint16_t)(1<<ch));
        for(int i=0;i<slotCount;++i)if(wanted(i)){
            if(num<slots[(size_t)i].keyLow||num>slots[(size_t)i].keyHigh)continue;
            const int target=num+slots[(size_t)i].transpose.load()+performanceTranspose.load();if(target<0||target>127)continue;
            noteTargets[(size_t)i][(size_t)ch][(size_t)num]=(int16_t)target;owners[(size_t)ch][(size_t)num]|=(uint8_t)(1<<i);
            dispatch(i,juce::MidiMessage::noteOn(ch+1,target,(juce::uint8)e.c));}
    } else if(release) {
        releaseNote(ch,num);
    } else if(kind==11&&num==64) {
        sustain[(size_t)ch]=e.c;pedalDown=std::any_of(sustain.begin(),sustain.end(),[](int v){return v>=64;});
        for(int i=0;i<slotCount;++i)if(wanted(i)&&!slots[(size_t)i].ignorePedal)sendPedal(i,ch,e.c);
    } else if(kind==11&&(num==120||num==123)) {
        for(auto& key:inputKeys)key.fetch_and((uint16_t)~(1<<ch));
        for(int i=0;i<slotCount;++i) if(slots[(size_t)i].running.load()){sendPedal(i,ch,0);dispatch(i,m);}
        owners[(size_t)ch].fill(0);sustain[(size_t)ch]=0;pedalDown=std::any_of(sustain.begin(),sustain.end(),[](int v){return v>=64;});
        for(auto& slot:noteTargets)slot[(size_t)ch].fill(-1);
    } else if(kind==10){for(int i=0;i<slotCount;++i){int target=noteTargets[(size_t)i][(size_t)ch][(size_t)num];if(target>=0)dispatch(i,juce::MidiMessage::aftertouchChange(ch+1,target,e.c));}}
    else for(int i=0;i<slotCount;++i)if(wanted(i))dispatch(i,m);
}
void Engine::render(float* const* out,int channels,int n) {
    const auto start=juce::Time::getHighResolutionTicks();
    juce::ScopedNoDenormals noDenormals;
    for(int c=0;c<channels;++c) if(out[c]) juce::FloatVectorOperations::clear(out[c],n);
    if(n<=0)return;
    if(n>blockSize.load()) { ++dropped;overflowPanic=true;return; }
    for(auto& s:slots) s.midi.clear();
    sequence.beginBlock();
    Event e;int count=0;
    if(remotePitch!=8192&&(juce::int32)(juce::Time::getMillisecondCounter()-remotePitchUntil)>=0)
        setRemotePitch(8192);
    while(count++<4096 && queue.pop(e)) {
        if(e.type==Event::SceneEdit){if(e.a>=0&&e.a<sceneCount&&e.b>=0&&e.b<slotCount&&validPart(e.part)){scenes[(size_t)e.a].parts[(size_t)e.b]=e.part;if(activeScene==e.a){applyScenePart(e.b,e.part,true);refreshActive();}}}
        else if(e.type==Event::PedalSettings){if(e.a>=0&&e.a<sceneCount&&e.b>=0&&e.b<slotCount){scenes[(size_t)e.a].parts[(size_t)e.b].ignorePedal=e.c!=0;if(activeScene==e.a){slots[(size_t)e.b].ignorePedal=e.c!=0;syncPedal(e.b);}}}
        else if(e.type==Event::GainSettings){if(e.a>=0&&e.a<sceneCount&&e.b>=0&&e.b<slotCount&&std::isfinite(e.value)){auto v=juce::jlimit(0.0f,1.0f,e.value);scenes[(size_t)e.a].parts[(size_t)e.b].gain=v;if(activeScene==e.a)execute(SlotGain,e.b,v,0);}}
        else if(e.type==Event::SequenceEdit)sequence.edit(e.a,e.note);
        else if(e.type==Event::SequenceSettings)sequence.settings(e.a,e.b,e.c!=0);
        else if(e.type==Event::SequencePreview)sequence.preview(e.a,e.b);
        else if(e.type==Event::LivePreview)startPreview(e.a,e.b);
        else if(e.type==Event::Midi) route(e);
        else if(e.type==Event::RemoteBatch) {
            for(int n=0;n<e.a;++n)execute(e.batch[n].action,e.batch[n].index,e.batch[n].value,0);
        }
        else if(e.type==Event::Control) execute(e.a,e.b,e.value,e.c);
        else if(e.type==Event::Bind&&e.a>=0&&e.a<mappingCount) {bindings[(size_t)e.a]=e.mapping;pressed[(size_t)e.a]=false;}
        else if(e.type==Event::ClearBindings) {bindings.fill({});pressed.fill(false);}
    }
    if(overflowPanic.exchange(false)) execute(Panic,0,0,0);
    if(previewLeft>0){previewLeft-=n;if(previewLeft<=0)stopPreview();}
    int running=0;
    for(int i=0;i<slotCount;++i) {
        auto& s=slots[(size_t)i];
        if(!s.plugin || !s.running.load()) {s.peak=0;s.meter.push({}, {},n,rate.load());continue;}
        ++running;s.buffer.clear();
        juce::AudioBuffer<float> block(s.buffer.getArrayOfWritePointers(),s.buffer.getNumChannels(),n);
        s.plugin->processBlock(block,s.midi);
        if(s.plugin->getTotalNumOutputChannels()==1)block.copyFrom(1,0,block,0,0,n);
        s.fx.process(block);
        float peak=0;std::array<float,2> maxima{};std::array<double,2> squares{};
        for(int k=0;k<n;++k) {
            float gain=s.gate.getNextValue()*s.trim.getNextValue();
            for(int c=0;c<std::min(channels,2);++c) {
                int pc=c;
                float sample=block.getSample(pc,k)*gain;
                if(!std::isfinite(sample)) sample=0;
                maxima[(size_t)c]=juce::jmax(maxima[(size_t)c],std::abs(sample));squares[(size_t)c]+=(double)sample*sample;peak=std::max(peak,std::abs(sample));if(out[c]) out[c][k]+=sample;
            }
        }
        s.peak=peak;s.meter.push(maxima,squares,n,rate.load(),peak>1);
        if(s.retiring) {
            s.tailLeft=(s.gate.getTargetValue()!=0?std::min(s.tailLeft,(int64_t)(tailSeconds.load()*rate.load())):s.tailLeft)-n;
            s.tailActive=true;
            s.tailRemaining=(float)(juce::jmax((int64_t)0,s.tailLeft)/rate.load());
            if(s.tailLeft<=0 && s.gate.getTargetValue()!=0) {s.gate.reset(rate.load(),0.01);s.gate.setTargetValue(0);}
            if(s.gate.getCurrentValue()==0 && !s.gate.isSmoothing()) {
                s.midi.clear();silence(i,true);block.clear();s.plugin->processBlock(block,s.midi);s.fx.reset();s.running=false;s.tailActive=false;s.tailRemaining=0;
            }
        }
    }
    renderClips(out,channels,n);
    sequence.render(out,channels,n);
    if(masterFx.count()>0){
        masterFxBuffer.clear();for(int ch=0;ch<juce::jmin(channels,2);++ch)if(out[ch])masterFxBuffer.copyFrom(ch,0,out[ch],n);
        juce::AudioBuffer<float> block(masterFxBuffer.getArrayOfWritePointers(),2,n);masterFx.process(block);
        for(int ch=0;ch<juce::jmin(channels,2);++ch)if(out[ch])juce::FloatVectorOperations::copy(out[ch],block.getReadPointer(ch),n);
    }
    float peak=0;std::array<float,2> maxima{};std::array<double,2> squares{};
    for(int k=0;k<n;++k) {
        float gain=master.getNextValue();
        for(int c=0;c<channels;++c) if(out[c]) {
            float v=out[c][k]*gain;peak=std::max(peak,std::abs(v));out[c][k]=juce::jlimit(-1.0f,1.0f,v);if(c<2){maxima[(size_t)c]=juce::jmax(maxima[(size_t)c],std::abs(out[c][k]));squares[(size_t)c]+=(double)out[c][k]*out[c][k];}
        }
    }
    outputPeak=peak;outputMeter.push(maxima,squares,n,rate.load(),peak>1);activeVoices=running;
    recorder.push(out,channels,n);
    movie.push(out,channels,n);
    float used=(float)(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-start)*rate.load()/n*100.0);
    cpuPercent=cpuPercent.load()*0.9f+used*0.1f;
}
void Engine::audioDeviceIOCallbackWithContext(const float* const*,int,float* const* out,int channels,int n,const juce::AudioIODeviceCallbackContext&) {render(out,channels,n);}
void Engine::audioDeviceAboutToStart(juce::AudioIODevice* d) {prepare(d->getCurrentSampleRate(),d->getCurrentBufferSizeSamples());}
void Engine::audioDeviceStopped() {movie.requestStop(Recorder::Reason::DeviceChanged);recorder.requestStop(Recorder::Reason::DeviceChanged);if(sequence.plugin){sequence.plugin->reset();sequence.plugin->releaseResources();}for(auto& s:slots) if(s.plugin) {s.plugin->reset();s.plugin->releaseResources();}for(auto& s:slots)s.fx.release();masterFx.release();resetRouting();}
void Engine::handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage& m) {if(m.getRawDataSize()>0&&m.getRawDataSize()<=3) {auto* d=m.getRawData();midi(d[0],m.getRawDataSize()>1?d[1]:0,m.getRawDataSize()>2?d[2]:0);}}
}
