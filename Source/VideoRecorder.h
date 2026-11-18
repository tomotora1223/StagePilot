#pragma once
#include <JuceHeader.h>
#include "Recorder.h"

namespace stage {
class VideoRecorder {
public:
    struct Camera { juce::String name, id; };
    enum class RateControl { CBR=0, QualityVbr=1 };
    enum class Profile { Auto=0, Baseline=66, Main=77, High=100 };
    enum class Encoder { Auto=0, Software=1 };
    struct Config {
        juce::String cameraId;
        juce::File target;
        double sampleRate=48000;
        int preferredWidth=1920, preferredHeight=1080, videoDelayMs=0;
        double preferredFps=30;
        bool strictFormat=true;
        int bitrateMbps=12, quality=85, keyframeSeconds=0, audioBitrateKbps=192;
        RateControl rateControl=RateControl::CBR;
        Profile profile=Profile::Auto;
        Encoder encoder=Encoder::Auto;
    };
    enum class Phase { Idle, Preparing, Previewing, Recording, Saving, Complete, Failed };
    VideoRecorder();
    ~VideoRecorder();
    static juce::Array<Camera> cameras(juce::String& error); // enumeration only; never activates a camera
    static juce::String validateConfig(const Config&);
    static bool supportsFormat(const Config&,int width,int height,double fps);
    static juce::var configToJson(const Config&);
    static bool configFromJson(const juce::var&,Config&,juce::String& error);
    bool start(const Config&,juce::String& error);
    bool startPreview(const Config&,juce::String& error);
    void push(float* const*,int channels,int samples) noexcept;
    void requestStop(Recorder::Reason why=Recorder::Reason::User) noexcept;
    bool pollFinished(); // UI; joins only after all saving has completed
    void finish(); // shutdown only
    bool isBusy() const noexcept; // UI only
    bool isCapturing() const noexcept;
    Phase phase() const noexcept;
    double seconds() const noexcept;
    juce::String error() const;
    juce::Image preview() const;
    juce::File movieFile() const;
    juce::File audioFile() const;
    juce::String formatDescription() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    bool begin(const Config&,bool previewOnly,bool synthetic,juce::String&,const juce::File& testVideo={},bool detailedPattern=false);
    void failForTest();
    friend int runVideoTests(const juce::File&);
};
int runVideoTests(const juce::File&);
}
