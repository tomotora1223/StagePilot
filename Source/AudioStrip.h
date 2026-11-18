#pragma once
#include "LiveView.h"
namespace stage {
class MeterView final:public juce::Component,public juce::SettableTooltipClient {
    MeterReading reading;juce::String signature,title;
    static float fraction(float value){return juce::jlimit(0.0f,1.0f,(juce::Decibels::gainToDecibels(value,-60.0f)+60)/60);}
public:
    explicit MeterView(juce::String text):title(std::move(text)){setTooltip(jp("太い帯は300msの平均レベル（RMS）。細い線はピーク、白い線は1秒保持したピーク。目盛りはdBFSです。"));}
    void setReading(MeterReading value){juce::String next;for(int c=0;c<2;++c)next+=juce::String(fraction(value.peak[(size_t)c]),3)+":"+juce::String(fraction(value.rms[(size_t)c]),3)+":"+juce::String(fraction(value.hold[(size_t)c]),3);next+=juce::String(juce::Decibels::gainToDecibels(juce::jmax(value.peak[0],value.peak[1]),-100.0f),1)+juce::String((int)value.clipped);reading=value;if(next!=signature){signature=next;repaint();}}
    void paint(juce::Graphics& g)override{
        auto b=getLocalBounds().toFloat();g.setColour(juce::Colour(0xff1b242e));g.fillRoundedRectangle(b,8);g.setColour(juce::Colour(0xffe4edf5));g.setFont(uiFont(15,juce::Font::bold));g.drawText(title,12,4,getWidth()-160,23,juce::Justification::centredLeft);
        auto db=juce::Decibels::gainToDecibels(juce::jmax(reading.peak[0],reading.peak[1]),-100.0f);g.setFont(uiFont(13));g.setColour(reading.clipped?juce::Colour(0xffff8994):juce::Colour(0xffc1d2df));g.drawText(reading.clipped?jp("音割れ"):db<=-100?jp("−∞ dBFS"):juce::String(db,1)+" dBFS",getWidth()-170,4,88,23,juce::Justification::centredRight);
        float x=28,w=getWidth()-118.0f;
        for(int c=0;c<2;++c){float y=31+c*14;g.setFont(uiFont(11));g.setColour(juce::Colour(0xff9dabbc));g.drawText(c==0?"L":"R",11,(int)y-2,12,13,juce::Justification::centred);g.setColour(juce::Colour(0xff0a1119));g.fillRoundedRectangle(x,y,w,9,2);juce::ColourGradient colours(juce::Colour(0xff68d7b5),x,0,juce::Colour(0xffff657c),x+w,0,false);colours.addColour(.88,juce::Colour(0xffffc06d));g.setGradientFill(colours);g.fillRoundedRectangle(x,y,w*fraction(reading.rms[(size_t)c]),9,2);g.setColour(juce::Colour(0xff82eacf));g.fillRect(x+w*fraction(reading.peak[(size_t)c])-1,y,2.0f,9.0f);g.setColour(juce::Colours::white);g.fillRect(x+w*fraction(reading.hold[(size_t)c])-1,y,1.0f,9.0f);}
        g.setFont(uiFont(10));g.setColour(juce::Colour(0xff9dabbc));for(int db:{-60,-48,-36,-24,-12,-6,0}){float at=x+w*(db+60)/60.0f;g.drawText(juce::String(db),(int)at-13,60,26,13,juce::Justification::centred);}
    }
};
class AudioStrip final:public juce::Component {
    Engine& engine;MeterView output{jp("最終出力")},source{jp("選択音源")};
    juce::Slider master,volume;juce::ComboBox selector;
    int focused=0;juce::String inventory;bool refreshing=false;
    static void knob(juce::Slider& s,double reset){s.setRange(0,100,1);s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,68,18);s.setTextValueSuffix(" %");enableReset(s,reset);s.setTooltip(jp("ドラッグで音量を変更。ダブルクリックで標準値へ戻します。"));s.setWantsKeyboardFocus(false);}
public:
    std::function<void(int)> onFocus;
    std::function<void(float)> onMaster;
    std::function<void(int,float)> onGain;
    std::function<void()> onGainBegin;
    explicit AudioStrip(Engine& e):engine(e){for(auto* c:std::vector<juce::Component*>{&output,&source,&master,&volume,&selector})addAndMakeVisible(c);knob(master,70);knob(volume,100);master.onValueChange=[this]{if(!refreshing&&onMaster)onMaster((float)master.getValue()/100);};volume.onDragStart=[this]{if(onGainBegin)onGainBegin();};volume.onValueChange=[this]{if(!refreshing&&onGain)onGain(focused,(float)volume.getValue()/100);};selector.onChange=[this]{if(refreshing)return;focused=selector.getSelectedId()-1;if(onFocus)onFocus(focused);refresh(true);};selector.setTooltip(jp("音量を見て操作する音源を選びます。演奏中の音色の組み合わせは変えません。"));refresh(true);}
    void focus(int index){focused=juce::jlimit(0,8,index);refresh(true);}
    int focus()const{return focused;}
    void refresh(bool structure=false){output.setReading(engine.outputMeter.read());source.setReading(focused==8?engine.sequence.meter.read():engine.slots[(size_t)focused].meter.read());if(!structure)return;refreshing=true;juce::String next;for(auto& s:engine.slots)next+=s.name+juce::String((int)(s.plugin!=nullptr));next+=engine.sequence.name;if(next!=inventory){inventory=next;selector.clear();for(int i=0;i<8;++i){auto& s=engine.slots[(size_t)i];selector.addItem(juce::String(i+1)+"  "+(s.plugin?s.name:jp("未設定")),i+1);selector.setItemEnabled(i+1,s.plugin!=nullptr);}selector.addItem(jp("打ち込み：")+engine.sequence.name,9);}
        selector.setSelectedId(focused+1,juce::dontSendNotification);bool ready=focused==8?engine.sequence.plugin!=nullptr:engine.slots[(size_t)focused].plugin!=nullptr;volume.setEnabled(ready);
        if(!master.isMouseButtonDown())master.setValue(engine.masterValue.load()*100,juce::dontSendNotification);if(!volume.isMouseButtonDown())volume.setValue((focused==8?engine.sequence.gain.load():engine.slots[(size_t)focused].gain.load())*100,juce::dontSendNotification);refreshing=false;
    }
    void resized()override{int half=(getWidth()-10)/2;output.setBounds(0,0,half,getHeight());source.setBounds(half+10,0,getWidth()-half-10,getHeight());master.setBounds(half-78,5,70,getHeight()-10);volume.setBounds(getWidth()-78,5,70,getHeight()-10);selector.setBounds(half+100,3,getWidth()-half-283,25);}
    bool testReset(){master.setValue(18,juce::dontSendNotification);volume.setValue(23,juce::dontSendNotification);simulateDoubleClick(master);simulateDoubleClick(volume);return master.getValue()==70&&volume.getValue()==100;}
};
}
