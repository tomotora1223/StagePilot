#pragma once
#include "Engine.h"
namespace stage {
inline juce::String jp(const char* text){return juce::String::fromUTF8(text);}
class MixerPanel final : public juce::Component, private juce::Timer {
    Engine& engine;
    std::function<void()> changed;
    juce::Label title,hint,nameHeader,layerHeader,transposeHeader,gainHeader;
    std::array<juce::TextButton,slotCount> names;
    std::array<juce::ToggleButton,slotCount> layers;
    std::array<juce::Slider,slotCount> shifts,levels;
    juce::TextButton clear{jp("すべての重ねを解除")};
    void label(juce::Label& l,juce::String t,int size){l.setText(t,juce::dontSendNotification);l.setFont(uiFont((float)size));addAndMakeVisible(l);}
    void timerCallback() override {
        if(!isVisible())return;
        for(int i=0;i<slotCount;++i){auto& s=engine.slots[(size_t)i];names[(size_t)i].setButtonText(jp("音色 ")+juce::String(i+1)+" / "+(s.plugin?s.name:jp("未設定")));
            names[(size_t)i].setToggleState(engine.selected.load()==i,juce::dontSendNotification);
            layers[(size_t)i].setToggleState(s.layer.load(),juce::dontSendNotification);layers[(size_t)i].setEnabled(s.plugin!=nullptr);
            if(!shifts[(size_t)i].isMouseButtonDown())shifts[(size_t)i].setValue(s.transpose.load(),juce::dontSendNotification);
            if(!levels[(size_t)i].isMouseButtonDown())levels[(size_t)i].setValue(s.gain.load()*100,juce::dontSendNotification);
        }
    }
public:
    MixerPanel(Engine& e,std::function<void()> change):engine(e),changed(std::move(change)){
        label(title,jp("音源を重ねて、厚みやハーモニーを作る"),22);
        label(hint,jp("選択中の音色に、チェックした音色を重ねます。移調は次に押す鍵盤から反映されます。+12で1オクターブ上。"),15);
        label(nameHeader,jp("音色（クリックして選択）"),14);label(layerHeader,jp("常に重ねる"),14);label(transposeHeader,jp("移調 / 半音"),14);label(gainHeader,jp("この音色の音量"),14);
        for(int i=0;i<slotCount;++i){auto n=(size_t)i;
            addAndMakeVisible(names[n]);names[n].onClick=[&,i]{engine.control(Select,i);};
            addAndMakeVisible(layers[n]);layers[n].setButtonText(jp("重ねる"));layers[n].onClick=[&,i]{engine.control(SetLayer,i,layers[(size_t)i].getToggleState()?1.0f:0.0f);changed();};
            for(auto* s:{&shifts[n],&levels[n]}){addAndMakeVisible(s);s->setSliderStyle(juce::Slider::LinearHorizontal);s->setTextBoxStyle(juce::Slider::TextBoxRight,false,68,30);}
            shifts[n].setRange(-24,24,1);enableReset(shifts[n],0);enableReset(levels[n],100);shifts[n].textFromValueFunction=[](double v){return (v>0?juce::String("+"):juce::String())+juce::String((int)v);};
            shifts[n].onValueChange=[&,i]{engine.control(SetTranspose,i,(float)shifts[(size_t)i].getValue());changed();};
            levels[n].setRange(0,100,1);levels[n].setValue(100,juce::dontSendNotification);levels[n].setTextValueSuffix(" %");
            levels[n].onValueChange=[&,i]{engine.control(SlotGain,i,(float)levels[(size_t)i].getValue()/100);changed();};
        }
        addAndMakeVisible(clear);clear.onClick=[&]{for(int i=0;i<slotCount;++i)engine.control(SetLayer,i,0);changed();};startTimerHz(12);
    }
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff101419));g.setColour(juce::Colour(0xff1b222b));g.fillRoundedRectangle(0,0,(float)getWidth(),(float)getHeight(),12);}
    void resized() override {
        int w=getWidth();title.setBounds(20,12,w-40,34);hint.setBounds(20,49,w-40,44);
        nameHeader.setBounds(20,102,340,24);layerHeader.setBounds(365,102,110,24);transposeHeader.setBounds(480,102,280,24);gainHeader.setBounds(775,102,w-795,24);
        for(int i=0;i<slotCount;++i){int y=140+i*49;names[(size_t)i].setBounds(20,y,335,36);layers[(size_t)i].setBounds(365,y,100,36);shifts[(size_t)i].setBounds(478,y,280,36);levels[(size_t)i].setBounds(775,y,w-795,36);}
        clear.setBounds(20,545,240,34);
    }
};
class EffectsPanel final : public juce::Component, private juce::Timer {
    Engine& engine;
    std::function<bool()> canEdit;
    std::function<void(const std::function<void()>&)> suspend;
    std::function<void()> changed;
    std::function<void(juce::String)> inform;
    std::unique_ptr<juce::FileChooser> chooser;
    std::array<juce::TextButton,clipCount> pads;
    std::array<bool,clipCount> heldKeys{};
    int selected=0;
    juce::Label title,hint,editLabel,volumeLabel,detail;
    juce::TextButton load{jp("効果音ファイルを選ぶ")},demo{jp("内蔵チャイムを入れる")},remove{jp("外す")},stop{jp("この効果音を止める")},stopAll{jp("効果音をすべて止める")};
    juce::Slider volume;
    juce::ToggleButton loop{jp("繰り返し再生する")};
    static constexpr const char* keys="QWETYUIO";
    void refresh(){
        for(int i=0;i<clipCount;++i){auto& c=engine.clips[(size_t)i];bool ready=c.data.audio.getNumSamples()>0;
            pads[(size_t)i].setButtonText(juce::String::charToString(keys[i])+"   "+(ready?c.data.name:jp("効果音を追加"))+"\n"+(c.playing?jp("再生中"):ready?jp("押すと最初から再生"):jp("選んで、下のボタンで追加")));
            pads[(size_t)i].setToggleState(selected==i,juce::dontSendNotification);}
        auto& c=engine.clips[(size_t)selected];editLabel.setText(jp("効果音 ")+juce::String(selected+1)+jp(" の設定"),juce::dontSendNotification);
        if(!volume.isMouseButtonDown())volume.setValue(c.gain.load()*100,juce::dontSendNotification);loop.setToggleState(c.loop,juce::dontSendNotification);
        detail.setText(c.data.audio.getNumSamples()>0?c.data.name+" / "+juce::String(c.data.audio.getNumSamples()/c.data.sampleRate,2)+jp(" 秒")+" / "+juce::String(c.data.sampleRate/1000,1)+" kHz":jp("WAV・AIFF・FLAC（120秒以内）を事前に読み込みます。"),juce::dontSendNotification);
    }
    void timerCallback() override {if(isVisible())refresh();}
    bool install(ClipData&& data){
        int64_t frames=data.audio.getNumSamples();for(int i=0;i<clipCount;++i)if(i!=selected)frames+=engine.clips[(size_t)i].data.audio.getNumSamples();
        if(frames*8>128*1024*1024){inform(jp("効果音の合計が128MBを超えます。短いファイルを使うか不要な効果音を外してください。"));return false;}
        suspend([&]{engine.clips[(size_t)selected].data=std::move(data);});changed();refresh();return true;
    }
public:
    EffectsPanel(Engine& e,std::function<bool()> editable,std::function<void(const std::function<void()>&)> pause,std::function<void()> change,std::function<void(juce::String)> message)
      :engine(e),canEdit(std::move(editable)),suspend(std::move(pause)),changed(std::move(change)),inform(std::move(message)){
        auto label=[&](juce::Label& l,juce::String t,int n){l.setText(t,juce::dontSendNotification);l.setFont(uiFont((float)n));addAndMakeVisible(l);};
        label(title,jp("鍵盤・パッド・PCキーで効果音を鳴らす"),22);
        label(hint,jp("画面のボタンで試聴できます。演奏モード中は Q W E T Y U I O キーでも再生。MIDI入力は「割り当て」で登録します。"),15);
        label(editLabel,"",16);label(volumeLabel,jp("効果音の音量"),15);label(detail,"",14);
        for(int i=0;i<clipCount;++i){addAndMakeVisible(pads[(size_t)i]);pads[(size_t)i].setWantsKeyboardFocus(false);pads[(size_t)i].onClick=[&,i]{selected=i;trigger(i);refresh();};}
        for(auto* b:{&load,&demo,&remove,&stop,&stopAll})addAndMakeVisible(b);
        load.onClick=[&]{if(!canEdit())return;const int target=selected;chooser=std::make_unique<juce::FileChooser>(jp("効果音ファイルを選んでください"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),"*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<EffectsPanel>(this),target](const juce::FileChooser& fc){if(!safe||!fc.getResult().existsAsFile()||!safe->canEdit())return;
                ClipData data;juce::String error;if(!readClip(fc.getResult(),data,error)){safe->inform(error);return;}safe->selected=target;if(safe->install(std::move(data)))safe->inform(jp("効果音を追加しました。上のボタンで再生できます。"));});};
        demo.onClick=[&]{if(canEdit())install(makeDemoClip(selected));};
        remove.onClick=[&]{if(canEdit()){install({});inform(jp("選んだ効果音を外しました。"));}};
        stop.onClick=[&]{engine.control(ClipStop,selected);};stopAll.onClick=[&]{engine.control(ClipStopAll);};
        addAndMakeVisible(volume);volume.setRange(0,100,1);enableReset(volume,80);volume.setTextValueSuffix(" %");volume.setSliderStyle(juce::Slider::LinearHorizontal);volume.setTextBoxStyle(juce::Slider::TextBoxRight,false,70,28);
        volume.onValueChange=[&]{engine.clips[(size_t)selected].gain=(float)volume.getValue()/100;changed();};
        addAndMakeVisible(loop);loop.onClick=[&]{engine.clips[(size_t)selected].loop=loop.getToggleState();changed();};startTimerHz(12);
    }
    bool testReset(){volume.setValue(23,juce::dontSendNotification);simulateDoubleClick(volume);return volume.getValue()==80;}
    void setLocked(bool locked){load.setEnabled(!locked);demo.setEnabled(!locked);remove.setEnabled(!locked);}
    bool trigger(int index){if(index<0||index>=clipCount)return false;if(engine.clips[(size_t)index].data.audio.getNumSamples()==0){inform(jp("この枠に効果音を追加してください。"));return false;}engine.control(ClipPlay,index,1);return true;}
    bool triggerKey(const juce::KeyPress& k){if(k.getModifiers().isAnyModifierKeyDown())return false;auto ch=juce::CharacterFunctions::toUpperCase(k.getTextCharacter()!=0?k.getTextCharacter():(juce::juce_wchar)k.getKeyCode());for(int i=0;i<clipCount;++i)if(ch==(juce::juce_wchar)keys[i]){if(!heldKeys[(size_t)i]){heldKeys[(size_t)i]=true;trigger(i);}return true;}return false;}
    void releaseKeys(){for(int i=0;i<clipCount;++i)if(!juce::KeyPress::isKeyCurrentlyDown(keys[i]))heldKeys[(size_t)i]=false;}
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff101419));g.setColour(juce::Colour(0xff1b222b));g.fillRoundedRectangle(0,0,(float)getWidth(),(float)getHeight(),12);}
    void resized() override {int w=getWidth(),col=(w-64)/4;title.setBounds(20,12,w-40,34);hint.setBounds(20,51,w-40,48);
        for(int i=0;i<clipCount;++i)pads[(size_t)i].setBounds(20+(i%4)*(col+8),120+(i/4)*103,col,92);
        editLabel.setBounds(20,337,190,35);load.setBounds(215,337,212,36);demo.setBounds(439,337,200,36);remove.setBounds(651,337,80,36);
        detail.setBounds(20,386,w-40,30);volumeLabel.setBounds(20,435,170,30);volume.setBounds(190,435,360,32);loop.setBounds(600,435,300,32);
        stop.setBounds(20,495,250,40);stopAll.setBounds(285,495,270,40);
    }
};
}
