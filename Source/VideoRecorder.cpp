#include "VideoRecorder.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <icodecapi.h>
#include <limits>
#include <numeric>
#include <wrl/client.h>
#include <cmath>
#include <mutex>
#include <thread>

namespace stage {
namespace {
using Microsoft::WRL::ComPtr;
using namespace juce;
constexpr LONGLONG second=10000000;
constexpr DWORD firstVideoStream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD allVideoStreams=static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
struct Failure { HRESULT code; String where; };
void require(HRESULT hr,const char* action) {if(FAILED(hr))throw Failure{hr,String::fromUTF8(action)};}
struct SystemModule {
    HMODULE module=nullptr;
    explicit SystemModule(const wchar_t* name){module=LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw Failure{HRESULT_FROM_WIN32(GetLastError()),String::fromUTF8("Windowsの動画機能（Media Feature Pack）の読み込み")};}
    ~SystemModule(){if(module)FreeLibrary(module);}
};
struct Platform {
    SystemModule base{L"mfplat.dll"},readWrite{L"mfreadwrite.dll"},core{L"mf.dll"};
    bool com=false,mf=false;
    Platform(){auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);require(hr,"動画処理の初期化");com=true;
        hr=MFStartup(MF_VERSION);if(FAILED(hr)){CoUninitialize();com=false;require(hr,"Windowsの動画機能の初期化");}mf=true;}
    ~Platform(){if(mf)MFShutdown();if(com)CoUninitialize();}
};
String failureText(const Failure& f){
    String advice;
    if(f.where.contains("H.264")||f.where.contains("エンコード")||f.where.contains("AAC"))advice=String::fromUTF8("選んだエンコード設定に対応していない可能性があります。解像度・fps・プロファイル・ビットレートや処理方式を変えてください。");
    else if(f.where.contains("形式")||f.where.contains("解像度"))advice=String::fromUTF8("指定した解像度・fpsにカメラが対応しているか確認してください。「指定の解像度・fpsを必須にする」をオフにすると、取得できる近い形式を実際の値とともに表示します。");
    else if(f.where.contains("保存")||f.where.contains("ファイル")||f.where.contains("結合"))advice=String::fromUTF8("保存先・空き容量・録画設定を確認してください。残っているWAVと映像素材は保持します。");
    else advice=String::fromUTF8("カメラの接続・他アプリでの使用・Windowsのカメラ設定を確認してください。");
    return f.where+String::fromUTF8("に失敗しました（")+String::toHexString((int)f.code)+String::fromUTF8("）。")+advice;
}
LONGLONG ticksToTime(int64_t ticks){return (LONGLONG)std::llround(Time::highResolutionTicksToSeconds(ticks)*second);}
ComPtr<IMFAttributes> attributes(){ComPtr<IMFAttributes> a;require(MFCreateAttributes(&a,5),"動画属性の作成");return a;}
ComPtr<IMFMediaType> media(){ComPtr<IMFMediaType> t;require(MFCreateMediaType(&t),"動画形式の作成");return t;}
ComPtr<IMFSample> bytesSample(const void* data,DWORD bytes,LONGLONG time,LONGLONG duration){
    ComPtr<IMFMediaBuffer> b;require(MFCreateMemoryBuffer(bytes,&b),"動画バッファーの確保");BYTE* target=nullptr;
    require(b->Lock(&target,nullptr,nullptr),"動画バッファーの書き込み");std::memcpy(target,data,bytes);b->Unlock();b->SetCurrentLength(bytes);
    ComPtr<IMFSample> s;require(MFCreateSample(&s),"動画サンプルの作成");s->AddBuffer(b.Get());s->SetSampleTime(time);s->SetSampleDuration(duration);return s;
}
void rateRatio(double hz,UINT32& numerator,UINT32& denominator){
    if(std::abs(hz-23.976)<.001){numerator=24000;denominator=1001;return;}
    if(std::abs(hz-29.97)<.001){numerator=30000;denominator=1001;return;}
    if(std::abs(hz-59.94)<.001){numerator=60000;denominator=1001;return;}
    numerator=(UINT32)std::llround(hz*1000);denominator=1000;auto divisor=std::gcd(numerator,denominator);numerator/=divisor;denominator/=divisor;
}
ComPtr<IMFSinkWriter> sink(const File& file,VideoRecorder::Encoder encoder=VideoRecorder::Encoder::Auto){auto a=attributes();a->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,encoder==VideoRecorder::Encoder::Auto?TRUE:FALSE);a->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE);
    ComPtr<IMFSinkWriter> w;require(MFCreateSinkWriterFromURL(file.getFullPathName().toWideCharPointer(),nullptr,a.Get(),&w),"動画ファイルの作成");return w;}
ComPtr<IMFMediaType> rgbType(UINT32 width,UINT32 height,UINT32 fps,UINT32 den){auto t=media();t->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);t->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
    t->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);MFSetAttributeSize(t.Get(),MF_MT_FRAME_SIZE,width,height);MFSetAttributeRatio(t.Get(),MF_MT_FRAME_RATE,fps,den);MFSetAttributeRatio(t.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);return t;}
