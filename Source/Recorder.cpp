#include "Recorder.h"
#include <cmath>

namespace stage {
Recorder::~Recorder() { requestStop(Reason::Shutdown); finish(); }

bool Recorder::start(const juce::File& target, double rate, juce::String& error) {
    if (isBusy()) { error = juce::String::fromUTF8("前の録音を保存中です。"); return false; }
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000) {
        error = juce::String::fromUTF8("出力機器のサンプリング周波数を確認してください。"); return false;
    }
    if (target.exists() || !target.getParentDirectory().createDirectory().wasOk()) {
        error = juce::String::fromUTF8("保存先に書き込めません。空き容量と保存先を確認してください。"); return false;
    }
    auto stream = target.createOutputStream();
    if (!stream || !stream->openedOk()) { error = juce::String::fromUTF8("録音ファイルを作成できません。"); return false; }
    auto* streamStatus = stream.get();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> output = std::move(stream);
    auto sink = wav.createWriterFor(output, juce::AudioFormatWriterOptions{}
        .withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
    if (!sink || streamStatus->getStatus().failed()) { error = juce::String::fromUTF8("WAVファイルを作成できません。"); return false; }
    file = target;
    diskStream = streamStatus;
    return begin(std::move(sink), rate, (int)std::ceil(rate * 2) + 1, error);
}

bool Recorder::begin(std::unique_ptr<juce::AudioFormatWriter> sink, double rate,
                     int capacity, juce::String& error) {
    if (isBusy() || !sink || capacity < 2) return false;
    sampleRate = rate;
    fifo = std::make_unique<juce::AbstractFifo>(capacity);
    buffer.setSize(2, capacity);
    writer = std::move(sink);
    frames = 0; firstTicks=0; reason = Reason::None; done = false;
    generation.fetch_add(1);
    accepting = true;
    try { worker = std::thread([this] { run(); }); }
    catch (...) {
        requestStop(Reason::WriteError);
        while (readers.load() != 0) std::this_thread::yield();
        writer.reset(); diskStream=nullptr; fifo.reset(); buffer.setSize(0, 0);
        error = juce::String::fromUTF8("録音用のスレッドを開始できません。"); return false;
    }
    return true;
}

void Recorder::requestStop(Reason why) noexcept {
    if (!accepting.load() && why != Reason::WriteError) return;
    if (why == Reason::WriteError) reason = why;
    else { auto empty = Reason::None; reason.compare_exchange_strong(empty, why); }
    accepting = false;
}

void Recorder::push(float* const* output, int channels, int samples) noexcept {
    if (!accepting.load() || samples <= 0) return;
    const auto serial = generation.load();
    // Seq-cst reader registration pairs with stop/drain. A callback that arrives
    // after stop may register late but cannot dereference retired buffers.
    readers.fetch_add(1);
    if (accepting.load() && serial == generation.load()) {
        if (channels <= 0 || output == nullptr) requestStop(Reason::DeviceChanged);
        else if (frames.load() + samples > frameLimit) requestStop(Reason::FileLimit);
        else {
            int a, n, b, m;
            fifo->prepareToWrite(samples, a, n, b, m);
            if (n + m != samples) requestStop(Reason::Overflow);
            else {
                for (int ch = 0; ch < 2; ++ch) {
                    auto* source = output[channels == 1 ? 0 : ch];
                    if (source) {
                        buffer.copyFrom(ch, a, source, n);
                        if (m != 0) buffer.copyFrom(ch, b, source + n, m);
                    } else {
                        buffer.clear(ch, a, n);
                        if (m != 0) buffer.clear(ch, b, m);
                    }
                }
                if(firstTicks.load()==0)firstTicks=juce::Time::getHighResolutionTicks();
                frames.fetch_add(samples);
                fifo->finishedWrite(samples);
            }
        }
    }
    readers.fetch_sub(1);
}

void Recorder::run() {
    int64_t sinceFlush = 0;
    bool failed = false;
    auto flush = [this] {
        bool ok=writer->flush();
        if(diskStream){diskStream->flush();ok=ok&&diskStream->getStatus().wasOk();}
        return ok;
    };
    for (;;) {
        int a, n, b, m;
        fifo->prepareToRead(8192, a, n, b, m);
        if (n + m > 0) {
            if (!failed) {
                bool ok = writer->writeFromAudioSampleBuffer(buffer, a, n);
                if (m != 0 && ok) ok = writer->writeFromAudioSampleBuffer(buffer, b, m);
                sinceFlush += n + m;
                if (ok && sinceFlush >= (int64_t)sampleRate) {
                    ok = flush(); sinceFlush = 0;
                }
                if (!ok) { failed = true; requestStop(Reason::WriteError); }
            }
            fifo->finishedRead(n + m);
        } else if (!accepting.load() && readers.load() == 0) {
            // The last registered callback may have published after prepareToRead.
            if (fifo->getNumReady() == 0) break;
        } else juce::Thread::sleep(5);
    }
    if (!flush()) requestStop(Reason::WriteError);
    writer.reset(); // WAV header is finalised on this disk thread
    diskStream=nullptr;
    done = true;
}

bool Recorder::pollFinished() {
    if (!isBusy() || !done.load()) return false;
    worker.join(); fifo.reset(); buffer.setSize(0, 0); return true;
}

void Recorder::finish() {
    requestStop();
    if (worker.joinable()) worker.join();
    fifo.reset(); buffer.setSize(0, 0);
}
}
