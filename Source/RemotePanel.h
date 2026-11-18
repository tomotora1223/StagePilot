#pragma once
#include "Remote.h"
#include "Locale.h"

namespace stage {
class RemotePanel final:public juce::Component,private juce::Timer {
    RemoteService& remote;
    juce::File config;
    juce::Label title,help,portLabel,keyLabel,addresses,feedback;
    juce::TextEditor port,key;
    juce::ComboBox hosts;
    juce::TextButton enable{jp("スマホからの操作を開始")},copy{jp("接続情報をコピー")};
    void timerCallback()override{feedback.setText(remote.enabled()?jp("待受中  /  操作受付 ")+juce::String(remote.received.load())+jp(" 件  /  HTTP・TCP・OSC対応"):jp("停止中。開始してから、Androidアプリに接続情報を入れてください。"),juce::dontSendNotification);}
public:
    RemotePanel(RemoteService& service,juce::File file):remote(service),config(file){
        for(auto* c:std::vector<juce::Component*>{&title,&help,&portLabel,&keyLabel,&addresses,&feedback,&port,&key,&hosts,&enable,&copy})addAndMakeVisible(c);
        auto settings=juce::JSON::parse(config);port.setText(juce::String(settings.hasProperty("port")?(int)settings["port"]:18760));port.setInputRestrictions(5,"0123456789");
        auto pairing=settings["token"].toString();if(pairing.length()<8)pairing=juce::Uuid().toString().removeCharacters("-");key.setText(pairing);key.setReadOnly(true);
        title.setText(jp("スマホを演奏用の操作パネルに"),juce::dontSendNotification);title.setFont(uiFont(25,juce::Font::bold));
        help.setText(jp("① 同じWi-Fi、またはUSBテザリングでPCと接続\n② 下の情報をAndroidアプリの「接続」に入力\n③ 音色・効果音を選んで、自分の名前のボタンを作成"),juce::dontSendNotification);help.setFont(uiFont(17));
        portLabel.setText(jp("基本ポート"),juce::dontSendNotification);keyLabel.setText(jp("接続キー"),juce::dontSendNotification);
        juce::StringArray ips;for(auto ip:juce::IPAddress::getAllAddresses(false)){auto value=ip.toString();int first=value.upToFirstOccurrenceOf(".",false,false).getIntValue();if(first>0&&first<224&&first!=127&&!value.startsWith("169.254."))ips.addIfNotAlreadyThere(value);}if(ips.isEmpty())ips.add("127.0.0.1");int preferred=1;for(int i=0;i<ips.size();++i){hosts.addItem(ips[i],i+1);if(ips[i].startsWith("192.168."))preferred=i+1;}hosts.setSelectedId(preferred);
        addresses.setText(jp("PCのIPアドレス（スマホと同じWi-Fi・USBのものを選ぶ）"),juce::dontSendNotification);addresses.setFont(uiFont(16));hosts.setTooltip(jp("複数ある場合は、スマホと同じネットワークのIPを選びます。接続できない場合は別のIPも確認してください。"));
        enable.onClick=[this]{if(remote.enabled())remote.stop();else{auto error=remote.start(port.getText().getIntValue(),key.getText());if(error.isNotEmpty()){feedback.setText(error,juce::dontSendNotification);return;}auto* o=new juce::DynamicObject();o->setProperty("port",remote.port());o->setProperty("token",key.getText());config.getParentDirectory().createDirectory();config.replaceWithText(juce::JSON::toString(juce::var(o)));}enable.setButtonText(remote.enabled()?jp("スマホからの操作を停止"):jp("スマホからの操作を開始"));port.setEnabled(!remote.enabled());timerCallback();};
        copy.onClick=[this]{juce::SystemClipboard::copyTextToClipboard("stagepilot://"+hosts.getText()+":"+port.getText()+"/"+key.getText());feedback.setText(jp("コピーしました。Androidの「接続情報を貼り付け」で取り込めます。IPは接続画面で変更できます。"),juce::dontSendNotification);};
        enable.setButtonText(remote.enabled()?jp("スマホからの操作を停止"):jp("スマホからの操作を開始"));port.setEnabled(!remote.enabled());setSize(720,570);timerCallback();startTimerHz(1);
    }
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff101419));}
    void resized()override{int w=getWidth()-48;title.setBounds(24,20,w,40);help.setBounds(24,70,w,100);addresses.setBounds(24,184,w,34);hosts.setBounds(24,226,w,38);portLabel.setBounds(24,306,126,30);port.setBounds(156,306,130,32);keyLabel.setBounds(24,352,126,30);key.setBounds(156,352,w-132,32);enable.setBounds(24,408,320,44);copy.setBounds(360,408,getWidth()-384,44);feedback.setBounds(24,468,w,76);}
};
}