void codecValue(ICodecAPI* codec,const GUID& key,ULONG expected,const char* label){
    // Some Media Foundation encoder wrappers do not implement IsModifiable.
    // SetValue followed by exact GetValue readback is the authoritative check.
    VARIANT value;VariantInit(&value);value.vt=VT_UI4;value.ulVal=expected;require(codec->SetValue(&key,&value),label);
    VARIANT actual;VariantInit(&actual);auto result=codec->GetValue(&key,&actual);bool equal=SUCCEEDED(result)&&((actual.vt==VT_UI4&&actual.ulVal==expected)||(actual.vt==VT_I4&&actual.lVal>=0&&(ULONG)actual.lVal==expected));VariantClear(&actual);
    if(!equal)throw Failure{FAILED(result)?result:E_FAIL,String::fromUTF8(label)+String::fromUTF8("（設定の反映を確認できません）")};
}
String profileName(UINT32 profile){if(profile==eAVEncH264VProfile_Base)return "Baseline";if(profile==eAVEncH264VProfile_Main)return "Main";if(profile==eAVEncH264VProfile_High)return "High";return String::fromUTF8("自動（プロファイル未確認）");}
DWORD addVideo(IMFSinkWriter* w,UINT32 width,UINT32 height,UINT32 fps,UINT32 den,const VideoRecorder::Config& config,String& applied,IMFMediaType* cameraInput=nullptr){
    auto out=rgbType(width,height,fps,den);out->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);out->SetUINT32(MF_MT_AVG_BITRATE,(UINT32)config.bitrateMbps*1000000u);
    if(config.profile!=VideoRecorder::Profile::Auto)out->SetUINT32(MF_MT_MPEG2_PROFILE,(UINT32)config.profile);
    DWORD index=0;require(w->AddStream(out.Get(),&index),"H.264の解像度・fps・プロファイル設定");auto input=rgbType(width,height,fps,den);require(w->SetInputMediaType(index,cameraInput?cameraInput:input.Get(),nullptr),"H.264エンコードの映像入力設定");
    ComPtr<ICodecAPI> codec;require(w->GetServiceForStream(index,GUID_NULL,IID_PPV_ARGS(&codec)),"H.264エンコード詳細設定へのアクセス");
    codecValue(codec.Get(),CODECAPI_AVEncCommonRateControlMode,config.rateControl==VideoRecorder::RateControl::CBR?eAVEncCommonRateControlMode_CBR:eAVEncCommonRateControlMode_Quality,"H.264のレート制御");
    if(config.rateControl==VideoRecorder::RateControl::CBR)codecValue(codec.Get(),CODECAPI_AVEncCommonMeanBitRate,(ULONG)config.bitrateMbps*1000000u,"H.264の目標ビットレート");
    else codecValue(codec.Get(),CODECAPI_AVEncCommonQuality,(ULONG)config.quality,"H.264の品質指定VBR");
    if(config.keyframeSeconds>0)codecValue(codec.Get(),CODECAPI_AVEncMPVGOPSize,(ULONG)std::llround((double)fps/den*config.keyframeSeconds),"H.264のキーフレーム間隔");
    UINT32 actualProfile=0;ComPtr<IMFTransform> transform;ComPtr<IMFMediaType> actualType;
    if(SUCCEEDED(w->GetServiceForStream(index,GUID_NULL,IID_PPV_ARGS(&transform)))&&SUCCEEDED(transform->GetOutputCurrentType(0,&actualType)))actualType->GetUINT32(MF_MT_MPEG2_PROFILE,&actualProfile);
    if(config.profile!=VideoRecorder::Profile::Auto&&actualProfile!=(UINT32)config.profile)throw Failure{MF_E_INVALIDMEDIATYPE,String::fromUTF8("H.264プロファイルの反映確認")};
    applied="H.264 "+profileName(actualProfile)+" / "+(config.rateControl==VideoRecorder::RateControl::CBR?"CBR "+String(config.bitrateMbps)+" Mbps":String::fromUTF8("品質VBR ")+String(config.quality))+(config.keyframeSeconds>0?String::fromUTF8(" / キーフレーム ")+String(config.keyframeSeconds)+String::fromUTF8("秒"):String::fromUTF8(" / キーフレーム自動"));
    applied+=config.encoder==VideoRecorder::Encoder::Software?String::fromUTF8(" / ソフトウェア指定"):String::fromUTF8(" / 自動（ハードウェア許可・使用有無未確認）");
    applied+=" / AAC "+String(config.audioBitrateKbps)+" kbps";return index;
}
struct StopFlag {std::atomic<bool> value{false};};
struct Capture {
    std::shared_ptr<StopFlag> stop;
    std::atomic<bool> localStop{false},done{false};
    std::atomic<VideoRecorder::Phase> phase{VideoRecorder::Phase::Preparing};
    std::atomic<int> width{0},height{0};
    std::atomic<int64_t> videoFrames{0},firstTicks{0};
    std::mutex gate,infoGate;
    String error,encodingDescription;
    Image image;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFSinkWriter> writer;
    ComPtr<IMFMediaType> cameraInput;
    UINT32 fps=30,den=1;DWORD stream=0;
    LONG stride=0;bool first=true,previewOnly=false,fileInput=false;
    LONGLONG sourceStart=0,lastVideoTime=-1;
    uint32 lastThumbnail=0;
    bool stopped() const {return localStop.load()||stop->value.load();}
    void fail(String text){std::lock_guard lock(infoGate);if(error.isEmpty())error=text;stop->value=true;}
    void thumbnail(IMFSample* sample){
        auto now=Time::getMillisecondCounter();if(now-lastThumbnail<125)return;lastThumbnail=now;
        ComPtr<IMFMediaBuffer> b;DWORD buffers=0;if(FAILED(sample->GetBufferCount(&buffers)))return;auto result=buffers==1?sample->GetBufferByIndex(0,&b):sample->ConvertToContiguousBuffer(&b);if(FAILED(result))return;BYTE* bytes=nullptr;DWORD length=0;
        if(FAILED(b->Lock(&bytes,nullptr,&length)))return;
        const int w=width.load(),h=height.load();const LONG pitch=stride?stride:w*4;
        if(length>=(DWORD)(std::abs(pitch)*h)){
            int tw=320,th=jmax(1,h*tw/w);Image next(Image::RGB,tw,th,false);Image::BitmapData bitmap(next,Image::BitmapData::writeOnly);
            auto* top=pitch<0?bytes+(h-1)*(-pitch):bytes;
            for(int y=0;y<th;++y){auto* row=top+(y*h/th)*pitch;for(int x=0;x<tw;++x){auto* p=row+(x*w/tw)*4;bitmap.setPixelColour(x,y,Colour(p[2],p[1],p[0]));}}
            std::lock_guard lock(infoGate);image=next;
        }b->Unlock();
    }
    void frame(IMFSample* sample,LONGLONG sourceTime){
        std::lock_guard lock(gate);if(stopped())return;
            if(previewOnly){thumbnail(sample);++videoFrames;return;}
        if(first){first=false;sourceStart=sourceTime;firstTicks=Time::getHighResolutionTicks();}
        const LONGLONG time=jmax<LONGLONG>(lastVideoTime+1,sourceTime-sourceStart);
        sample->SetSampleTime(time);auto frameNumber=videoFrames.load();sample->SetSampleDuration((frameNumber+1)*second*den/fps-frameNumber*second*den/fps);
        require(writer->WriteSample(stream,sample),"カメラ映像の保存");lastVideoTime=time;++videoFrames;thumbnail(sample);
    }
};
class Callback final : public IMFSourceReaderCallback {
    std::atomic<ULONG> refs{1};std::shared_ptr<Capture> capture;
public:
    explicit Callback(std::shared_ptr<Capture> c):capture(std::move(c)){}
    STDMETHODIMP QueryInterface(REFIID id,void** object) override {if(!object)return E_POINTER;*object=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IMFSourceReaderCallback)){*object=static_cast<IMFSourceReaderCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHODIMP_(ULONG) AddRef() override{return ++refs;}
    STDMETHODIMP_(ULONG) Release() override{auto n=--refs;if(!n)delete this;return n;}
    STDMETHODIMP OnReadSample(HRESULT hr,DWORD,DWORD flags,LONGLONG time,IMFSample* sample) override {
        auto c=capture;if(c->stopped())return S_OK;
        try{
            require(hr,"カメラからの映像取得");
            if(flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))throw Failure{E_FAIL,String::fromUTF8("カメラの接続または形式の維持")};
            if(sample)c->frame(sample,time);
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM){if(c->fileInput)c->stop->value=true;else c->fail(String::fromUTF8("カメラからの映像が終了しました。音声WAVを保持します。"));return S_OK;}
            ComPtr<IMFSourceReader> next;{std::lock_guard lock(c->gate);if(!c->stopped())next=c->reader;}
            if(next&&!c->stopped())require(next->ReadSample(firstVideoStream,0,nullptr,nullptr,nullptr,nullptr),"次のカメラ映像の取得");
        }catch(const Failure& f){if(!c->stopped())c->fail(failureText(f));}catch(...){if(!c->stopped())c->fail(String::fromUTF8("カメラ映像の処理に失敗しました。"));}return S_OK;
    }
    STDMETHODIMP OnEvent(DWORD,IMFMediaEvent* event) override{HRESULT hr=S_OK;if(!capture->stopped()&&event&&SUCCEEDED(event->GetStatus(&hr))&&FAILED(hr))capture->fail(String::fromUTF8("カメラからエラーが届きました。音声はWAVに残します。"));return S_OK;}
    STDMETHODIMP OnFlush(DWORD) override{return S_OK;}
};
void openCamera(std::shared_ptr<Capture> c,const VideoRecorder::Config& config){
    auto a=attributes();a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    a->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,config.cameraId.toWideCharPointer());
    require(MFCreateDeviceSource(a.Get(),&c->source),"カメラを開く操作");
    ComPtr<IMFSourceReaderCallback> callback;callback.Attach(new Callback(c));
    a=attributes();a->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,callback.Get());a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
    require(MFCreateSourceReaderFromMediaSource(c->source.Get(),a.Get(),&c->reader),"カメラの読み取り準備");
    c->reader->SetStreamSelection(allVideoStreams,FALSE);require(c->reader->SetStreamSelection(firstVideoStream,TRUE),"映像入力の選択");
    ComPtr<IMFMediaType> best;double bestScore=1e30;
    auto consider=[&](IMFMediaType* type,UINT32 w,UINT32 h,UINT32 fps,UINT32 den){
        double hz=(double)fps/den;if(!VideoRecorder::supportsFormat(config,(int)w,(int)h,hz))return;
        double score=std::abs((double)w-config.preferredWidth)+std::abs((double)h-config.preferredHeight)+std::abs(hz-config.preferredFps)*30;
        if(score<bestScore){best=type;bestScore=score;}
    };
    for(DWORD n=0;n<2048;++n){ComPtr<IMFMediaType> t;if(FAILED(c->reader->GetNativeMediaType(firstVideoStream,n,&t)))break;
        UINT32 w=0,h=0,fps=0,den=1;GUID subtype{};
        if(FAILED(MFGetAttributeSize(t.Get(),MF_MT_FRAME_SIZE,&w,&h))||FAILED(t->GetGUID(MF_MT_SUBTYPE,&subtype)))continue;
        if(subtype!=MFVideoFormat_NV12&&subtype!=MFVideoFormat_YUY2&&subtype!=MFVideoFormat_MJPG&&subtype!=MFVideoFormat_RGB32&&subtype!=MFVideoFormat_I420)continue;
        if(SUCCEEDED(MFGetAttributeRatio(t.Get(),MF_MT_FRAME_RATE,&fps,&den))&&fps&&den)consider(t.Get(),w,h,fps,den);
        UINT32 minFps=0,minDen=0,maxFps=0,maxDen=0;
        if(SUCCEEDED(MFGetAttributeRatio(t.Get(),MF_MT_FRAME_RATE_RANGE_MIN,&minFps,&minDen))&&SUCCEEDED(MFGetAttributeRatio(t.Get(),MF_MT_FRAME_RATE_RANGE_MAX,&maxFps,&maxDen))&&minDen&&maxDen&&config.preferredFps>=(double)minFps/minDen&&config.preferredFps<=(double)maxFps/maxDen){
            auto candidate=media();require(t->CopyAllItems(candidate.Get()),"カメラの可変fps形式の取得");rateRatio(config.preferredFps,fps,den);MFSetAttributeRatio(candidate.Get(),MF_MT_FRAME_RATE,fps,den);consider(candidate.Get(),w,h,fps,den);
        }
    }
    if(!best)throw Failure{MF_E_INVALIDMEDIATYPE,String::fromUTF8("指定したカメラ形式 ")+String(config.preferredWidth)+" x "+String(config.preferredHeight)+" / "+String(config.preferredFps,3)+String::fromUTF8(" fps の選択")};
    require(c->reader->SetCurrentMediaType(firstVideoStream,nullptr,best.Get()),"カメラ形式の設定");
    auto rgb=media();rgb->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);rgb->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
    require(c->reader->SetCurrentMediaType(firstVideoStream,nullptr,rgb.Get()),"カメラ映像の色変換");
    require(c->reader->GetCurrentMediaType(firstVideoStream,&rgb),"カメラ形式の確認");
    UINT32 w=0,h=0;require(MFGetAttributeSize(rgb.Get(),MF_MT_FRAME_SIZE,&w,&h),"カメラの解像度取得");c->width=(int)w;c->height=(int)h;
    require(MFGetAttributeRatio(rgb.Get(),MF_MT_FRAME_RATE,&c->fps,&c->den),"カメラのfps確認");if(!c->fps||!c->den||!VideoRecorder::supportsFormat(config,(int)w,(int)h,(double)c->fps/c->den))throw Failure{MF_E_INVALIDMEDIATYPE,String::fromUTF8("カメラの実際の形式が指定値と一致するかの確認")};
    {auto divisor=std::gcd(c->fps,c->den);c->fps/=divisor;c->den/=divisor;}
    UINT32 pitch=0;if(SUCCEEDED(rgb->GetUINT32(MF_MT_DEFAULT_STRIDE,&pitch)))c->stride=(LONG)pitch;else c->stride=(LONG)w*4;
    c->cameraInput=rgb;
}
void closeCamera(std::shared_ptr<Capture> c){
    c->localStop=true;
    if(c->reader)c->reader->Flush(firstVideoStream);
    if(c->source)c->source->Shutdown();
    std::lock_guard lock(c->gate);c->reader.Reset();c->source.Reset();
    if(c->writer){auto hr=c->writer->Finalize();c->writer.Reset();if(FAILED(hr))c->fail(String::fromUTF8("カメラ映像の保存を完了できませんでした。音声WAVと映像素材を残します。"));}
}
// Test fixture only: exercise the same asynchronous RGB capture callback using
// a generated movie, without opening any physical camera or microphone.
void openTestVideo(std::shared_ptr<Capture> c,const File& file){
    c->fileInput=true;ComPtr<IMFSourceReaderCallback> callback;callback.Attach(new Callback(c));
    auto a=attributes();a->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,callback.Get());a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
    require(MFCreateSourceReaderFromURL(file.getFullPathName().toWideCharPointer(),a.Get(),&c->reader),"テスト映像入力の準備");
    c->reader->SetStreamSelection(allVideoStreams,FALSE);c->reader->SetStreamSelection(firstVideoStream,TRUE);
    auto rgb=media();rgb->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);rgb->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
    require(c->reader->SetCurrentMediaType(firstVideoStream,nullptr,rgb.Get()),"テスト映像のRGB変換");require(c->reader->GetCurrentMediaType(firstVideoStream,&rgb),"テスト映像形式の確認");
    UINT32 w=0,h=0;require(MFGetAttributeSize(rgb.Get(),MF_MT_FRAME_SIZE,&w,&h),"テスト解像度");c->width=(int)w;c->height=(int)h;require(MFGetAttributeRatio(rgb.Get(),MF_MT_FRAME_RATE,&c->fps,&c->den),"テスト映像のfps");if(!c->fps||!c->den)throw Failure{MF_E_INVALIDMEDIATYPE,String::fromUTF8("テスト映像のfps確認")};auto divisor=std::gcd(c->fps,c->den);c->fps/=divisor;c->den/=divisor;
    c->cameraInput=rgb;UINT32 pitch=0;c->stride=SUCCEEDED(rgb->GetUINT32(MF_MT_DEFAULT_STRIDE,&pitch))?(LONG)pitch:(LONG)w*4;
}

