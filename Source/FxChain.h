#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

namespace stage {
constexpr int fxCapacity = 4;
struct FxNode {
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription description;
    std::atomic<bool> bypassed{false};
    FxNode()=default;
    FxNode(const FxNode&)=delete;
    FxNode& operator=(const FxNode&)=delete;
    FxNode(FxNode&& other) noexcept { *this=std::move(other); }
    FxNode& operator=(FxNode&& other) noexcept { plugin=std::move(other.plugin);description=std::move(other.description);bypassed=other.bypassed.load();return *this; }
};
// Plugin construction, structural mutation, prepare, and state serialization require
// the device callback to be detached. Only bypass changes are allowed during rendering.
class FxChain {
    juce::MidiBuffer midi;
    double sampleRate=48000;
    int blockSize=256;
public:
    std::array<FxNode,fxCapacity> nodes;
    int count() const;
    int latencySamples() const;
    static juce::String configureStereo(juce::AudioPluginInstance&);
    void prepare(double,int);
    void release();
    void reset();
    void process(juce::AudioBuffer<float>&);
    void clear();
    void remove(int);
    void moveNode(int,int);
    void swapWith(FxChain&);
};
std::unique_ptr<juce::AudioPluginInstance> makeTestFx();
int runFxTests(const juce::File&,const juce::String& pluginPath={});
}
