#include "Engine.h"
#include <vector>
#include <cmath>

namespace stage {
namespace {
class ProbeWriter : public juce::AudioFormatWriter {
public:
    ProbeWriter(std::atomic<bool>& called, std::atomic<bool>& gate, bool failing,
                std::thread::id source, std::atomic<bool>& separate)
        : AudioFormatWriter(new juce::MemoryOutputStream(),"Probe",48000,2,24),
          entered(called), release(gate), fail(failing), producer(source), differentThread(separate) {}
    bool write(const int** , int) override {
        differentThread = std::this_thread::get_id()!=producer;
        entered=true;
        while(!release.load())juce::Thread::sleep(1);
        return !fail;
    }
    bool flush() override {return !fail;}
private:
    std::atomic<bool>& entered;
    std::atomic<bool>& release;
    bool fail;
    std::thread::id producer;
    std::atomic<bool>& differentThread;
};
bool waitFor(const std::function<bool()>& condition) {
    auto deadline=juce::Time::getMillisecondCounter()+5000;
    while(juce::Time::getMillisecondCounter()<deadline){if(condition())return true;juce::Thread::sleep(1);}
    return condition();
}
}

int runRecordingTests(const juce::File& report) {
    using namespace juce;
    report.getParentDirectory().createDirectory();
    auto folder=report.getParentDirectory().getNonexistentChildFile("recording-data","",false);folder.createDirectory();
    Array<var> results;int failures=0;
    auto check=[&](bool ok,const char* text){auto* r=new DynamicObject();r->setProperty("test",text);r->setProperty("passed",ok);results.add(var(r));if(!ok)++failures;};
    AudioFormatManager formats;formats.registerBasicFormats();
    auto read=[&](File f){return std::unique_ptr<AudioFormatReader>(formats.createReaderFor(f));};
    AudioBuffer<float> block(2,256);for(int i=0;i<256;++i){block.setSample(0,i,.24f*std::sin(i*.1f));block.setSample(1,i,-.13f*std::cos(i*.1f));}
    String error;double copyMicroseconds=0;
    {
        Recorder recorder;
        check(!recorder.isBusy()&&!recorder.isCapturing(),"Idle recorder has no disk thread");
        auto file=folder.getChildFile("stereo.wav");
        check(recorder.start(file,48000,error),"Start 24-bit stereo WAV at output sample rate");
        auto before=block.getSample(0,50);
        for(int n=0;n<40;++n)recorder.push(block.getArrayOfWritePointers(),2,256);
        recorder.requestStop();check(!recorder.isCapturing(),"Stop immediately detaches the audio producer");
        recorder.finish();auto reader=read(file);
        check(reader&&reader->sampleRate==48000&&reader->numChannels==2&&reader->bitsPerSample==24&&reader->lengthInSamples==10240,"Finalised WAV has exact frame count and metadata");
        AudioBuffer<float> restored(2,10240);bool exact=reader&&reader->read(&restored,0,10240,0,true,true);
        if(exact)for(int c=0;c<2;++c)for(int n=0;n<10240;++n)exact&=std::abs(restored.getSample(c,n)-block.getSample(c,n%256))<.000001f;
        check(exact&&block.getSample(0,50)==before,"Stereo channels round trip within 24-bit precision and playback buffers remain unchanged");
        check(!recorder.start(file,48000,error),"An existing recording is never overwritten");
        check(!recorder.start(folder.getChildFile("bad-rate.wav"),0,error),"Invalid device sample rate is rejected");
        auto blocker=folder.getChildFile("not-a-directory");blocker.replaceWithText("keep");
        check(!recorder.start(blocker.getChildFile("fail.wav"),48000,error),"Unwritable recording destination is reported without starting");
        check(recorder.start(folder.getChildFile("next.wav"),44100,error),"Next take starts at a different device sample rate");
        recorder.push(block.getArrayOfWritePointers(),2,256);recorder.finish();auto next=read(folder.getChildFile("next.wav"));
        check(next&&next->sampleRate==44100&&next->lengthInSamples==256,"Restart contains only the new take");
    }
    {
        Recorder recorder;auto file=folder.getChildFile("mono-null.wav");recorder.start(file,48000,error);
        float* mono[]={block.getWritePointer(0)};recorder.push(mono,1,256);
        float* missing[]={nullptr,block.getWritePointer(1)};recorder.push(missing,2,256);recorder.finish();
        auto reader=read(file);AudioBuffer<float> audio(2,512);bool ok=reader&&reader->read(&audio,0,512,0,true,true);
        if(ok)for(int i=0;i<256;++i)ok&=std::abs(audio.getSample(0,i)-audio.getSample(1,i))<.000001f&&audio.getSample(0,256+i)==0;
        check(ok,"Mono output duplicates safely and missing channels record silence");
    }
    {
        Recorder recorder;auto file=folder.getChildFile("wrap.wav");
        std::unique_ptr<OutputStream> output=file.createOutputStream();WavAudioFormat wav;
        auto writer=wav.createWriterFor(output,AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        recorder.begin(std::move(writer),48000,1025,error);
        bool wrapped=true;double elapsed=0;
        for(int n=0;n<320;++n){wrapped&=waitFor([&]{return recorder.fifo->getFreeSpace()>=256;});auto begin=Time::getMillisecondCounterHiRes();recorder.push(block.getArrayOfWritePointers(),2,256);elapsed+=Time::getMillisecondCounterHiRes()-begin;}
        copyMicroseconds=elapsed*1000/320;recorder.finish();auto reader=read(file);AudioBuffer<float> audio(2,81920);
        wrapped&=reader&&reader->lengthInSamples==81920&&reader->read(&audio,0,81920,0,true,true);
        if(wrapped)for(int c=0;c<2;++c)for(int i=0;i<81920;++i)wrapped&=std::abs(audio.getSample(c,i)-block.getSample(c,i%256))<.000001f;
        check(wrapped,"Repeated FIFO wrap-around preserves every sample and stereo channel");
    }
    {
        Recorder recorder;std::atomic<bool> entered{false},release{false},different{false};
        recorder.begin(std::make_unique<ProbeWriter>(entered,release,false,std::this_thread::get_id(),different),48000,1025,error);
        recorder.push(block.getArrayOfWritePointers(),2,256);
        bool blocked=waitFor([&]{return entered.load();});
        const auto begin=Time::getMillisecondCounterHiRes();
        for(int n=0;n<12;++n)recorder.push(block.getArrayOfWritePointers(),2,256);
        const auto elapsed=Time::getMillisecondCounterHiRes()-begin;
        check(blocked&&elapsed<100&&recorder.getReason()==Recorder::Reason::Overflow&&!recorder.isCapturing(),"A blocked disk never waits on the audio thread; full buffer stops recording explicitly");
        release=true;recorder.finish();check(different.load(),"All writer calls run on a separate disk thread");
    }
    {
        Recorder recorder;std::atomic<bool> entered{false},release{true},different{false};
        recorder.begin(std::make_unique<ProbeWriter>(entered,release,true,std::this_thread::get_id(),different),48000,4097,error);
        recorder.push(block.getArrayOfWritePointers(),2,256);
        bool failed=waitFor([&]{return !recorder.isCapturing();});recorder.finish();
        check(failed&&recorder.getReason()==Recorder::Reason::WriteError,"Disk write failures stop capture and are reported");
    }
    {
        Recorder recorder;auto file=folder.getChildFile("limit.wav");recorder.frameLimit=512;recorder.start(file,48000,error);
        for(int n=0;n<4;++n)recorder.push(block.getArrayOfWritePointers(),2,256);recorder.finish();auto reader=read(file);
        check(recorder.getReason()==Recorder::Reason::FileLimit&&reader&&reader->lengthInSamples==512,"WAV size limit stops cleanly before exceeding RIFF capacity");
    }
    {
        auto engine=std::make_unique<Engine>();auto& e=*engine;
        e.slots[0].plugin=makeTestSynth();e.slots[1].plugin=makeTestSynth();e.clips[0].data=makeDemoClip(0);
        e.sequence.pattern.notes[0]={72,0,1,100,true};e.prepare(48000,256);
        auto file=folder.getChildFile("performance.wav");e.recorder.start(file,48000,error);
        AudioBuffer<float> expected(2,256*160);
        e.control(SetLayer,1,1);e.midi(0x90,60,95);e.control(ClipPlay,0,1);e.control(SequencePlay);e.control(Master,0,.42f);
        auto tick=[&](int n){e.render(block.getArrayOfWritePointers(),2,256);for(int c=0;c<2;++c)expected.copyFrom(c,n*256,block,c,0,256);};
        for(int n=0;n<160;++n){if(n==30)e.control(Select,1);if(n==65){e.fadeSeconds=.01f;e.control(FadeOut);}if(n==100)e.control(Restore);if(n==130)e.control(Panic);tick(n);}
        e.recorder.finish();auto reader=read(file);AudioBuffer<float> audio(2,expected.getNumSamples());
        bool same=reader&&reader->lengthInSamples==expected.getNumSamples()&&reader->read(&audio,0,audio.getNumSamples(),0,true,true);
        if(same)for(int c=0;c<2;++c)for(int n=0;n<audio.getNumSamples();++n)same&=std::abs(audio.getSample(c,n)-expected.getSample(c,n))<.000001f;
        check(same&&audio.getMagnitude(0,audio.getNumSamples())>.01f,"Recorded waveform matches the final master mix including layers, clips, sequence, switching, fade, restore and panic");
        check(audio.getMagnitude(0,256*90,256*8)<.000001f&&audio.getMagnitude(0,256*131,256*20)<.000001f,"Fade and panic silence are preserved in the recording");
        e.recorder.start(folder.getChildFile("device-stop.wav"),48000,error);tick(0);e.audioDeviceStopped();e.recorder.finish();
        check(e.recorder.getReason()==Recorder::Reason::DeviceChanged,"Device stop finalises the take rather than leaving an open WAV");
        e.prepare(48000,256);e.recorder.start(folder.getChildFile("rate-change.wav"),48000,error);tick(0);e.prepare(44100,256);e.recorder.finish();
        check(e.recorder.getReason()==Recorder::Reason::DeviceChanged,"Sample-rate change stops before mixing incompatible rates into one WAV");
    }
    {
        auto file=folder.getChildFile("shutdown.wav");
        {Recorder recorder;recorder.start(file,48000,error);recorder.push(block.getArrayOfWritePointers(),2,256);}
        auto reader=read(file);check(reader&&reader->lengthInSamples==256,"Recorder destruction drains pending data and closes the WAV");
        Recorder recorder;file=folder.getChildFile("empty.wav");recorder.start(file,48000,error);recorder.finish();reader=read(file);
        check(reader&&reader->lengthInSamples==0,"An immediate stop produces a valid empty WAV");
    }
    {
        Recorder recorder;std::atomic<bool> running{true};std::atomic<int> calls{0};
        std::thread audio([&]{while(running.load()){recorder.push(block.getArrayOfWritePointers(),2,256);++calls;Thread::sleep(1);}});
        bool ok=true;for(int take=0;take<16;++take){auto file=folder.getChildFile("concurrent-"+String(take)+".wav");ok&=recorder.start(file,48000,error);Thread::sleep(4);recorder.requestStop();ok&=waitFor([&]{return recorder.pollFinished();});auto reader=read(file);ok&=reader&&reader->lengthInSamples%256==0;}
        running=false;audio.join();check(ok&&calls>0,"Concurrent callback and sixteen start/stop cycles drain safely");
    }
    auto* root=new DynamicObject();root->setProperty("failures",failures);root->setProperty("audioDeviceOpened",false);root->setProperty("results",results);root->setProperty("dataFolder",folder.getFullPathName());root->setProperty("meanCopyMicrosecondsPer256FrameBlock",copyMicroseconds);root->setProperty("timingScope","Audio producer copy only, excluding disk CPU; not a physical latency benchmark");
    report.replaceWithText(JSON::toString(var(root)));return failures?1:0;
}
}