// The video is copied as H.264 packets; only AAC audio encoding and, when needed,
// resampling are performed here, after the live performance has stopped recording.
void combine(const File& video,const File& wav,const File& target,LONGLONG videoOffset,const VideoRecorder::Config& config){
    ComPtr<IMFSourceReader> reader;require(MFCreateSourceReaderFromURL(video.getFullPathName().toWideCharPointer(),nullptr,&reader),"録画素材の読み込み");
    reader->SetStreamSelection(allVideoStreams,FALSE);reader->SetStreamSelection(firstVideoStream,TRUE);
    ComPtr<IMFMediaType> h264;require(reader->GetNativeMediaType(firstVideoStream,0,&h264),"録画素材の形式取得");
    GUID subtype{};h264->GetGUID(MF_MT_SUBTYPE,&subtype);if(subtype!=MFVideoFormat_H264)throw Failure{MF_E_INVALIDMEDIATYPE,String::fromUTF8("H.264素材の取得")};
    require(reader->SetCurrentMediaType(firstVideoStream,nullptr,h264.Get()),"映像の再圧縮を省く設定");
    AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<AudioFormatReader> audio(formats.createReaderFor(wav));
    if(!audio||audio->lengthInSamples<=0)throw Failure{E_FAIL,String::fromUTF8("演奏音声の読み込み")};
    const int outRate=audio->sampleRate==44100?44100:48000;
    const int64_t audioFrames=(int64_t)std::llround(audio->lengthInSamples*outRate/audio->sampleRate);
    AudioFormatReaderSource audioSource(audio.get(),false);ResamplingAudioSource resampler(&audioSource,false,2);
    resampler.setResamplingRatio(audio->sampleRate/outRate);resampler.prepareToPlay(1024,outRate);
    auto writer=sink(target,VideoRecorder::Encoder::Software);DWORD videoStream=0,audioStream=0;
    require(writer->AddStream(h264.Get(),&videoStream),"動画の映像形式設定");require(writer->SetInputMediaType(videoStream,h264.Get(),nullptr),"映像のコピー設定");
    auto output=media();output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);output->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_AAC);output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,outRate);output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,(UINT32)config.audioBitrateKbps*1000u/8u);
    require(writer->AddStream(output.Get(),&audioStream),"AAC音声の設定");
    auto input=media();input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);input->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,outRate);input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4);input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,outRate*4);
    require(writer->SetInputMediaType(audioStream,input.Get(),nullptr),"演奏音声の入力設定");require(writer->BeginWriting(),"演奏動画の保存開始");
    LONGLONG videoDelay=jmax<LONGLONG>(0,videoOffset),audioDelay=jmax<LONGLONG>(0,-videoOffset),firstVideoTime=0,commonLead=0;
    bool first=true,eof=false;ComPtr<IMFSample> next;LONGLONG nextTime=0;
    auto nextVideo=[&]{
        next.Reset();while(!eof&&!next){
            DWORD flags=0;LONGLONG time=0;require(reader->ReadSample(firstVideoStream,0,nullptr,&flags,&time,&next),"映像素材の取得");
            if(flags&MF_SOURCE_READERF_ERROR)throw Failure{E_FAIL,String::fromUTF8("映像素材の読み取り")};if(flags&MF_SOURCE_READERF_ENDOFSTREAM)eof=true;
            if(next){
                UINT64 storedDecodeTime=0;LONGLONG decodeTime=SUCCEEDED(next->GetUINT64(MFSampleExtension_DecodeTimestamp,&storedDecodeTime))?(LONGLONG)storedDecodeTime:time;
                if(first){
                    first=false;firstVideoTime=time;
                    // B-frame streams may begin decoding before their first presentation.
                    // Keep every timestamp nonnegative by moving BOTH tracks together;
                    // their measured performance/camera offset remains unchanged.
                    commonLead=jmax<LONGLONG>(0,firstVideoTime-decodeTime-videoDelay);audioDelay+=commonLead;
                }
                const LONGLONG shift=videoDelay+commonLead-firstVideoTime;
                const LONGLONG presentation=time+shift,decoding=decodeTime+shift;
                if(presentation<0||decoding<0)throw Failure{MF_E_INVALID_TIMESTAMP,String::fromUTF8("映像素材の表示時刻とデコード時刻の確認")};
                require(next->SetSampleTime(presentation),"映像の表示時刻の設定");
                // The MP4 source can omit DTS when it equals PTS. Set it explicitly
                // for every sample after shifting, so the mux never mixes old DTS
                // attributes with newly shifted PTS defaults.
                require(next->SetUINT64(MFSampleExtension_DecodeTimestamp,(UINT64)decoding),"映像のデコード時刻の設定");
                nextTime=decoding;
            }
        }
    };
    AudioBuffer<float> block(2,1024);std::array<int16_t,2048> pcm{};int64_t audioPosition=0;nextVideo();
    while(next||audioPosition<audioFrames){
        LONGLONG audioTime=audioDelay+audioPosition*second/outRate;
        if(next&&(audioPosition>=audioFrames||nextTime<=audioTime)){require(writer->WriteSample(videoStream,next.Get()),"映像の結合");nextVideo();}
        else {int n=(int)jmin<int64_t>(1024,audioFrames-audioPosition);block.clear();AudioSourceChannelInfo info(&block,0,n);resampler.getNextAudioBlock(info);
            for(int i=0;i<n;++i)for(int ch=0;ch<2;++ch)pcm[(size_t)i*2+ch]=(int16_t)jlimit(-32768,32767,(int)std::lround(block.getSample(ch,i)*32767));
            auto sample=bytesSample(pcm.data(),(DWORD)(n*4),audioTime,n*second/outRate);require(writer->WriteSample(audioStream,sample.Get()),"演奏音声の結合");audioPosition+=n;}
    }
    require(writer->Finalize(),"演奏動画の保存完了");resampler.releaseResources();
}
}

