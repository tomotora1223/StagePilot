#pragma once
#include "Remote.h"
#include "Locale.h"
#include "../ThirdParty/QRCode/qrcodegen.hpp"

namespace stage {
inline juce::Image renderPairingQr(const juce::String& payload) {
    const auto code=qrcodegen::QrCode::encodeText(payload.toRawUTF8(),qrcodegen::QrCode::Ecc::MEDIUM);
    constexpr int quiet=4, scale=6;
    const int size=(code.getSize()+2*quiet)*scale;
    juce::Image pixels(juce::Image::RGB,size,size,true);
    juce::Graphics g(pixels);g.fillAll(juce::Colours::white);g.setColour(juce::Colours::black);
    for(int y=0;y<code.getSize();++y)for(int x=0;x<code.getSize();++x)
        if(code.getModule(x,y))g.fillRect((x+quiet)*scale,(y+quiet)*scale,scale,scale);
    return pixels;
}
class RemotePanel final:public juce::Component,private juce::Timer {
    RemoteService& remote;
    juce::File config;
    juce::Label title,help,portLabel,keyLabel,addresses,feedback,qrLabel;
    juce::ImageComponent qr;
    juce::TextEditor port,key;
    juce::ComboBox hosts;
    juce::TextButton enable{jp("スマホからの操作を開始")},copy{jp("接続情報をコピー")},reload{jp("一覧を更新")};
    juce::String pairingLink() const {
        return "stagepilot://"+hosts.getText()+":"+port.getText()+"/"+key.getText();
    }
    void refreshQr() {
        qr.setVisible(remote.enabled());
        qrLabel.setText(remote.enabled()?jp("Android → 接続 → QRを読む"):jp("連携を開始するとQRが表示されます"),juce::dontSendNotification);
        if(!remote.enabled()){qr.setImage({});return;}
        qr.setImage(renderPairingQr(pairingLink()),juce::RectanglePlacement(juce::RectanglePlacement::centred));
    }
    void refreshAddresses() {
        const auto previous=hosts.getText();
        juce::StringArray found;
        for(auto ip:juce::IPAddress::getAllAddresses(false)){
            const auto value=ip.toString();
            const int first=value.upToFirstOccurrenceOf(".",false,false).getIntValue();
            if(first>0&&first<224&&first!=127&&!value.startsWith("169.254."))found.addIfNotAlreadyThere(value);
        }
        if(found.isEmpty())found.add("127.0.0.1");
        hosts.clear(juce::dontSendNotification);
        int preferred=1;
        for(int i=0;i<found.size();++i){
            hosts.addItem(found[i],i+1);
            if(found[i].startsWith("192.168."))preferred=i+1;
        }
        const auto selected=found.indexOf(previous);
        hosts.setSelectedId(selected>=0?selected+1:preferred,juce::dontSendNotification);
    }
    void timerCallback()override{feedback.setText(remote.enabled()?jp("待受中  /  操作受付 ")+juce::String(remote.received.load())+jp(" 件  /  HTTP・TCP・OSC対応"):jp("停止中。開始してから、AndroidでQRを読んでください。"),juce::dontSendNotification);}
public:
    RemotePanel(RemoteService& service,juce::File file):remote(service),config(file){
        for(auto* c:std::vector<juce::Component*>{&title,&help,&portLabel,&keyLabel,&addresses,&feedback,&port,&key,&hosts,&enable,&copy,&reload,&qr,&qrLabel})addAndMakeVisible(c);
        auto settings=juce::JSON::parse(config);port.setText(juce::String(settings.hasProperty("port")?(int)settings["port"]:18760));port.setInputRestrictions(5,"0123456789");
        auto pairing=settings["token"].toString();if(pairing.length()<8||pairing.length()>128||!pairing.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"))pairing=juce::Uuid().toString().removeCharacters("-");key.setText(pairing);key.setReadOnly(true);
        title.setText(jp("スマホを演奏用の操作パネルに"),juce::dontSendNotification);title.setFont(uiFont(25,juce::Font::bold));
        help.setText(jp("① 同じWi-Fiか、USB・Bluetoothテザリングで接続\n② PCで連携開始。Androidの「接続 → QRを読む」を選択"),juce::dontSendNotification);help.setFont(uiFont(17));
        portLabel.setText(jp("基本ポート"),juce::dontSendNotification);keyLabel.setText(jp("接続キー"),juce::dontSendNotification);
        refreshAddresses();
        addresses.setText(jp("PCのIPアドレス（スマホとつないだ回線）"),juce::dontSendNotification);addresses.setFont(uiFont(16));hosts.setTooltip(jp("複数ある場合は、スマホと同じネットワークのIPを選びます。接続できない場合は別のIPも確認してください。"));
        enable.onClick=[this]{if(remote.enabled())remote.stop();else{auto error=remote.start(port.getText().getIntValue(),key.getText());if(error.isNotEmpty()){feedback.setText(error,juce::dontSendNotification);return;}auto* o=new juce::DynamicObject();o->setProperty("port",remote.port());o->setProperty("token",key.getText());config.getParentDirectory().createDirectory();config.replaceWithText(juce::JSON::toString(juce::var(o)));}enable.setButtonText(remote.enabled()?jp("スマホからの操作を停止"):jp("スマホからの操作を開始"));port.setEnabled(!remote.enabled());refreshQr();timerCallback();};
        copy.onClick=[this]{juce::SystemClipboard::copyTextToClipboard(pairingLink());feedback.setText(jp("コピーしました。Androidの「接続情報を貼り付け」で取り込めます。IPは接続画面で変更できます。"),juce::dontSendNotification);};
        enable.setButtonText(remote.enabled()?jp("スマホからの操作を停止"):jp("スマホからの操作を開始"));port.setEnabled(!remote.enabled());hosts.onChange=[this]{refreshQr();};
        reload.onClick=[this]{refreshAddresses();refreshQr();};
        qrLabel.setJustificationType(juce::Justification::centred);
        setSize(790,535);refreshQr();timerCallback();startTimerHz(1);
    }
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff101419));}
    void resized() override {
        auto body=getLocalBounds().reduced(24);
        title.setBounds(body.removeFromTop(42));
        help.setBounds(body.removeFromTop(70));
        body.removeFromTop(10);
        auto actions=body.removeFromBottom(88);
        feedback.setBounds(actions.removeFromBottom(40));
        enable.setBounds(actions.removeFromLeft(324).reduced(0,2));
        actions.removeFromLeft(12);copy.setBounds(actions.removeFromLeft(230).reduced(0,2));
        auto code=body.removeFromRight(250);
        qrLabel.setBounds(code.removeFromBottom(38));
        qr.setBounds(code.reduced(8,0));
        body.removeFromRight(18);
        addresses.setBounds(body.removeFromTop(36));
        auto hostRow=body.removeFromTop(36);
        reload.setBounds(hostRow.removeFromRight(108));
        hostRow.removeFromRight(8);
        hosts.setBounds(hostRow);
        body.removeFromTop(18);
        auto portRow=body.removeFromTop(38);
        portLabel.setBounds(portRow.removeFromLeft(110));port.setBounds(portRow.removeFromLeft(125));
        body.removeFromTop(8);
        keyLabel.setBounds(body.removeFromTop(26));key.setBounds(body.removeFromTop(34));
    }
};
}
