#pragma once
#include <JuceHeader.h>
#include "Typography.h"
#include <array>
#include <atomic>
#include <cstdint>
#include "Sequence.h"
#include "Scenes.h"
#include "ResetSlider.h"
#include "Recorder.h"
#include "VideoRecorder.h"
#include "FxChain.h"

namespace stage {
constexpr int slotCount = 8, mappingCount = 64, clipCount = 8;

// Bounded MPMC queue: MIDI driver threads and UI never wait on the audio thread.
template<typename T, size_t N> class Queue {
    static_assert((N & (N - 1)) == 0);
    struct Cell { std::atomic<size_t> sequence; T data{}; };
    std::array<Cell, N> cells;
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
public:
    Queue() { for (size_t i = 0; i < N; ++i) cells[i].sequence.store(i); }
    bool push(const T& data) noexcept {
        auto p = head.load(std::memory_order_relaxed);
        for (;;) {
            auto& c = cells[p & (N - 1)];
            auto s = c.sequence.load(std::memory_order_acquire);
            auto d = static_cast<intptr_t>(s) - static_cast<intptr_t>(p);
            if (d == 0) {
                if (head.compare_exchange_weak(p, p + 1, std::memory_order_relaxed)) {
                    c.data = data; c.sequence.store(p + 1, std::memory_order_release); return true;
                }
            } else if (d < 0) return false;
            else p = head.load(std::memory_order_relaxed);
        }
    }
    bool pop(T& data) noexcept {
        auto p = tail.load(std::memory_order_relaxed);
        for (;;) {
            auto& c = cells[p & (N - 1)];
            auto s = c.sequence.load(std::memory_order_acquire);
            auto d = static_cast<intptr_t>(s) - static_cast<intptr_t>(p + 1);
            if (d == 0) {
                if (tail.compare_exchange_weak(p, p + 1, std::memory_order_relaxed)) {
                    data = c.data; c.sequence.store(p + N, std::memory_order_release); return true;
                }
            } else if (d < 0) return false;
            else p = tail.load(std::memory_order_relaxed);
        }
    }
};

enum Action { Select, Next, Previous, FadeOut, Restore, Panic, Master, SlotGain, Parameter,
              LayerToggle, ClipPlay, ClipStop, ClipStopAll, SetTranspose, SetLayer, SequencePlay, SequenceStop,
              SelectScene, NextScene, PreviousScene, PerformanceTranspose, SetIgnorePedal, SequenceGain, PerformanceTransposeStep, NextPerformance, PreviousPerformance, MasterStep, RemotePitch };
inline bool sceneTarget(int action){return action==SelectScene;}
inline bool instrumentTarget(int action) {return action==Select||action==SlotGain||action==Parameter||action==LayerToggle;}
inline bool clipTarget(int action) {return action==ClipPlay||action==ClipStop;}
struct ClipData {
    juce::AudioBuffer<float> audio;
    double sampleRate=48000;
    juce::File file;
    juce::String name;
    int builtin=-1;
};
struct Clip {
    ClipData data;
    std::atomic<float> gain{0.8f};
    std::atomic<bool> loop{false}, playing{false};
    struct Voice {double position=0;float velocity=1;bool active=false;juce::SmoothedValue<float> envelope;};
    std::array<Voice,2> voices;
};
bool readClip(const juce::File&,ClipData&,juce::String& error);
ClipData makeDemoClip(int index);
struct Mapping {
    int kind = 0; // MIDI status high nibble: 9 note, 11 CC, 12 program
    int channel = 0; // 0 = all
    int number = 0;
    int action = Select;
    int slot = 0;
    int parameter = 0;
    float minimum = 0.0f, maximum = 1.0f;
};
constexpr int remoteBatchLimit=8;
struct ControlOperation { int action, index; float value; };
struct Event {
    enum Type { Midi, Control, Bind, ClearBindings, SequenceEdit, SequenceSettings, SequencePreview, SceneEdit, PedalSettings, GainSettings, LivePreview, RemoteBatch } type = Midi;
    int a = 0, b = 0, c = 0;
    float value = 0;
    Mapping mapping{};
    union { SequenceNote note{}; ScenePart part; ControlOperation batch[remoteBatchLimit]; };
};
struct Slot {
    Slot() { trim.setCurrentAndTargetValue(1.0f); }
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription description;
    FxChain fx;
    juce::String name;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    juce::SmoothedValue<float> gate;
    juce::SmoothedValue<float> trim;
    std::atomic<float> peak{0};
    AudioMeter meter;
    std::atomic<bool> running{false};
    std::atomic<bool> layer{false};
    std::atomic<int> transpose{0};
    std::atomic<int> keyLow{0},keyHigh{127};
    std::atomic<bool> sceneEnabled{false};
    std::atomic<bool> ignorePedal{false}, pedalDown{false}, tailActive{false};
    std::atomic<float> tailRemaining{0};
    std::atomic<float> gain{1.0f};
    int64_t tailLeft = 0;
    bool retiring = false;
};

class Engine : public juce::AudioIODeviceCallback, public juce::MidiInputCallback {
public:
    Engine();
    std::array<Slot, slotCount> slots;
    std::array<Clip,clipCount> clips;
    Sequencer sequence;
    std::array<Scene,sceneCount> scenes; // parts are audio-thread owned; names are UI owned
    std::array<ScenePart,slotCount> standalone{};
    std::atomic<int> activeScene{-1};
    std::atomic<int> performanceTranspose{0};
    std::atomic<bool> pedalDown{false};
    std::atomic<bool> rangeLearning{false};
    std::atomic<int> rangeLearned{-1};
    std::array<std::atomic<uint16_t>,128> inputKeys{};
    std::atomic<int> previewKey{-1};
    std::atomic<int> selected{0}, mode{0}; // tail, crossfade, cut
    std::atomic<float> fadeSeconds{1.5f}, transitionSeconds{0.08f}, tailSeconds{8.0f};
    std::atomic<float> outputPeak{0}, masterValue{0.7f}, cpuPercent{0};
    AudioMeter outputMeter;
    Recorder recorder;
    VideoRecorder movie;
    FxChain masterFx;
    std::atomic<int> activeVoices{0}, dropped{0}, lastMidi{0}, learned{0};
    std::atomic<bool> learning{false};
    std::atomic<int> blockSize{256};
    std::atomic<double> rate{48000};
    std::array<Mapping, mappingCount> bindings{}; // audio-thread owned during playback
    bool post(const Event& e);
    void control(int action, int slot = 0, float value = 0, int parameter = 0);
    void midi(int status, int data1, int data2 = 0);
    void preview(int note,int velocity=100);
    void prepare(double sampleRate, int samples);
    void render(float* const* output, int channels, int samples);
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                         const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void resetRouting(); // call only with callback detached
    void restoreScene(); // call only with callback detached
private:
    juce::AudioBuffer<float> masterFxBuffer;
    const std::unique_ptr<Queue<Event,4096>> queue=std::make_unique<Queue<Event,4096>>();
    std::atomic<bool> overflowPanic{false};
    juce::SmoothedValue<float> master;
    std::array<std::array<uint8_t, 128>, 16> owners{};
    std::array<std::array<std::array<int16_t,128>,16>,slotCount> noteTargets{};
    std::array<int, 16> sustain{};
    std::array<std::array<int,16>,slotCount> forwardedPedal{};
    std::array<bool, mappingCount> pressed{};
    std::array<std::array<bool,128>,16> consumed{};
    struct PreviewVoice {int target=-1,channel=-1;};
    std::array<PreviewVoice,slotCount> previewVoices{};
    int64_t previewLeft=0;
    void startPreview(int note,int velocity);
    int remotePitch=8192;
    juce::uint32 remotePitchUntil=0;
    std::array<int,slotCount> remotePitchSent{};
    void setRemotePitch(int value);
    void syncRemotePitch(int slot);
    void stopPreview();
    void releasePreview(int slot);
    void execute(int action, int slot, float value, int parameter);
    void switchTo(int index);
    void refreshActive();
    bool wanted(int index) const {return slots[(size_t)index].plugin&&(activeScene>=0?slots[(size_t)index].sceneEnabled.load():(selected.load()==index||slots[(size_t)index].layer.load()));}
    void selectScene(int index);
    void applyScenePart(int index,ScenePart part,bool preserveTail=false);
    void releaseNote(int channel,int note);
    void playClip(int index,float velocity);
    void stopClip(int index,bool immediate=false);
    void renderClips(float* const*,int,int);
    void route(const Event&);
    void silence(int index, bool immediate);
    void dispatch(int index, const juce::MidiMessage&);
    void sendPedal(int index,int channel,int value);
    void syncPedal(int index);
    void releaseForSwitch(int index);
    void releaseSharedForSwitch(const std::array<bool,slotCount>& previous);
};
std::unique_ptr<juce::AudioPluginInstance> makeTestSynth();
int runTests(const juce::File& report, const juce::String& pluginPath = {});
}
