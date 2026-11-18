#include "Engine.h"
#include <thread>
#include <limits>
#include <vector>

namespace stage {
int runVideoTests(const juce::File& report){
    using namespace juce;report.getParentDirectory().createDirectory();
    auto folder=report.getParentDirectory().getNonexistentChildFile("video-test-data","",false);folder.createDirectory();
    Array<var> checks;Array<var> movies;int failures=0;
    auto check=[&](bool ok,const String& title){auto* o=new DynamicObject();o->setProperty("test",title);o->setProperty("passed",ok);checks.add(var(o));if(!ok)++failures;};
    auto wait=[&](const std::function<bool()>& done,int ms=15000){auto end=Time::getMillisecondCounter()+(uint32)ms;while(Time::getMillisecondCounter()<end){if(done())return true;Thread::sleep(2);}return done();};
    String error;VideoRecorder empty;VideoRecorder::Config cfg;
    check(!empty.isBusy()&&!empty.isCapturing()&&empty.phase()==VideoRecorder::Phase::Idle,"Idle movie mode starts no video processing thread");
    check(!empty.start(cfg,error)&&!empty.isBusy(),"Starting without an explicitly selected camera is rejected");
    cfg.cameraId="stagepilot-test-only";cfg.target=folder.getChildFile("existing.mp4");cfg.target.replaceWithText("keep");
    check(!empty.start(cfg,error)&&cfg.target.loadFileAsString()=="keep","Existing movies are never overwritten");
    cfg.target=folder.getChildFile("bad-rate.mp4");cfg.sampleRate=0;
    check(!empty.start(cfg,error)&&!cfg.target.exists(),"Invalid audio configuration is rejected before activating a camera");
    {
        VideoRecorder::Config c;c.cameraId="stagepilot-test-only";c.target=folder.getChildFile("validation-only.mp4");
        check(VideoRecorder::validateConfig(c).isEmpty(),"Default 1080p capture settings pass configuration validation without camera activation");
        check(VideoRecorder::supportsFormat(c,1920,1080,30)&&!VideoRecorder::supportsFormat(c,1280,720,30)&&!VideoRecorder::supportsFormat(c,1920,1080,15),"Strict requested format excludes silent resolution and frame-rate downgrades");
        c.preferredFps=60;check(VideoRecorder::supportsFormat(c,1920,1080,59.94)&&!VideoRecorder::supportsFormat(c,1920,1080,30),"60fps strict mode permits nominal 59.94 but rejects a 30fps camera mode");
        c.strictFormat=false;check(VideoRecorder::supportsFormat(c,1280,720,30),"Explicit fallback mode permits a supported lower camera mode");
        c={};c.cameraId="stagepilot-test-only";c.target=folder.getChildFile("validation-only.mp4");
        std::vector<std::pair<String,std::function<void(VideoRecorder::Config&)>>> invalid={
            {"zero frame rate",[](auto& v){v.preferredFps=0;}},{"nonfinite frame rate",[](auto& v){v.preferredFps=std::numeric_limits<double>::quiet_NaN();}},
            {"odd H.264 width",[](auto& v){v.preferredWidth=1919;}},{"oversized height",[](auto& v){v.preferredHeight=2162;}},
            {"zero bitrate",[](auto& v){v.bitrateMbps=0;}},{"out-of-range quality",[](auto& v){v.quality=101;}},
            {"unknown H.264 profile",[](auto& v){v.profile=static_cast<VideoRecorder::Profile>(1);}},{"unknown encoder",[](auto& v){v.encoder=static_cast<VideoRecorder::Encoder>(999);}},
            {"oversized GOP interval",[](auto& v){v.keyframeSeconds=11;}},{"unsupported AAC bitrate",[](auto& v){v.audioBitrateKbps=256;}}
        };
        for(auto& setting:invalid){auto bad=c;setting.second(bad);VideoRecorder recorder;String rejection;bool accepted=recorder.start(bad,rejection);check(!VideoRecorder::validateConfig(bad).isEmpty()&&!accepted&&!recorder.isBusy()&&!rejection.isEmpty()&&!bad.target.exists(),"Invalid "+setting.first+" rejected before starting camera worker");recorder.requestStop();recorder.finish();}
        c.preferredFps=60;c.profile=VideoRecorder::Profile::High;c.encoder=VideoRecorder::Encoder::Software;c.bitrateMbps=18;c.keyframeSeconds=2;c.audioBitrateKbps=160;c.rateControl=VideoRecorder::RateControl::QualityVbr;c.quality=92;c.strictFormat=false;
        auto value=VideoRecorder::configToJson(c);VideoRecorder::Config restored;String parseError;bool read=VideoRecorder::configFromJson(value,restored,parseError);
        check(read&&restored.preferredWidth==c.preferredWidth&&restored.preferredHeight==c.preferredHeight&&restored.preferredFps==60&&restored.profile==c.profile&&restored.encoder==c.encoder&&restored.bitrateMbps==18&&restored.keyframeSeconds==2&&restored.audioBitrateKbps==160&&restored.rateControl==c.rateControl&&restored.quality==92&&!restored.strictFormat,"Detailed camera settings round-trip without losing fps, rate control, profile, GOP, encoder or audio bitrate");
    }
    cfg={};cfg.preferredWidth=320;cfg.preferredHeight=240;
    check(empty.begin(cfg,true,true,error),"Synthetic preview starts without activating physical cameras");
    bool previewed=wait([&]{return empty.preview().isValid();});
    if(previewed){PNGImageFormat png;FileOutputStream out(folder.getChildFile("synthetic-preview.png"));png.writeImageToStream(empty.preview(),out);}
    empty.requestStop();bool closed=wait([&]{return empty.pollFinished();});
    check(previewed&&closed&&!empty.isBusy()&&!empty.movieFile().existsAsFile(),"Closing preview releases its processing thread without recording files");

    auto engine=std::make_unique<Engine>();auto& e=*engine;e.slots[0].plugin=makeTestSynth();e.slots[1].plugin=makeTestSynth();
    for(int take=0;take<6;++take){
        int rate=take==1?96000:take==2?44100:48000;int block=rate==44100?441:rate/200;
        e.prepare(rate,block);VideoRecorder::Config c;c.sampleRate=rate;c.preferredWidth=320;c.preferredHeight=240;c.target=folder.getChildFile("take-"+String(take)+".mp4");if(take==3)c.videoDelayMs=150;if(take==4)c.videoDelayMs=-150;
        if(take==5)c.target=folder.getChildFile(String::fromUTF8("演奏動画 テスト かな.mp4"));
        bool started=e.movie.begin(c,false,true,error);check(started,"Synthetic video take starts");
        bool ready=wait([&]{return e.movie.phase()==VideoRecorder::Phase::Recording||e.movie.phase()==VideoRecorder::Phase::Failed;});
        bool active=ready&&e.movie.isCapturing();check(active,"Windows H.264 recorder reaches active state");
        if(active){check(!e.movie.start(c,error),"A second recording cannot replace an active take");
            AudioBuffer<float> data(2,block);e.midi(0x90,60,110);
            for(int n=0;n<(rate==44100?75:150);++n){if(n==35)e.control(Select,1);if(n==90)e.control(Panic);e.render(data.getArrayOfWritePointers(),2,block);Thread::sleep(rate==44100?10:5);}
        }
        e.movie.requestStop();bool saved=wait([&]{return e.movie.pollFinished();},30000);
        check(saved&&e.movie.phase()==VideoRecorder::Phase::Complete&&c.target.getSize()>1024,"Windows MP4 mux finalises H.264 camera frames with AAC performance audio");
        if(e.movie.error().isNotEmpty()){auto* o=new DynamicObject();o->setProperty("take",take);o->setProperty("error",e.movie.error());movies.add(var(o));}
        AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<AudioFormatReader> audio(formats.createReaderFor(c.target.withFileExtension("wav")));
        check(audio&&audio->sampleRate==rate&&audio->numChannels==2&&audio->bitsPerSample==24&&audio->lengthInSamples>rate/2,"Original stereo 24-bit WAV is retained at the device sample rate");
        check(!c.target.withFileExtension("camera-part.mp4").existsAsFile(),"Successful mux removes only its own temporary video file");
        auto* item=new DynamicObject();item->setProperty("file",c.target.getFullPathName());item->setProperty("inputRate",rate);item->setProperty("videoDelayMs",c.videoDelayMs);item->setProperty("name","legacy-"+String(take));item->setProperty("width",320);item->setProperty("height",240);item->setProperty("fps",30);item->setProperty("pattern","red-green");item->setProperty("audioBitrateKbps",c.audioBitrateKbps);movies.add(var(item));
    }
    for(int take=0;take<5;++take){
        e.prepare(48000,240);VideoRecorder::Config c;c.sampleRate=48000;c.preferredWidth=take<2?1920:1280;c.preferredHeight=take<2?1080:720;c.preferredFps=take==1||take==3?30:60;
        c.encoder=VideoRecorder::Encoder::Software;c.profile=take==1?VideoRecorder::Profile::Main:take==2?VideoRecorder::Profile::Baseline:VideoRecorder::Profile::High;
        c.bitrateMbps=take==0?10:take==2?6:8;c.keyframeSeconds=1;c.audioBitrateKbps=take==1?128:take==2?160:192;c.rateControl=take==3?VideoRecorder::RateControl::QualityVbr:VideoRecorder::RateControl::CBR;c.quality=92;
        if(take==4){c.preferredWidth=3840;c.preferredHeight=2160;c.preferredFps=30;c.profile=VideoRecorder::Profile::High;c.bitrateMbps=45;c.keyframeSeconds=0;}
        c.target=folder.getChildFile("advanced-"+String(take)+".mp4");String startError;
        bool started=e.movie.begin(c,false,true,startError,{},true);check(started,"Advanced synthetic recording accepts validated detailed configuration "+String(take));
        bool ready=started&&wait([&]{return e.movie.phase()==VideoRecorder::Phase::Recording||e.movie.phase()==VideoRecorder::Phase::Failed;},30000);bool active=ready&&e.movie.isCapturing();
        check(active,"Detailed software H.264 encoder becomes active "+String(take)+" / "+e.movie.error());
        if(active){AudioBuffer<float> data(2,240);e.midi(0x90,60,110);for(int n=0;n<(take==4?160:300);++n){e.render(data.getArrayOfWritePointers(),2,240);Thread::sleep(5);}}
        e.movie.requestStop();bool finished=wait([&]{return e.movie.pollFinished();},60000);
        check(finished&&e.movie.phase()==VideoRecorder::Phase::Complete&&c.target.getSize()>1024,"Detailed resolution/fps/profile recording finalizes MP4 and performance audio "+String(take)+" / "+e.movie.error());
        AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<AudioFormatReader> audio(formats.createReaderFor(c.target.withFileExtension("wav")));
        check(audio&&audio->sampleRate==48000&&audio->numChannels==2&&audio->bitsPerSample==24&&audio->lengthInSamples>(take==4?24000:48000),"Detailed capture retains independent original stereo 24-bit WAV "+String(take));
        auto* item=new DynamicObject();item->setProperty("file",c.target.getFullPathName());item->setProperty("name","advanced-"+String(take));item->setProperty("inputRate",48000);item->setProperty("videoDelayMs",0);item->setProperty("width",c.preferredWidth);item->setProperty("height",c.preferredHeight);item->setProperty("fps",c.preferredFps);item->setProperty("bitrateMbps",c.bitrateMbps);item->setProperty("profile",(int)c.profile);item->setProperty("encoder",(int)c.encoder);item->setProperty("keyframeSeconds",c.keyframeSeconds);item->setProperty("audioBitrateKbps",c.audioBitrateKbps);item->setProperty("rateControl",(int)c.rateControl);item->setProperty("quality",c.quality);item->setProperty("pattern","moving-detail");if(e.movie.error().isNotEmpty())item->setProperty("error",e.movie.error());movies.add(var(item));
    }
    {
        e.prepare(48000,240);VideoRecorder::Config c;c.preferredWidth=320;c.preferredHeight=240;c.target=folder.getChildFile("camera-failure.mp4");e.movie.begin(c,false,true,error);
        wait([&]{return e.movie.isCapturing()||e.movie.phase()==VideoRecorder::Phase::Failed;});AudioBuffer<float> data(2,240);e.midi(0x90,64,110);
        for(int i=0;i<70;++i){e.render(data.getArrayOfWritePointers(),2,240);Thread::sleep(5);}
        e.movie.failForTest();bool saved=wait([&]{return e.movie.pollFinished();},30000);
        check(saved&&e.movie.phase()==VideoRecorder::Phase::Failed&&e.movie.error().isNotEmpty()&&c.target.withFileExtension("wav").getSize()>100,"Camera failures are reported and retain the original audio backup");
    }
    {
        e.prepare(48000,240);e.midi(0x90,60,110);std::atomic<bool> pumping{true};
        std::thread producer([&]{AudioBuffer<float> data(2,240);while(pumping.load()){e.render(data.getArrayOfWritePointers(),2,240);Thread::sleep(5);}});
        VideoRecorder::Config c;c.preferredWidth=320;c.preferredHeight=240;c.target=folder.getChildFile("async-capture.mp4");
        bool started=e.movie.begin(c,false,false,error,folder.getChildFile("take-0.mp4"));
        bool saved=started&&wait([&]{return e.movie.pollFinished();},30000);pumping=false;producer.join();
        check(saved&&e.movie.phase()==VideoRecorder::Phase::Complete&&c.target.getSize()>1024&&c.target.withFileExtension("wav").getSize()>104,"Asynchronous Source Reader callback captures RGB frames, encodes and muxes without physical camera access");
        auto* item=new DynamicObject();item->setProperty("file",c.target.getFullPathName());item->setProperty("name","async-capture");item->setProperty("inputRate",48000);item->setProperty("width",320);item->setProperty("height",240);item->setProperty("fps",30);item->setProperty("pattern","async-red-green");item->setProperty("audioBitrateKbps",c.audioBitrateKbps);if(e.movie.error().isNotEmpty())item->setProperty("error",e.movie.error());movies.add(var(item));
    }
    {
        VideoRecorder invalid;VideoRecorder::Config c;c.cameraId="stagepilot-nonexistent-camera";c.target=folder.getChildFile("missing-camera.mp4");
        bool requested=invalid.start(c,error);bool finished=wait([&]{return invalid.pollFinished();});
        check(requested&&finished&&invalid.phase()==VideoRecorder::Phase::Failed&&invalid.error().isNotEmpty(),"A missing camera fails asynchronously with a visible error");
    }
    auto* root=new DynamicObject();root->setProperty("failures",failures);root->setProperty("physicalCameraActivated",false);root->setProperty("physicalAudioDeviceOpened",false);root->setProperty("results",checks);root->setProperty("movies",movies);root->setProperty("dataFolder",folder.getFullPathName());
    report.replaceWithText(JSON::toString(var(root)));return failures?1:0;
}
}