juce::String VideoRecorder::validateConfig(const Config& c){
    if(c.preferredWidth<160||c.preferredWidth>3840||c.preferredHeight<120||c.preferredHeight>2160||(c.preferredWidth&1)||(c.preferredHeight&1))return juce::String::fromUTF8("解像度は横160〜3840、縦120〜2160の偶数で指定してください。");
    if(!std::isfinite(c.preferredFps)||c.preferredFps<10||c.preferredFps>60)return juce::String::fromUTF8("フレームレートは10〜60fpsで指定してください。");
    if(c.bitrateMbps<1||c.bitrateMbps>100||c.quality<1||c.quality>100)return juce::String::fromUTF8("映像は1〜100Mbps、品質は1〜100で指定してください。");
    if(c.rateControl!=RateControl::CBR&&c.rateControl!=RateControl::QualityVbr)return juce::String::fromUTF8("レート制御はCBRまたは品質指定VBRを選んでください。");
    if(c.profile!=Profile::Auto&&c.profile!=Profile::Baseline&&c.profile!=Profile::Main&&c.profile!=Profile::High)return juce::String::fromUTF8("H.264プロファイルを選び直してください。");
    if(c.encoder!=Encoder::Auto&&c.encoder!=Encoder::Software)return juce::String::fromUTF8("エンコード処理方式を選び直してください。");
    if(c.keyframeSeconds<0||c.keyframeSeconds>10)return juce::String::fromUTF8("キーフレーム間隔は自動、または1〜10秒で指定してください。");
    if(c.audioBitrateKbps!=96&&c.audioBitrateKbps!=128&&c.audioBitrateKbps!=160&&c.audioBitrateKbps!=192)return juce::String::fromUTF8("AAC音声は96・128・160・192kbpsから選んでください。");
    if(!std::isfinite(c.sampleRate)||c.sampleRate<8000||c.sampleRate>384000||c.videoDelayMs< -1000||c.videoDelayMs>1000||c.cameraId.length()>4096)return juce::String::fromUTF8("音声のサンプルレート、映像の同期補正、カメラの設定値を確認してください。");
    return {};
}
bool VideoRecorder::supportsFormat(const Config& c,int width,int height,double fps){
    if(width<160||width>3840||height<120||height>2160||(width&1)||(height&1)||!std::isfinite(fps)||fps<10||fps>60.001)return false;
    bool matchingRate=std::abs(fps-c.preferredFps)<=.01;
    // Camera labels commonly call 30000/1001 and 60000/1001 "30" and "60".
    // Permit only these standard nominal pairs; never silently halve the frame rate.
    for(double nominal:{24.0,30.0,60.0})if(std::abs(c.preferredFps-nominal)<.001&&std::abs(fps-nominal*1000/1001)<.001)matchingRate=true;
    return !c.strictFormat||(width==c.preferredWidth&&height==c.preferredHeight&&matchingRate);
}
juce::var VideoRecorder::configToJson(const Config& c){
    auto* o=new juce::DynamicObject();o->setProperty("version",1);o->setProperty("cameraId",c.cameraId);o->setProperty("preferredWidth",c.preferredWidth);o->setProperty("preferredHeight",c.preferredHeight);o->setProperty("preferredFps",c.preferredFps);o->setProperty("strictFormat",c.strictFormat);o->setProperty("bitrateMbps",c.bitrateMbps);o->setProperty("rateControl",(int)c.rateControl);o->setProperty("quality",c.quality);o->setProperty("profile",(int)c.profile);o->setProperty("encoder",(int)c.encoder);o->setProperty("keyframeSeconds",c.keyframeSeconds);o->setProperty("audioBitrateKbps",c.audioBitrateKbps);o->setProperty("videoDelayMs",c.videoDelayMs);return juce::var(o);
}
bool VideoRecorder::configFromJson(const juce::var& value,Config& config,juce::String& error){
    error.clear();if(!value.isObject()){error=juce::String::fromUTF8("録画設定の形式が正しくありません。");return false;}Config candidate=config;
    auto integer=[&](const char* key,int& result){if(!value.hasProperty(key))return true;auto field=value[key];if(!field.isInt()&&!field.isInt64()&&!field.isDouble())return false;double number=(double)field;if(!std::isfinite(number)||number<(std::numeric_limits<int>::min)()||number>(std::numeric_limits<int>::max)()||std::floor(number)!=number)return false;result=(int)number;return true;};
    int version=1,rate=(int)candidate.rateControl,profile=(int)candidate.profile,encoder=(int)candidate.encoder;
    if(!integer("version",version)||version!=1||!integer("preferredWidth",candidate.preferredWidth)||!integer("preferredHeight",candidate.preferredHeight)||!integer("bitrateMbps",candidate.bitrateMbps)||!integer("rateControl",rate)||!integer("quality",candidate.quality)||!integer("profile",profile)||!integer("encoder",encoder)||!integer("keyframeSeconds",candidate.keyframeSeconds)||!integer("audioBitrateKbps",candidate.audioBitrateKbps)||!integer("videoDelayMs",candidate.videoDelayMs)){error=juce::String::fromUTF8("録画設定に読み込めない数値があります。");return false;}
    if(value.hasProperty("preferredFps")){auto fps=value["preferredFps"];if(!fps.isDouble()&&!fps.isInt()&&!fps.isInt64()){error=juce::String::fromUTF8("録画設定のfpsが数値ではありません。");return false;}candidate.preferredFps=(double)fps;}
    if(value.hasProperty("strictFormat")){if(!value["strictFormat"].isBool()){error=juce::String::fromUTF8("録画設定の形式一致条件が正しくありません。");return false;}candidate.strictFormat=(bool)value["strictFormat"];}
    if(value.hasProperty("cameraId")){if(!value["cameraId"].isString()){error=juce::String::fromUTF8("カメラの設定形式が正しくありません。");return false;}candidate.cameraId=value["cameraId"].toString();}
    candidate.rateControl=(RateControl)rate;candidate.profile=(Profile)profile;candidate.encoder=(Encoder)encoder;error=validateConfig(candidate);if(error.isNotEmpty())return false;config=candidate;return true;
}

