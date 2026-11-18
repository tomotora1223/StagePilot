#pragma once
#include "Engine.h"
#include <thread>
#include <mutex>
#include <deque>

namespace stage {
// Networking never runs on the audio callback. Only bounded Engine events cross that boundary.
class RemoteService {
    Engine& engine;
    std::unique_ptr<juce::StreamingSocket> httpSocket,tcpSocket;
    std::unique_ptr<juce::DatagramSocket> oscSocket;
    std::thread httpThread,tcpThread,oscThread;
    std::atomic<bool> running{false},accepting{true};
    std::mutex mutex;
    juce::var catalog;
    juce::String token;
    std::deque<std::pair<juce::String,int64_t>> recent;
    int64_t windowStart=0;
    int windowCount=0,basePort=18760;
    void streamLoop(juce::StreamingSocket&,bool http);
    void udpLoop();
    juce::var request(const juce::var&);
    bool available(const juce::String& kind,int index) const;
public:
    std::atomic<int> received{0},rejected{0},duplicates{0};
    explicit RemoteService(Engine& e):engine(e){}
    ~RemoteService(){stop();}
    juce::String start(int port,const juce::String& pairingToken,const juce::String& address={});
    void stop();
    bool enabled()const{return running.load();}
    int port()const{return basePort;}
    // Finish admissions already in progress before the GUI replaces a plugin or set.
    void allowCommands(bool allow){std::lock_guard<std::mutex> guard(mutex);accepting=allow;}
    void publish(juce::var value){std::lock_guard<std::mutex> guard(mutex);catalog=std::move(value);}
    juce::var state();
    static juce::var decodeOsc(const void*,int);
    static juce::MemoryBlock encodeOscReply(const juce::String& json);
};
int runRemoteTests(const juce::File& report);
class RemoteFixture final:private juce::Timer {
    Engine engine;
    RemoteService remote{engine};
    juce::AudioBuffer<float> buffer{2,960};
    juce::File folder;
    float peak=0;
    int frames=0;
    int64_t started=0;
    void timerCallback()override;
public:
    RemoteFixture(juce::File,int,const juce::String&);
    ~RemoteFixture()override{stopTimer();remote.stop();}
};
}
