#include "Remote.h"
#include <cmath>
namespace stage {
using namespace juce;
namespace {
var fixtureCatalog(){
    Array<var> items;auto add=[&](String kind,int index,String name,String colour){auto* o=new DynamicObject();o->setProperty("kind",kind);o->setProperty("index",index);o->setProperty("name",name);o->setProperty("color",colour);items.add(var(o));};
    add("instrument",0,String::fromUTF8("ピアノ"),"72d6c2");add("instrument",1,String::fromUTF8("パッド"),"ffbb70");add("scene",0,String::fromUTF8("ピアノ＋パッド"),"72d6c2");add("scene",1,String::fromUTF8("スプリット"),"94baff");add("effect",0,String::fromUTF8("チャイム"),"c3d77a");
    auto* o=new DynamicObject();o->setProperty("items",items);o->setProperty("session","Remote test set");return var(o);
}
void prepareFixture(Engine& e){for(int i=0;i<2;++i)e.slots[(size_t)i].plugin=makeTestSynth();e.clips[0].data=makeDemoClip(0);e.scenes[0].registered=e.scenes[1].registered=true;e.scenes[0].parts[0]={0,127,0,1,true,false};e.scenes[0].parts[1]={0,127,12,.5f,true,true};e.scenes[1].parts[0]={0,59,0,1,true,false};e.scenes[1].parts[1]={60,127,0,1,true,true};e.prepare(48000,960);}
String streamCall(int port,const var& input,bool http){StreamingSocket client;if(!client.connect("127.0.0.1",port,1000))return {};String body=JSON::toString(input,true),packet=http?"POST /api HTTP/1.1\r\nHost: localhost\r\nContent-Length: "+String(body.getNumBytesAsUTF8())+"\r\n\r\n"+body:body+"\n";if(client.write(packet.toRawUTF8(),packet.getNumBytesAsUTF8())<=0)return {};String response;uint32 deadline=Time::getMillisecondCounter()+2000;while(Time::getMillisecondCounter()<deadline){if(client.waitUntilReady(true,100)!=1)continue;char data[2048];int n=client.read(data,sizeof(data),false);if(n<=0)break;response+=String::fromUTF8(data,n);if(!http&&response.containsChar('\n'))break;}return http?response.fromFirstOccurrenceOf("\r\n\r\n",false,false):response.trim();}
var command(String action,String id,int index=0,float value=1){auto* o=new DynamicObject();o->setProperty("op","command");o->setProperty("token","stagepilot-test");o->setProperty("action",action);o->setProperty("id",id);o->setProperty("index",index);o->setProperty("value",value);o->setProperty("expires",Time::currentTimeMillis()+1500);return var(o);}
void oscString(MemoryOutputStream& out,const String& s){out.write(s.toRawUTF8(),s.getNumBytesAsUTF8());out.writeByte(0);while(out.getDataSize()%4)out.writeByte(0);}
MemoryBlock oscCommand(const var& c){MemoryOutputStream out;oscString(out,"/stagepilot/command");oscString(out,",sssifs");oscString(out,c["token"]);oscString(out,c["id"]);oscString(out,c["action"]);out.writeIntBigEndian((int)c["index"]);out.writeFloatBigEndian((float)c["value"]);oscString(out,c["expires"].toString());return out.getMemoryBlock();}
}
int runRemoteTests(const File& report){
    int failures=0;Array<var> results;auto check=[&](bool ok,String text){auto* o=new DynamicObject();o->setProperty("test",text);o->setProperty("passed",ok);results.add(var(o));if(!ok)++failures;};Engine e;prepareFixture(e);RemoteService server(e);server.publish(fixtureCatalog());int port=19760;auto error=server.start(port,"stagepilot-test","127.0.0.1");check(error.isEmpty(),"Bind HTTP, TCP and OSC listeners");
    auto stateRequest=JSON::parse("{\"op\":\"state\",\"token\":\"stagepilot-test\"}");auto stateReply=JSON::parse(streamCall(port,stateRequest,true));check((bool)stateReply["ok"]&&stateReply["items"].size()==5,"HTTP state returns Japanese instrument, registered scene and effect catalog");
    auto badAuth=command("effect","auth");badAuth.getDynamicObject()->setProperty("token","wrong");check(!(bool)JSON::parse(streamCall(port,badAuth,true))["ok"]&&server.received==0,"Wrong pairing key rejected without audio events");
    AudioBuffer<float> out(2,960);auto render=[&]{e.render(out.getArrayOfWritePointers(),2,960);};
    for(int protocol=0;protocol<3;++protocol){
        auto send=[&](var c){if(protocol<2)return JSON::parse(streamCall(port+protocol,c,protocol==0));DatagramSocket socket;socket.bindToPort(0,"127.0.0.1");auto packet=oscCommand(c);socket.write("127.0.0.1",port+2,packet.getData(),(int)packet.getSize());char bytes[4096];if(socket.waitUntilReady(true,1500)!=1)return var();int n=socket.read(bytes,sizeof(bytes),false);if(n<24)return var();int offset=0;for(int strings=0;strings<2;++strings){while(offset<n&&bytes[offset])++offset;offset=(offset+4)&~3;}return JSON::parse(String::fromUTF8(bytes+offset,n-offset));};
        String prefix=String(protocol)+"-";check((bool)send(command("scene",prefix+"scene",1))["ok"],"Protocol "+String(protocol)+": scene command acknowledged");render();check(e.activeScene==1&&e.slots[0].keyHigh==59&&e.slots[1].keyLow==60,"Protocol "+String(protocol)+": split scene applied by audio callback");
        check((bool)send(command("instrument",prefix+"instrument",0))["ok"],"Protocol "+String(protocol)+": instrument switch acknowledged");render();check(e.activeScene==-1&&e.selected==0,"Protocol "+String(protocol)+": standalone instrument applied");
        send(command("instrument",prefix+"mixed-start",1));render();auto* shared=e.slots[0].plugin.get();
        send(command("next",prefix+"mixed-next"));render();check(e.activeScene==0&&e.slots[0].plugin.get()==shared,"Protocol "+String(protocol)+": next crosses instrument to combined sound without recreating plugin");
        send(command("previous",prefix+"mixed-previous"));render();check(e.activeScene==-1&&e.selected==1,"Protocol "+String(protocol)+": previous crosses combined sound to instrument");
        send(command("instrument",prefix+"mixed-wrap-start",0));render();send(command("previous",prefix+"mixed-wrap-back"));render();check(e.activeScene==1,"Protocol "+String(protocol)+": previous wraps first instrument to last combined sound");
        send(command("next",prefix+"mixed-wrap-forward"));render();check(e.activeScene==-1&&e.selected==0,"Protocol "+String(protocol)+": next wraps last combined sound to first instrument");
        auto nextQueued=command("next",prefix+"queued-next");send(nextQueued);send(nextQueued);send(command("next",prefix+"queued-next2"));render();check(e.activeScene==0,"Protocol "+String(protocol)+": consecutive next commands advance twice with duplicate ID applied once");
        send(command("previous",prefix+"queued-prev"));send(command("previous",prefix+"queued-prev2"));render();check(e.activeScene==-1&&e.selected==0,"Protocol "+String(protocol)+": consecutive previous commands apply in audio queue order");
        auto effect=command("effect",prefix+"effect",0);check((bool)send(effect)["ok"],"Protocol "+String(protocol)+": effect acknowledged");render();check(e.clips[0].playing&&out.getMagnitude(0,960)>0,"Protocol "+String(protocol)+": effect produces finite nonzero audio");int before=server.received;auto duplicate=send(effect);check((bool)duplicate["ok"]&&(bool)duplicate["duplicate"]&&server.received==before,"Protocol "+String(protocol)+": duplicate effect ID never replays");
        send(command("master",prefix+"master",0,.42f));send(command("transpose",prefix+"shift",0,7));render();check(std::abs(e.masterValue.load()-.42f)<.001f&&e.performanceTranspose==7,"Protocol "+String(protocol)+": output volume and transpose applied");
        auto step=command("transposeStep",prefix+"step",0,1);send(step);send(step);send(command("transposeStep",prefix+"step2",0,1));render();check(e.performanceTranspose==9,"Protocol "+String(protocol)+": queued increments accumulate; duplicate retry increments only once");
        send(command("transposeStep",prefix+"down",0,-1));render();check(e.performanceTranspose==8,"Protocol "+String(protocol)+": decrement applies current value on audio thread");
        send(command("transpose",prefix+"upper",0,24));send(command("transposeStep",prefix+"upper-step",0,1));render();check(e.performanceTranspose==24,"Protocol "+String(protocol)+": increment clamps at +24");
        send(command("transpose",prefix+"lower",0,-24));send(command("transposeStep",prefix+"lower-step",0,-1));render();check(e.performanceTranspose== -24,"Protocol "+String(protocol)+": decrement clamps at -24");
        check(!(bool)send(command("transposeStep",prefix+"fraction-step",0,.5f))["ok"]&&!(bool)send(command("transposeStep",prefix+"big-step",0,2))["ok"],"Protocol "+String(protocol)+": invalid steps rejected");
        const auto bundle=String("batch:")+R"json([{"action":"scene","index":0},{"action":"transpose","value":-3},{"action":"master","value":0.4},{"action":"masterStep","value":0.05},{"action":"slotGain","index":1,"value":0.25}])json";
        const auto compound=command(bundle,prefix+"compound");
        const bool accepted=(bool)send(compound)["ok"];
        const bool repeated=(bool)send(compound)["duplicate"];
        render();
        check(accepted&&repeated&&e.activeScene==0&&e.performanceTranspose==-3&&
              std::abs(e.masterValue.load()-.45f)<.001f&&e.slots[1].gain==.25f,
              "Protocol "+String(protocol)+": same-block button actions execute in order and deduplicate");
        const bool mod=(bool)send(command("modulation",prefix+"modulation",0,73))["ok"];
        const bool expr=(bool)send(command("expression",prefix+"expression",0,26))["ok"];
        render();
        const auto wheels=server.state();
        check(mod&&expr&&(int)wheels["modulation"]==73&&(int)wheels["expression"]==26,
              "Protocol "+String(protocol)+": both MIDI wheels hold independently and report their current positions");
        check(!(bool)send(command("modulation",prefix+"invalid-wheel",0,128))["ok"],
              "Protocol "+String(protocol)+": reject MIDI CC outside the 7-bit range");
        const auto invalid=String("batch:")+R"json([{"action":"transpose","value":12},{"action":"scene","index":7}])json";
        const bool rejected=!(bool)send(command(invalid,prefix+"invalid-compound"))["ok"];
        render();
        check(rejected&&e.performanceTranspose==-3,"Protocol "+String(protocol)+": reject an invalid batch without applying its first action");
        check((bool)send(command("pitch",prefix+"bend",0,16383))["ok"]&&
              !(bool)send(command("pitch",prefix+"invalid-bend",0,16384))["ok"],
              "Protocol "+String(protocol)+": pitch wheel accepts only valid 14-bit values");
        check(!(bool)send(command("scene",prefix+"empty",7))["ok"],"Protocol "+String(protocol)+": unregistered scene rejected");auto expired=command("effect",prefix+"expired");expired.getDynamicObject()->setProperty("expires",Time::currentTimeMillis()-1);check(!(bool)send(expired)["ok"],"Protocol "+String(protocol)+": expired operation rejected");
        send(command("panic",prefix+"panic"));render();check(!e.clips[0].playing&&out.getMagnitude(0,960)==0&&e.previewKey==-1,"Protocol "+String(protocol)+": panic clears effect playback and rendered audio");
    }
    auto http=[&](var c){return JSON::parse(streamCall(port,c,true));};
    http(command("instrument","pitch-test-instrument",0));
    http(command("transpose","pitch-test-shift",0,0));
    render();
    e.midi(0x90,69,100);
    auto periods=[&]{
        int crossings=0;
        float previous=0;
        for(int block=0;block<10;++block){
            render();
            for(int n=0;n<out.getNumSamples();++n){
                float sample=out.getSample(0,n);
                if(previous<0&&sample>=0)++crossings;
                previous=sample;
            }
        }
        return crossings;
    };
    const int centre=periods();
    http(command("pitch","pitch-test-up",0,16383));
    const int raised=periods();
    http(command("pitch","pitch-test-centre",0,8192));
    const int released=periods();
    http(command("pitch","pitch-test-disconnect",0,16383));
    render();
    Thread::sleep(650);
    const int recovered=periods();
    check(raised>centre+6&&std::abs(centre-released)<=1&&std::abs(centre-recovered)<=1,
          "Synth bends a held note and returns to centre on release or timeout");
    http(command("expression","expression-silence",0,0));
    render();
    const bool silent=out.getMagnitude(0,960)==0.0f;
    http(command("expression","expression-full",0,127));
    render();
    check(silent&&out.getMagnitude(0,960)>0.0f,"CC11 changes held-note output in the built-in synth");
    check(!(bool)JSON::parse(streamCall(port,command("master","invalid-value",0,1.5f),true))["ok"],"Out-of-range master value rejected");server.allowCommands(false);check(!(bool)JSON::parse(streamCall(port,command("effect","busy"),true))["ok"],"Plugin loading gate rejects remote audio operations");server.allowCommands(true);
    auto huge=command("instrument","huge-index");huge.getDynamicObject()->setProperty("index",(int64_t)4294967296LL);check(!(bool)JSON::parse(streamCall(port,huge,true))["ok"],"64-bit source index cannot wrap around into an available instrument");auto fraction=command("instrument","fraction-index");fraction.getDynamicObject()->setProperty("index",.5);check(!(bool)JSON::parse(streamCall(port+1,fraction,false))["ok"],"Fractional source index rejected on TCP");
    auto packet=oscCommand(command("effect","osc-padded"));check(!RemoteService::decodeOsc(packet.getData(),(int)packet.getSize()-1).isObject(),"Truncated OSC rejected");check(!RemoteService::decodeOsc("not an OSC packet",16).isObject(),"Unknown OSC address rejected");
    auto begin=Time::getMillisecondCounter();server.stop();check(Time::getMillisecondCounter()-begin<1500,"Stopping remote listeners completes without audio thread involvement");check(server.start(port,"stagepilot-test","127.0.0.1").isEmpty(),"Listeners can restart on same ports");server.stop();e.audioDeviceStopped();auto* root=new DynamicObject();root->setProperty("failures",failures);root->setProperty("results",results);report.getParentDirectory().createDirectory();report.replaceWithText(JSON::toString(var(root)));return failures?1:0;
}
RemoteFixture::RemoteFixture(File target,int port,const String& token):folder(target){folder.createDirectory();prepareFixture(engine);remote.publish(fixtureCatalog());auto error=remote.start(port,token,"127.0.0.1");auto* ready=new DynamicObject();ready->setProperty("ok",error.isEmpty());ready->setProperty("error",error);ready->setProperty("opensAudioDevice",false);folder.getChildFile("ready.json").replaceWithText(JSON::toString(var(ready)));started=Time::currentTimeMillis();startTimerHz(50);}
void RemoteFixture::timerCallback(){engine.render(buffer.getArrayOfWritePointers(),2,960);peak=jmax(peak,buffer.getMagnitude(0,960));if(++frames%10==0){auto snapshot=remote.state();snapshot.getDynamicObject()->setProperty("offlinePeakMaximum",peak);snapshot.getDynamicObject()->setProperty("renderBlocks",frames);folder.getChildFile("state.json").replaceWithText(JSON::toString(snapshot));}if(folder.getChildFile("stop.flag").exists()||Time::currentTimeMillis()-started>900000)JUCEApplication::getInstance()->quit();}
}