struct VideoRecorder::Impl {
    Recorder audio;
    std::shared_ptr<StopFlag> stop=std::make_shared<StopFlag>();
    std::shared_ptr<Capture> capture;
    std::thread worker;
    Config config;
    bool previewOnly=false;
    void run(bool synthetic,const File& testVideo,bool detailedPattern){
        auto c=capture;File part=previewOnly?File():config.target.withFileExtension("camera-part.mp4");
        try{
            Platform platform;
            try{
                if(synthetic){c->width=config.preferredWidth;c->height=config.preferredHeight;rateRatio(config.preferredFps,c->fps,c->den);c->stride=c->width.load()*4;}
                else if(testVideo.existsAsFile())openTestVideo(c,testVideo);
                else openCamera(c,config);
                if(!previewOnly){c->writer=sink(part,config.encoder);String description;c->stream=addVideo(c->writer.Get(),c->width,c->height,c->fps,c->den,config,description,c->cameraInput.Get());{std::lock_guard lock(c->infoGate);c->encodingDescription=description;}require(c->writer->BeginWriting(),"H.264の保存開始");String err;
                    if(!audio.start(config.target.withFileExtension("wav"),config.sampleRate,err))throw Failure{E_FAIL,err};}
                c->phase=previewOnly?Phase::Previewing:Phase::Recording;
                if(!synthetic&&!c->stopped())require(c->reader->ReadSample(firstVideoStream,0,nullptr,nullptr,nullptr,nullptr),"カメラ映像の取得開始");
                auto began=Time::getMillisecondCounter();auto syntheticStart=Time::getHighResolutionTicks();int64_t syntheticFrame=0;std::vector<uint32_t> pixels;if(synthetic)pixels.resize((size_t)c->width*c->height);
                while(!c->stopped()){
                    if(!previewOnly&&!audio.isCapturing()){c->fail(String::fromUTF8("音声録音が停止しました。保存先・空き容量・出力機器を確認してください。"));break;}
                    if(synthetic){
                        int width=c->width.load(),height=c->height.load();
                        if(detailedPattern){for(int y=0;y<height;++y)for(int x=0;x<width;++x){int grid=((x/12)^(y/12))&1;int moving=(int)((x+syntheticFrame*9)%width);int r=grid?220:35,g=((y+syntheticFrame*5)%height)*220/height+15,b=moving<width/5?235:45;pixels[(size_t)y*width+x]=(uint32_t)(r<<16|g<<8|b);}}
                        else std::fill(pixels.begin(),pixels.end(),syntheticFrame%30<15?0x00e02020:0x0020d040);
                        auto sourceTime=syntheticFrame*second*c->den/c->fps,following=(syntheticFrame+1)*second*c->den/c->fps;
                        auto sample=bytesSample(pixels.data(),(DWORD)(pixels.size()*4),sourceTime,following-sourceTime);if(previewOnly){c->thumbnail(sample.Get());++c->videoFrames;}else c->frame(sample.Get(),sourceTime);++syntheticFrame;
                        auto deadline=syntheticFrame*second*c->den/c->fps;auto elapsed=ticksToTime(Time::getHighResolutionTicks()-syntheticStart);if(deadline>elapsed)Thread::sleep((int)jmax<LONGLONG>(1,(deadline-elapsed)/10000));
                    }
                    else {Thread::sleep(20);if(c->videoFrames==0&&Time::getMillisecondCounter()-began>8000)c->fail(String::fromUTF8("カメラから映像が届きません。接続と他アプリでの使用を確認してください。"));}
                }
                c->phase=Phase::Saving;audio.requestStop();closeCamera(c);audio.finish();
                String problem;{std::lock_guard lock(c->infoGate);problem=c->error;}
                if(!previewOnly){
                    if(c->videoFrames==0)throw Failure{E_FAIL,String::fromUTF8("録画する映像が届く前に停止したため、音声のみの保存")};
                    const auto videoOffset=ticksToTime(c->firstTicks.load()-audio.firstInputTicks())+(LONGLONG)config.videoDelayMs*10000;
                    combine(part,config.target.withFileExtension("wav"),config.target,videoOffset,config);
                    if(problem.isEmpty())part.deleteFile();
                }
                c->phase=problem.isEmpty()?Phase::Complete:Phase::Failed;
            }catch(const Failure& failure){c->fail(failureText(failure));c->phase=Phase::Saving;audio.requestStop();closeCamera(c);audio.finish();throw;}catch(...){c->phase=Phase::Saving;audio.requestStop();closeCamera(c);audio.finish();throw;}
        }catch(const Failure& f){c->fail(failureText(f));c->phase=Phase::Failed;}catch(...){c->fail(String::fromUTF8("録画を保存できませんでした。残っているWAVと映像素材を確認してください。"));c->phase=Phase::Failed;}
        c->localStop=true;c->done=true;
    }
};
VideoRecorder::VideoRecorder():impl(std::make_unique<Impl>()){}
VideoRecorder::~VideoRecorder(){requestStop();finish();}
juce::Array<VideoRecorder::Camera> VideoRecorder::cameras(juce::String& error){
    juce::Array<Camera> list;std::thread thread([&]{try{Platform platform;auto a=attributes();a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);IMFActivate** devices=nullptr;UINT32 count=0;
        require(MFEnumDeviceSources(a.Get(),&devices,&count),"カメラ一覧の取得");for(UINT32 n=0;n<count;++n){WCHAR* name=nullptr;WCHAR* id=nullptr;UINT32 length=0;
            devices[n]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&length);devices[n]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,&id,&length);if(id)list.add({name?juce::String(name):juce::String::fromUTF8("カメラ"),juce::String(id)});CoTaskMemFree(name);CoTaskMemFree(id);devices[n]->Release();}CoTaskMemFree(devices);
        }catch(const Failure& f){error=failureText(f);}catch(...){error=juce::String::fromUTF8("カメラ一覧を取得できません。Windowsのカメラ機能を確認してください。");}});thread.join();return list;
}
bool VideoRecorder::begin(const Config& c,bool previewOnly,bool synthetic,juce::String& error,const juce::File& testVideo,bool detailedPattern){
    if(isBusy()){error=juce::String::fromUTF8("前の録画処理が完了するまでお待ちください。");return false;}
    error=validateConfig(c);if(error.isNotEmpty())return false;
    if(c.cameraId.isEmpty()&&!synthetic&&!testVideo.existsAsFile()){error=juce::String::fromUTF8("カメラを選択してください。");return false;}
    if(!previewOnly&&!c.target.hasFileExtension("mp4")){error=juce::String::fromUTF8("録画の保存先はMP4ファイルにしてください。");return false;}
    if(!previewOnly&&(c.target.exists()||c.target.withFileExtension("wav").exists()||c.target.withFileExtension("camera-part.mp4").exists()||!c.target.getParentDirectory().createDirectory().wasOk())){error=juce::String::fromUTF8("録画先に書き込めません。同名のファイルと空き容量を確認してください。");return false;}
    impl->config=c;impl->previewOnly=previewOnly;impl->stop->value=false;impl->capture=std::make_shared<Capture>();impl->capture->stop=impl->stop;impl->capture->previewOnly=previewOnly;
    try{impl->worker=std::thread([this,synthetic,testVideo,detailedPattern]{impl->run(synthetic,testVideo,detailedPattern);});}catch(...){error=juce::String::fromUTF8("録画用スレッドを開始できません。");impl->capture->fail(error);impl->capture->phase=Phase::Failed;impl->capture->done=true;return false;}return true;
}
bool VideoRecorder::start(const Config& c,juce::String& e){return begin(c,false,false,e);}
bool VideoRecorder::startPreview(const Config& c,juce::String& e){return begin(c,true,false,e);}
void VideoRecorder::push(float* const* data,int channels,int samples) noexcept{impl->audio.push(data,channels,samples);}
void VideoRecorder::requestStop(Recorder::Reason why) noexcept{impl->stop->value=true;impl->audio.requestStop(why);}
bool VideoRecorder::pollFinished(){if(!isBusy()||!impl->capture->done.load())return false;impl->worker.join();return true;}
void VideoRecorder::finish(){requestStop();if(impl->worker.joinable())impl->worker.join();}
bool VideoRecorder::isBusy() const noexcept{return impl->worker.joinable();}
bool VideoRecorder::isCapturing() const noexcept{return phase()==Phase::Recording&&!impl->stop->value.load();}
VideoRecorder::Phase VideoRecorder::phase() const noexcept{return impl->capture?impl->capture->phase.load():Phase::Idle;}
double VideoRecorder::seconds() const noexcept{return impl->audio.frameCount()/impl->config.sampleRate;}
juce::String VideoRecorder::error() const{if(!impl->capture)return {};std::lock_guard lock(impl->capture->infoGate);return impl->capture->error;}
juce::Image VideoRecorder::preview() const{if(!impl->capture)return {};std::lock_guard lock(impl->capture->infoGate);return impl->capture->image;}
juce::File VideoRecorder::movieFile() const{return impl->config.target;}
juce::File VideoRecorder::audioFile() const{return impl->config.target==juce::File()?juce::File():impl->config.target.withFileExtension("wav");}
juce::String VideoRecorder::formatDescription() const{if(!impl->capture||phase()==Phase::Preparing)return {};auto c=impl->capture;if(c->width<=0||c->height<=0)return {};String description=String(c->width.load())+" × "+String(c->height.load())+" / "+String((double)c->fps/c->den,3)+" fps";std::lock_guard lock(c->infoGate);if(c->encodingDescription.isNotEmpty())description+=" / "+c->encodingDescription;return description;}
void VideoRecorder::failForTest(){if(impl->capture)impl->capture->fail(juce::String::fromUTF8("テスト用のカメラ切断。音声WAVは保持します。"));}
}

