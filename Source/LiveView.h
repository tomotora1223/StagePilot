#pragma once
#include "KeyboardView.h"
namespace stage {
class LiveView final:public juce::Component{
    Engine& engine;KeyGeometry geometry;juce::String signature;std::array<bool,128> keys{};juce::uint32 nextStructure=0;
    std::array<juce::TextButton,8> pedals;
    struct RowList:juce::Component,juce::SettableTooltipClient {
        LiveView& owner;explicit RowList(LiveView& view):owner(view){}
        void paint(juce::Graphics& g)override{owner.paintRows(g);}
        void mouseDown(const juce::MouseEvent& e)override{auto list=owner.visibleSlots();int row=e.y/46;if(row>=0&&row<(int)list.size()&&owner.onFocus)owner.onFocus(list[(size_t)row]);}
        void mouseMove(const juce::MouseEvent& e)override{auto list=owner.visibleSlots();int row=e.y/46;setTooltip(row>=0&&row<(int)list.size()?owner.engine.slots[(size_t)list[(size_t)row]].name:juce::String());}
    } rowList{*this};
    juce::Viewport rowViewport;
    PlayableKeyboard audition{engine};
    bool active(int i)const{auto& s=engine.slots[(size_t)i];return s.plugin&&(engine.activeScene>=0?s.sceneEnabled.load():engine.selected==i||s.layer.load());}
    std::vector<int> visibleSlots()const{std::vector<int> result;for(int i=0;i<8;++i)if(active(i))result.push_back(i);for(int i=0;i<8;++i)if(!active(i)&&engine.slots[(size_t)i].tailActive&&engine.slots[(size_t)i].running)result.push_back(i);return result;}
    float mapLeft()const{return juce::jmin(380.0f,getWidth()*.54f);}
    float rowHeight(int)const{return 46.0f;}
    void layout(){for(auto& button:pedals)button.setVisible(false);auto list=visibleSlots();rowList.setSize(getWidth()-12,juce::jmax(rowViewport.getHeight(),(int)list.size()*46));for(size_t r=0;r<list.size();++r){int i=list[r];auto& s=engine.slots[(size_t)i];auto& b=pedals[(size_t)i];b.setVisible(true);b.setEnabled(active(i));b.setButtonText(!active(i)?jp("解除済み"):s.ignorePedal?jp("ペダルなし"):s.pedalDown?jp("踏んでいる"):jp("ペダルあり"));b.setToggleState(active(i)&&!s.ignorePedal,juce::dontSendNotification);b.setBounds((int)mapLeft()-106,(int)r*46+5,98,36);}rowList.repaint();}
public:
    std::function<void(int,bool)> onPedalChange;std::function<void(int)> onFocus;
    explicit LiveView(Engine& e):engine(e){addAndMakeVisible(audition);audition.onVisualChange=[this]{repaint(0,getHeight()-105,getWidth(),105);};addAndMakeVisible(rowViewport);rowViewport.setViewedComponent(&rowList,false);rowViewport.setScrollBarsShown(true,false);rowViewport.setScrollBarThickness(10);setInterceptsMouseClicks(true,true);for(int i=0;i<8;++i){auto& b=pedals[(size_t)i];rowList.addAndMakeVisible(b);b.setWantsKeyboardFocus(false);b.setTooltip(jp("この音源だけ、サステインペダルを使う・使わないを切り替えます。"));b.onClick=[this,i]{if(onPedalChange)onPedalChange(i,!engine.slots[(size_t)i].ignorePedal.load());};}}
    ~LiveView()override{rowViewport.setViewedComponent(nullptr,false);}
    bool refresh(bool force=false){bool changed=false;for(int n=0;n<128;++n){bool down=engine.inputKeys[(size_t)n]!=0;changed|=keys[(size_t)n]!=down;keys[(size_t)n]=down;}auto now=juce::Time::getMillisecondCounter();if(force||now>=nextStructure){nextStructure=now+200;juce::String next=juce::String(engine.activeScene.load())+":"+juce::String(engine.performanceTranspose.load())+":"+juce::String((int)engine.pedalDown.load());for(int i=0;i<8;++i){auto& s=engine.slots[(size_t)i];next+=s.name+juce::String(s.transpose.load())+":"+juce::String(s.keyLow.load())+":"+juce::String(s.keyHigh.load())+":"+juce::String(s.gain.load(),3)+":"+juce::String((int)s.layer.load())+":"+juce::String((int)s.sceneEnabled.load())+":"+juce::String((int)s.ignorePedal.load())+":"+juce::String((int)s.pedalDown.load())+":"+juce::String((int)s.tailActive.load())+":"+juce::String(s.tailRemaining.load(),1)+":"+juce::String((int)(s.plugin!=nullptr));}next+=juce::String(engine.selected.load());changed|=signature!=next;signature=next;if(changed)layout();}if(changed){repaint();return true;}return false;}
    void resized()override{bool compact=getHeight()<260;int height=compact?60:85,y=getHeight()-height-11,hud=y-28,top=compact?52:67;rowViewport.setBounds(0,top,getWidth(),juce::jmax(46,hud-7-top));layout();audition.setBounds(18,y,getWidth()-36,height);}
    void visibilityChanged()override{if(!isVisible())audition.release();}
    void testPressKey(int note,bool black=false,bool drag=false){audition.testPress(note,black,drag);}
    void releaseKey(){audition.release();}
    void refreshKeys(){audition.refresh();}
    bool testPreviewHighlight(int note){return audition.testHighlighted(note,false);}
    void paint(juce::Graphics& g)override{
        auto bounds=getLocalBounds().toFloat();g.setColour(juce::Colour(0xff1b242e));g.fillRoundedRectangle(bounds,12);
        bool compact=getHeight()<260;g.setFont(uiFont(compact?16:17,juce::Font::bold));g.setColour(juce::Colour(0xffeef3f6));g.drawText(jp("鍵盤と音源の状態"),18,compact?8:10,getWidth()-36,compact?23:26,juce::Justification::centredLeft);
        g.setFont(uiFont(compact?11:13));g.setColour(engine.pedalDown?juce::Colour(0xffffca70):juce::Colour(0xffafc0d1));g.drawText(jp("ペダル：")+jp(engine.pedalDown?"踏んでいる":"離している")+jp("  / 切替時は前の音源のペダルを解除"),18,compact?33:38,getWidth()-36,compact?16:23,juce::Justification::centredLeft);
        int hud=audition.getY()-28,left=(int)juce::jmin(350.0f,getWidth()*.48f);g.setColour(juce::Colour(0xffb2c0d1));g.setFont(uiFont(11));g.drawText(jp("薄い帯 = 音域 / 緑 = 試聴 / 黄 = MIDI"),18,hud,left-24,25,juce::Justification::centredLeft);g.setFont(uiFont(15,juce::Font::bold));g.setColour(audition.readoutColour());g.drawText(audition.pressedLabel(),left,hud,getWidth()-left-18,25,juce::Justification::centredLeft);
    }
    void paintRows(juce::Graphics& g){
        auto list=visibleSlots();const float left=mapLeft(),width=getWidth()-left-18;float row=rowHeight((int)list.size());
        if(list.empty()){g.setColour(juce::Colour(0xffaab8c8));g.setFont(uiFont(18));g.drawFittedText(jp("左の音色を選ぶと、ここに重ね方・移調・範囲が表示されます。"),20,76,getWidth()-40,90,juce::Justification::centred,3);}
        for(size_t r=0;r<list.size();++r){int i=list[r];auto& s=engine.slots[(size_t)i];bool current=active(i);float y=r*row;auto colour=instrumentColour(i);g.setColour(colour.withAlpha(current?1.0f:.5f));g.fillRoundedRectangle(18,y+4,4,juce::jmax(2.0f,row-8),2);g.setFont(uiFont(14,juce::Font::bold));g.drawText(juce::String(i+1)+"  "+instrumentDisplayName(s.name),30,(int)y,(int)left-143,(int)row/2+3,juce::Justification::centredLeft);
            g.setFont(uiFont(row<30?11:13));g.setColour(current?juce::Colour(0xffe0e8f0):juce::Colour(0xffb3a7a0));int shift=s.transpose.load(),global=engine.performanceTranspose.load();auto text=global!=0?jp("合計 ")+shiftNumber(shift+global)+jp("半音"):shift==0?jp("移調なし"):shiftNumber(shift)+jp("半音");text+=jp(" / 音量 ")+juce::String(juce::roundToInt(s.gain.load()*100))+"%";if(!current)text=jp("前の音・余韻 残り ")+juce::String(s.tailRemaining.load(),1)+jp("秒");g.drawText(text,30,(int)(y+row/2),(int)left-143,(int)row/2,juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff303e4e));g.fillRoundedRectangle(left,y+5,width,juce::jmax(2.0f,row-10),4);float x=left+geometry.edge(s.keyLow.load())*width,right=left+geometry.edge(s.keyHigh.load()+1)*width;if(s.keyHigh>=21&&s.keyLow<=108){g.setColour(colour.withAlpha(current?1.0f:.24f));g.fillRoundedRectangle(x,y+5,juce::jmax(3.0f,right-x),juce::jmax(2.0f,row-10),4);if(row>=33&&right-x>95){g.setColour(current?juce::Colour(0xff11201e):juce::Colour(0xffc0c7ce));g.drawText(current?noteName(s.keyLow)+" – "+noteName(s.keyHigh):jp("新しい入力は送りません"),(int)x+7,(int)y+5,(int)(right-x)-14,(int)row-10,juce::Justification::centredLeft);}}
        }
    }
    PlayableKeyboard& testKeyboard(){return audition;}
    bool testPedalButton(int slot){auto& b=pedals[(size_t)slot];if(!b.isVisible()||!b.isEnabled())return false;b.onClick();return true;}
    bool testReadableRows(int count){bool ok=rowList.getHeight()>=count*46&&rowViewport.getHeight()<count*46;for(auto& pedal:pedals)if(pedal.isVisible())ok=ok&&pedal.getHeight()>=32;return ok;}
};
}
