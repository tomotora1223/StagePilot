#pragma once
#include "Engine.h"
#include "Locale.h"

namespace stage {
// All callbacks run on the message thread. The owner detaches the audio callback
// before changing the chain structure; bypass itself is an atomic control.
class FxPanel final : public juce::Component, private juce::Timer {
    Engine& engine;
    int target=0;
    bool locked=false,refreshing=false;
    juce::Label title,flow,help;
    juce::ComboBox targets;
    juce::TextButton add{jp("＋ FXを追加")};
    struct Row final : juce::Component {
        FxPanel& owner;int index;
        juce::Label name,detail;
        juce::TextButton bypass{jp("有効")},open{jp("画面")},more{jp("…")};
        bool loaded=false;
        Row(FxPanel& panel,int at):owner(panel),index(at){
            for(auto* c:std::vector<juce::Component*>{&name,&detail,&bypass,&open,&more})addAndMakeVisible(c);
            name.setFont(uiFont(16,juce::Font::bold));name.setColour(juce::Label::textColourId,juce::Colour(0xffeef3f6));name.setMinimumHorizontalScale(1.0f);name.setBorderSize(juce::BorderSize<int>(0));detail.setBorderSize(juce::BorderSize<int>(0));
            detail.setFont(uiFont(12));detail.setColour(juce::Label::textColourId,juce::Colour(0xffa6b5c6));
            for(auto* button:{&bypass,&open,&more})button->setWantsKeyboardFocus(false);
            bypass.setTooltip(jp("このFXだけを有効・無効にします。音源は読み込み直しません。"));open.setTooltip(jp("プラグインの画面を開きます。"));more.setTooltip(jp("順番を変更・このFXを外す"));
            bypass.onClick=[this]{if(!loaded||!owner.onBypass)return;auto& node=owner.chain().nodes[(size_t)index];owner.onBypass(owner.target,index,!node.bypassed.load());owner.refresh();};
            open.onClick=[this]{if(loaded&&owner.onOpen)owner.onOpen(owner.target,index);};
            more.onClick=[this]{owner.showMenu(index,more);};
        }
        void resized()override{
            const int w=getWidth();name.setBounds(46,7,juce::jmax(20,w-300),34);detail.setBounds(46,41,juce::jmax(20,w-300),20);
            bypass.setBounds(w-238,16,76,36);open.setBounds(w-152,16,76,36);more.setBounds(w-66,16,46,36);
        }
        void paint(juce::Graphics& g)override{
            auto colour=owner.target==slotCount?juce::Colour(0xff72d6c2):instrumentColour(owner.target);
            g.setColour(juce::Colour(loaded?0xff1b242e:0xff151b22));g.fillRoundedRectangle(getLocalBounds().toFloat(),8);
            g.setColour(colour.withAlpha(loaded?.9f:.25f));g.fillRoundedRectangle(12,12,3,(float)getHeight()-24,1.5f);
            g.setFont(uiFont(15));g.setColour(juce::Colour(0xffa6b5c6));g.drawText(juce::String(index+1),20,12,20,40,juce::Justification::centred);
        }
    };
    std::array<std::unique_ptr<Row>,fxCapacity> rows;
    FxChain& chain(){return target==slotCount?engine.masterFx:engine.slots[(size_t)target].fx;}
    bool sourcePresent()const{return target==slotCount||engine.slots[(size_t)target].plugin!=nullptr;}
    juce::String milliseconds(int samples)const{return juce::String(samples*1000.0/juce::jmax(1.0,engine.rate.load()),1)+" ms";}
    void timerCallback()override{if(isShowing())refresh();}
    void showMenu(int index,juce::Component& anchor){
        if(locked||index<0||index>=fxCapacity||!chain().nodes[(size_t)index].plugin)return;
        juce::PopupMenu menu;menu.addItem(1,jp("上へ移動"),index>0);menu.addItem(2,jp("下へ移動"),index+1<chain().count());menu.addSeparator();menu.addItem(3,jp("このFXを外す"));
        int selectedTarget=target;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor),[safe=juce::Component::SafePointer<FxPanel>(this),selectedTarget,index](int result){
            if(!safe||safe->locked||safe->target!=selectedTarget)return;
            if((result==1||result==2)&&safe->onMove)safe->onMove(selectedTarget,index,result==1?-1:1);
            else if(result==3&&safe->onRemove)safe->onRemove(selectedTarget,index);
            safe->refresh();
        });
    }
    static bool inside(juce::Rectangle<int> outer,juce::Rectangle<int> inner){return inner.getWidth()>0&&inner.getHeight()>0&&outer.contains(inner);}
