#include "Remote.h"
#include <cmath>

namespace stage {
using namespace juce;
namespace {
var result(bool ok,const String& message={}){auto* o=new DynamicObject();o->setProperty("ok",ok);if(message.isNotEmpty())o->setProperty("error",message);return var(o);}
bool integer(const var& v){return v.isInt()||v.isInt64();}
void oscString(MemoryOutputStream& out,const String& s){out.write(s.toRawUTF8(),s.getNumBytesAsUTF8());out.writeByte(0);while(out.getDataSize()%4)out.writeByte(0);}
bool writeAll(StreamingSocket& socket,const String& text){int offset=0,count=text.getNumBytesAsUTF8();while(offset<count){if(socket.waitUntilReady(false,300)!=1)return false;int n=socket.write(text.toRawUTF8()+offset,count-offset);if(n<=0)return false;offset+=n;}return true;}
}
String RemoteService::start(int portNumber,const String& pairingToken,const String& address){
    stop();if(portNumber<1024||portNumber>65533||pairingToken.length()<8)return String::fromUTF8("ポートは1024〜65533、接続キーは8文字以上にしてください。");
    basePort=portNumber;token=pairingToken;httpSocket=std::make_unique<StreamingSocket>();tcpSocket=std::make_unique<StreamingSocket>();oscSocket=std::make_unique<DatagramSocket>();
    if(!httpSocket->createListener(basePort,address)||!tcpSocket->createListener(basePort+1,address)||!oscSocket->bindToPort(basePort+2,address)){stop();return String::fromUTF8("ポートを開けません。別のポート番号を選んでください。");}
    {std::lock_guard<std::mutex> guard(mutex);recent.clear();windowStart=0;windowCount=0;}
    received=0;rejected=0;duplicates=0;running=true;httpThread=std::thread([this]{streamLoop(*httpSocket,true);});tcpThread=std::thread([this]{streamLoop(*tcpSocket,false);});oscThread=std::thread([this]{udpLoop();});return {};
}
void RemoteService::stop(){
    running=false;
    // Listener waits are bounded; avoid a close/accept race in the underlying socket implementation.
    if(httpThread.joinable())httpThread.join();if(tcpThread.joinable())tcpThread.join();if(oscThread.joinable())oscThread.join();
    httpSocket.reset();tcpSocket.reset();oscSocket.reset();
}
bool RemoteService::available(const String& kind,int index)const{
    if(auto* items=catalog["items"].getArray())for(auto& item:*items)if(item["kind"].toString()==kind&&(int)item["index"]==index)return true;return false;
}
var RemoteService::state(){
    std::lock_guard<std::mutex> guard(mutex);auto* o=new DynamicObject();o->setProperty("ok",true);o->setProperty("version",1);o->setProperty("transposeStep",true);o->setProperty("remoteControls",true);o->setProperty("items",catalog["items"]);o->setProperty("session",catalog["session"]);
    o->setProperty("scene",engine.activeScene.load());o->setProperty("instrument",engine.selected.load());o->setProperty("master",engine.masterValue.load());o->setProperty("transpose",engine.performanceTranspose.load());o->setProperty("nowMs",Time::currentTimeMillis());o->setProperty("ready",accepting.load());
    Array<var> playing;for(int i=0;i<clipCount;++i)if(engine.clips[(size_t)i].playing)playing.add(i);o->setProperty("playing",playing);o->setProperty("received",received.load());o->setProperty("rejected",rejected.load());return var(o);
}
var RemoteService::request(const var& input){
    if(!input.isObject()||!input["token"].isString()||input["token"].toString()!=token){++rejected;return result(false,"auth");}
    if(input["op"].toString()=="state")return state();
    if(input["op"].toString()!="command"){++rejected;return result(false,"operation");}
    std::lock_guard<std::mutex> guard(mutex);
    auto fail=[this](const String& why){++rejected;return result(false,why);};
    String id=input["id"].toString(),action=input["action"].toString();int64_t now=Time::currentTimeMillis();
    if(!input["id"].isString()||id.isEmpty()||id.length()>96)return fail("id");
    while(!recent.empty()&&now-recent.front().second>10000)recent.pop_front();
    for(auto& old:recent)if(old.first==id){++duplicates;auto reply=result(true);reply.getDynamicObject()->setProperty("id",id);reply.getDynamicObject()->setProperty("duplicate",true);return reply;}
    if(!integer(input["expires"])||(int64_t)input["expires"]<now||(int64_t)input["expires"]>now+5000)return fail("expired");
    if(!accepting)return fail("busy");
    if(now-windowStart>=1000){windowStart=now;windowCount=0;}if(++windowCount>120)return fail("rate");
    auto decode=[&](const var& command,const String& action,Event& event)->String {
        int index=0;if(command.hasProperty("index")){if(!integer(command["index"])||(int64_t)command["index"]<0||(int64_t)command["index"]>=8)return String("index");index=(int)command["index"];}
        event.type=Event::Control;event.b=index;event.value=1;
        if(action=="scene"){if(!available("scene",index))return String("unavailable");event.a=SelectScene;}
        else if(action=="instrument"){if(!available("instrument",index))return String("unavailable");event.a=Select;}
        else if(action=="effect"||action=="effectStop"){if(!available("effect",index))return String("unavailable");event.a=action=="effect"?ClipPlay:ClipStop;}
        else if(action=="fade")event.a=FadeOut;
        else if(action=="restore")event.a=Restore;
        else if(action=="panic")event.a=Panic;
        else if(action=="effectsStop")event.a=ClipStopAll;
        else if(action=="sequence")event.a=SequencePlay;
        else if(action=="sequenceStop")event.a=SequenceStop;
        else if(action=="next"||action=="previous"){
            bool found=false;for(int n=0;n<8;++n)found|=available("instrument",n)||available("scene",n);if(!found)return String("unavailable");
            event.a=action=="next"?NextPerformance:PreviousPerformance;
        }else if(action=="transposeStep"){
            auto v=command["value"];if((!v.isDouble()&&!integer(v))||((double)v!=1.0&&(double)v!= -1.0))return String("value");event.a=PerformanceTransposeStep;event.value=(float)v;
        }else if(action=="master"||action=="transpose"||action=="masterStep"||action=="pitch"||action=="slotGain"){
            auto v=command["value"];if(!v.isDouble()&&!integer(v))return String("value");float value=(float)v;if(!std::isfinite(value))return String("value");
            if(action=="master"||action=="slotGain"){
                if(value<0||value>1)return String("value");
                if(action=="slotGain"&&!available("instrument",index))return String("unavailable");
                event.a=action=="master"?Master:SlotGain;
            }else if(action=="masterStep"){
                if(value< -1||value>1)return String("value");
                event.a=MasterStep;
            }else if(action=="pitch"){
                if(value<0||value>16383||std::floor(value)!=value)return String("value");
                event.a=RemotePitch;
            }
            else {if(value< -24||value>24||std::floor(value)!=value)return String("value");event.a=PerformanceTranspose;}
            event.value=value;
        }else return String("action");
        return {};
    };
    Event event;
    if(action.startsWith("batch:")){
        auto payload=JSON::parse(action.substring(6));
        const auto* list=payload.getArray();
        if(list==nullptr||list->isEmpty()||list->size()>remoteBatchLimit)return fail("batch");
        event.type=Event::RemoteBatch;
        for(const auto& operation:*list){
            if(!operation.isObject())return fail("batch");
            Event command;
            const auto error=decode(operation,operation["action"].toString(),command);
            if(error.isNotEmpty()||command.a==RemotePitch)return fail(error.isNotEmpty()?error:"batch");
            event.batch[event.a++]={command.a,command.b,command.value};
        }
    }else{
        const auto error=decode(input,action,event);
        if(error.isNotEmpty())return fail(error);
    }
    if(!engine.post(event))return fail("queue");recent.emplace_back(id,now);if(recent.size()>1024)recent.pop_front();++received;auto reply=result(true);reply.getDynamicObject()->setProperty("id",id);reply.getDynamicObject()->setProperty("duplicate",false);return reply;
}
void RemoteService::streamLoop(StreamingSocket& listener,bool http){
    while(running){
        if(listener.waitUntilReady(true,100)!=1)continue;std::unique_ptr<StreamingSocket> client(listener.waitForNextConnection());if(!client)continue;
        String packet;bool complete=false;uint32 deadline=Time::getMillisecondCounter()+1200;
        while(running&&Time::getMillisecondCounter()<deadline&&packet.getNumBytesAsUTF8()<16384){
            if(client->waitUntilReady(true,50)!=1)continue;char bytes[2048];int n=client->read(bytes,sizeof(bytes),false);if(n<=0)break;packet+=String::fromUTF8(bytes,n);
            if(!http){if(packet.containsChar('\n')){complete=true;break;}}
            else {int split=packet.indexOf("\r\n\r\n");if(split>=0){auto header=packet.substring(0,split);StringArray lines;lines.addLines(header);int length=-1;for(auto& line:lines)if(line.startsWithIgnoreCase("Content-Length:"))length=line.fromFirstOccurrenceOf(":",false,false).trim().getIntValue();if(length>=0&&length<=8192&&packet.substring(split+4).getNumBytesAsUTF8()>=length){complete=true;break;}}}
        }
        var reply=result(false,"request");
        if(complete&&packet.getNumBytesAsUTF8()<=16384){String json=http?packet.fromFirstOccurrenceOf("\r\n\r\n",false,false):packet.upToFirstOccurrenceOf("\n",false,false);bool route=!http||packet.startsWith("POST /api HTTP/1.");if(route&&json.getNumBytesAsUTF8()<=8192)reply=request(JSON::parse(json));}
        String body=JSON::toString(reply,true),response;
        if(http)response="HTTP/1.1 "+String((bool)reply["ok"]?"200 OK":"400 Bad Request")+"\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+String(body.getNumBytesAsUTF8())+"\r\n\r\n"+body;
        else response=body+"\n";writeAll(*client,response);
    }
}
var RemoteService::decodeOsc(const void* bytes,int size){
    if(size<4||size>8192||size%4)return {};auto* data=(const uint8_t*)bytes;int at=0;
    auto readString=[&](String& value){int begin=at;while(at<size&&data[at]!=0)++at;if(at==size)return false;value=String::fromUTF8((const char*)data+begin,at-begin);++at;while(at%4){if(at>=size||data[at++]!=0)return false;}return true;};
    String address,tags,auth,id,action,indexText,expiresText;if(!readString(address)||!readString(tags)||address!="/stagepilot/command"||tags!=",sssifs")return {};
    if(!readString(auth)||!readString(id)||!readString(action)||at+8>size)return {};int index=(int)ByteOrder::bigEndianInt(data+at);at+=4;uint32 bits=ByteOrder::bigEndianInt(data+at);at+=4;float value;std::memcpy(&value,&bits,4);if(!readString(expiresText)||at!=size||expiresText.containsOnly("0123456789")==false||expiresText.isEmpty())return {};
    auto* o=new DynamicObject();o->setProperty("op","command");o->setProperty("token",auth);o->setProperty("id",id);o->setProperty("action",action);o->setProperty("index",index);o->setProperty("value",value);o->setProperty("expires",expiresText.getLargeIntValue());return var(o);
}
MemoryBlock RemoteService::encodeOscReply(const String& json){MemoryOutputStream out;oscString(out,"/stagepilot/reply");oscString(out,",s");oscString(out,json);return out.getMemoryBlock();}
void RemoteService::udpLoop(){
    while(running){if(oscSocket->waitUntilReady(true,100)!=1)continue;char bytes[8196];String host;int portNumber=0;int count=oscSocket->read(bytes,sizeof(bytes),false,host,portNumber);if(count<=0)continue;
        auto input=decodeOsc(bytes,count);var reply=input.isObject()?request(input):result(false,"osc");if(!input.isObject())++rejected;auto output=encodeOscReply(JSON::toString(reply,true));if(oscSocket->waitUntilReady(false,100)==1)oscSocket->write(host,portNumber,output.getData(),(int)output.getSize());
    }
}
}
