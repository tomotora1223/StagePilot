#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <thread>

namespace stage {
// Single audio producer, one disk consumer. No disk access, locks, allocation,
// or thread wake-up calls in push(). The disk thread exists only while recording.
class Recorder {
public:
    enum class Reason { None, User, DeviceChanged, Overflow, WriteError, FileLimit, Shutdown };
    Recorder() = default;
    ~Recorder();
    bool start(const juce::File&, double sampleRate, juce::String& error);
    void push(float* const* output, int channels, int samples) noexcept;
    void requestStop(Reason = Reason::User) noexcept;
    // UI thread: joins only after disk work has finished. Never blocks playback.
    bool pollFinished();
    void finish(); // shutdown/tests only; drains and finalises the WAV
    bool isCapturing() const noexcept { return accepting.load(); }
    bool isBusy() const noexcept { return worker.joinable(); } // UI thread only
    double seconds() const noexcept { return frames.load() / sampleRate; }
    double getSampleRate() const noexcept { return sampleRate; }
    int64_t frameCount() const noexcept { return frames.load(); }
    int64_t firstInputTicks() const noexcept { return firstTicks.load(); }
    Reason getReason() const noexcept { return reason.load(); }
    const juce::File& getFile() const noexcept { return file; }
private:
    friend int runRecordingTests(const juce::File&);
    bool begin(std::unique_ptr<juce::AudioFormatWriter>, double, int capacity, juce::String&);
    void run();
    std::unique_ptr<juce::AbstractFifo> fifo;
    juce::AudioBuffer<float> buffer;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::FileOutputStream* diskStream = nullptr; // owned by writer; disk thread only after start
    std::thread worker;
    std::atomic<bool> accepting{false}, done{false};
    std::atomic<unsigned> readers{0}, generation{0};
    std::atomic<int64_t> frames{0};
    std::atomic<int64_t> firstTicks{0};
    std::atomic<Reason> reason{Reason::None};
    double sampleRate = 48000;
    juce::File file;
    // Keep a conventional RIFF WAV comfortably below its 4 GiB limit.
    int64_t frameLimit = (int64_t(3500) * 1024 * 1024) / 6;
};
int runRecordingTests(const juce::File&);
}