public:
    std::function<void(int)> onAdd;
    std::function<void(int,int)> onOpen,onRemove;
    std::function<void(int,int,int)> onMove;
    std::function<void(int,int,bool)> onBypass;
    explicit FxPanel(Engine& e,int initialTarget=0):engine(e),target(juce::jlimit(0,slotCount,initialTarget)){
        for(auto* c:std::vector<juce::Component*>{&title,&targets,&add,&flow,&help})addAndMakeVisible(c);
        title.setText(jp("音にFXをかける"),juce::dontSendNotification);title.setFont(uiFont(24,juce::Font::bold));title.setColour(juce::Label::textColourId,juce::Colour(0xffeef3f6));
        flow.setFont(uiFont(14));flow.setColour(juce::Label::textColourId,juce::Colour(0xffb9c8d6));help.setFont(uiFont(14));help.setColour(juce::Label::textColourId,juce::Colour(0xffb9c8d6));help.setMinimumHorizontalScale(1.0f);
        for(int i=0;i<slotCount;++i)targets.addItem(jp("音源 ")+juce::String(i+1),i+1);targets.addItem(jp("全体出力"),slotCount+1);
        targets.onChange=[this]{if(refreshing)return;target=juce::jlimit(0,slotCount,targets.getSelectedId()-1);refresh();};
        targets.setTooltip(jp("FXをかける音源、または全体出力を選びます。"));add.onClick=[this]{if(!locked&&sourcePresent()&&chain().count()<fxCapacity&&onAdd)onAdd(target);};
        for(int i=0;i<fxCapacity;++i){rows[(size_t)i]=std::make_unique<Row>(*this,i);addAndMakeVisible(*rows[(size_t)i]);}
        setSize(760,520);refresh();startTimerHz(3);
    }
    void setLocked(bool value){locked=value;refresh();}
    void setTarget(int value){target=juce::jlimit(0,slotCount,value);refresh();}
    int selectedTarget()const{return target;}
    void refresh(){
        refreshing=true;for(int i=0;i<slotCount;++i){auto& slot=engine.slots[(size_t)i];targets.changeItemText(i+1,jp("音源 ")+juce::String(i+1)+"  "+(slot.plugin?slot.name:jp("未設定")));}targets.setSelectedId(target+1,juce::dontSendNotification);refreshing=false;
        auto& effects=chain();int count=effects.count();bool present=sourcePresent();add.setEnabled(!locked&&present&&count<fxCapacity);add.setButtonText(count>=fxCapacity?jp("FXは4つまで"):jp("＋ FXを追加"));
        flow.setText(!present?jp("音源を読み込んでから、FXを追加できます。"):jp("上から順に処理　")+juce::String(count)+jp(" / 4　追加の遅れ ")+milliseconds(effects.latencySamples()),juce::dontSendNotification);
        for(int i=0;i<fxCapacity;++i){auto& row=*rows[(size_t)i];auto& node=effects.nodes[(size_t)i];row.loaded=node.plugin!=nullptr;bool bypassed=node.bypassed.load();
            auto name=row.loaded?(node.description.name.isNotEmpty()?node.description.name:node.plugin->getName()):jp("FXなし");row.name.setText(name,juce::dontSendNotification);row.name.setTooltip(name);row.detail.setText(row.loaded?(bypassed?jp("無効 / 元の音を通します"):jp("追加の遅れ ")+milliseconds(node.plugin->getLatencySamples())):i==count&&present?jp("上の「FXを追加」から始めます"):juce::String(),juce::dontSendNotification);
            row.bypass.setButtonText(bypassed?jp("無効"):jp("有効"));row.bypass.setToggleState(row.loaded&&!bypassed,juce::dontSendNotification);row.bypass.setEnabled(row.loaded);row.open.setEnabled(row.loaded);row.more.setEnabled(row.loaded&&!locked);row.repaint();}
        help.setText((target==slotCount?jp("全体出力：音源・効果音・打ち込みをまとめた音にかかります。"):jp("この音源を使うすべての音色に共通。切替時にFXを再読込しません。"))+"\n"+(locked?jp("演奏モード：有効・無効とプラグイン画面を操作できます。"):jp("追加・削除・並べ替えは設定モードで行います。"))+"\n"+jp("FXの遅れは自動補正しません。遅れの少ないFXが演奏向きです。"),juce::dontSendNotification);
    }
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff101419));}
    void resized()override{
        int w=getWidth();title.setBounds(22,15,w-44,36);targets.setBounds(24,62,w-224,36);add.setBounds(w-184,62,160,36);flow.setBounds(24,106,w-48,28);
        int rowHeight=juce::jmax(58,juce::jmin(68,(getHeight()-240)/fxCapacity));for(int i=0;i<fxCapacity;++i)rows[(size_t)i]->setBounds(24,142+i*(rowHeight+6),w-48,rowHeight);
        int footer=142+fxCapacity*(rowHeight+6)+3;help.setBounds(24,footer,w-48,juce::jmax(66,getHeight()-footer-12));
    }
    // These checks exercise real bounds and control state at the supported size.
    bool testLayoutChecks(){
        bool ok=true;auto before=getBounds();for(auto size:std::array<juce::Point<int>,2>{{{760,520},{920,620}}}){setSize(size.x,size.y);refresh();auto bounds=getLocalBounds();ok=ok&&inside(bounds,title.getBounds())&&inside(bounds,targets.getBounds())&&inside(bounds,add.getBounds())&&inside(bounds,help.getBounds())&&!targets.getBounds().intersects(add.getBounds());
            for(auto& row:rows){auto b=row->getLocalBounds();ok=ok&&inside(bounds,row->getBounds())&&inside(b,row->name.getBounds())&&inside(b,row->detail.getBounds())&&inside(b,row->bypass.getBounds())&&inside(b,row->open.getBounds())&&inside(b,row->more.getBounds())&&!row->name.getBounds().intersects(row->bypass.getBounds());}}
        bool wasLocked=locked;setLocked(true);ok=ok&&!add.isEnabled();for(auto& row:rows)ok=ok&&!row->more.isEnabled();setLocked(wasLocked);setBounds(before);return ok;
    }
};
}
