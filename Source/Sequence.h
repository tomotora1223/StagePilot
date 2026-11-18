#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "AudioMeter.h"

namespace stage {
constexpr int sequenceNoteCount = 128;
struct SequenceNote { int pitch=60, start=0, length=1, velocity=100; bool enabled=false; };
struct Pattern {
    std::array<SequenceNote,sequenceNoteCount> notes{};
    int bpm=120, bars=2;
    bool loop=true;
};
// A separate instrument instance keeps sequence transport independent of live MIDI.
// All non-atomic fields are accessed with the device detached, or on its audio thread.
class Sequencer {
public:
    Pattern pattern;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription description;
    juce::String name;
    std::atomic<float> gain{1};
    AudioMeter meter;
    std::atomic<bool> playing{false}, sounding{false};
    std::atomic<double> position{0};
    std::array<std::atomic<bool>,128> lit{};
    juce::MidiBuffer midi;
    void prepare(double sr,int block);
    void reset();
    void beginBlock() { midi.clear(); }
    void start();
    void stop(bool immediate=false,int offset=0);
    void preview(int pitch,int velocity);
    void edit(int index,SequenceNote note);
    void settings(int bpm,int bars,bool loop);
    void render(float* const* output,int channels,int samples);
    int64_t durationFrames() const;
    static bool valid(const Pattern&);
    static juce::var toJson(const Pattern&);
    static bool fromJson(const juce::var&,Pattern&);
private:
    double sampleRate=48000;
    int maximumBlock=256,previewPitch=-1;
    int64_t frame=0,tail=0,previewLeft=0;
    std::array<int,128> held{};
    juce::AudioBuffer<float> buffer;
    juce::SmoothedValue<float> tailGate;
    juce::SmoothedValue<float> trim;
    void noteOn(int pitch,int velocity,int offset);
    void noteOff(int pitch,int offset);
    int64_t stepFrame(int step) const;
};
}
