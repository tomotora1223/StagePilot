#pragma once
#include "Extras.h"
namespace stage {
inline juce::String noteName(int note){return juce::MidiMessage::getMidiNoteName(juce::jlimit(0,127,note),true,true,4);}
inline juce::String shiftNumber(int value){return (value>0?"+":"")+juce::String(value);}
inline juce::String shiftDescription(int value){if(value==0)return jp("移調なし");if(value%12==0)return juce::String(std::abs(value)/12)+jp(value>0?"オクターブ上":"オクターブ下");return juce::String(std::abs(value))+jp(value>0?"半音上":"半音下");}
inline juce::String instrumentDisplayName(juce::String name){return name.startsWith(jp("内蔵音源 / "))?name.substring(jp("内蔵音源 / ").length()):name;}
struct KeyboardLayer {int low=0,high=127;bool enabled=false;bool operator==(const KeyboardLayer&)const=default;};
using KeyboardLayers=std::array<KeyboardLayer,8>;
inline KeyboardLayers playingKeyboardLayers(const Engine& engine){KeyboardLayers result{};for(int i=0;i<8;++i){auto& s=engine.slots[(size_t)i];result[(size_t)i]={s.keyLow.load(),s.keyHigh.load(),s.plugin&&(engine.activeScene>=0?s.sceneEnabled.load():engine.selected==i||s.layer.load())};}return result;}
inline unsigned keyboardLayerMask(const KeyboardLayers& layers,int note){unsigned result=0;for(int i=0;i<8;++i){auto& p=layers[(size_t)i];if(p.enabled&&note>=p.low&&note<=p.high)result|=1u<<i;}return result;}
inline juce::Colour pressedKeyColour(bool midi){return juce::Colour(midi?0xffffca70:0xff72d6c2);}
// Range handles, surface overlays and hit testing share the same note geometry.
struct KeyGeometry {
    static constexpr int first=21,last=108;
    std::array<float,89> edges{};
    std::array<float,128> centres{},whiteStarts{};
    KeyGeometry(){int whites=0;for(int n=first;n<=last;++n){bool black=juce::MidiMessage::isMidiNoteBlack(n);whiteStarts[(size_t)n]=(float)whites/52;centres[(size_t)n]=(whites+(black?0.0f:.5f))/52;if(!black)++whites;}edges[0]=0;edges[88]=1;for(int n=first+1;n<=last;++n)edges[(size_t)(n-first)]=(centres[(size_t)n-1]+centres[(size_t)n])*.5f;}
    float edge(int note)const{return edges[(size_t)juce::jlimit(0,88,note-first)];}
    int hit(float fraction)const{for(int i=0;i<88;++i)if(fraction<edges[(size_t)i+1])return first+i;return last;}
    juce::Rectangle<float> keyBounds(int note,juce::Rectangle<float> area)const{auto keys=area.withTrimmedBottom(19);float white=keys.getWidth()/52;bool black=juce::MidiMessage::isMidiNoteBlack(note);return {keys.getX()+(black?centres[(size_t)note]*keys.getWidth()-white*.3f:whiteStarts[(size_t)note]*keys.getWidth()),keys.getY(),black?white*.6f:white-1,black?keys.getHeight()*.61f:keys.getHeight()};}
    int hitKey(juce::Point<float> p,juce::Rectangle<float> area)const{if(!area.withTrimmedBottom(19).contains(p))return -1;for(int pass=1;pass>=0;--pass)for(int n=first;n<=last;++n){bool black=juce::MidiMessage::isMidiNoteBlack(n);auto r=keyBounds(n,area);if(!black)r.setWidth(area.getWidth()/52);if(black==(pass==1)&&r.contains(p))return n;}return -1;}
    void keyboard(juce::Graphics& g,juce::Rectangle<float> area,const Engine& engine,const KeyboardLayers& layers,int localPreview=-1)const{
        auto keys=area.withTrimmedBottom(19);int layerCount=0;for(auto p:layers)layerCount+=p.enabled;
        int preview=localPreview>=0?localPreview:engine.previewKey.load();
        for(int pass=0;pass<2;++pass)for(int n=first;n<=last;++n){bool black=juce::MidiMessage::isMidiNoteBlack(n);if(black!=(pass==1))continue;auto r=keyBounds(n,area);auto base=juce::Colour(black?0xff0d1219:0xffe3eaf0);unsigned mask=keyboardLayerMask(layers,n);bool midi=engine.inputKeys[(size_t)n].load()!=0,pressed=midi||preview==n;
            if(mask&&!pressed){float red=0,green=0,blue=0;int count=0;for(int i=0;i<8;++i)if(mask&(1u<<i)){auto c=instrumentColour(i);red+=c.getFloatRed();green+=c.getFloatGreen();blue+=c.getFloatBlue();++count;}base=base.interpolatedWith(juce::Colour::fromFloatRGBA(red/count,green/count,blue/count,1),black?.20f:.14f);}
            g.setColour(pressed?pressedKeyColour(midi):base);g.fillRect(r);
            if(mask&&!pressed&&layerCount){float lane=juce::jmin(2.5f,r.getHeight()*.32f/layerCount);int rank=0;for(int i=0;i<8;++i)if(layers[(size_t)i].enabled){if(mask&(1u<<i)){g.setColour(instrumentColour(i).withAlpha(black?.55f:.38f));g.fillRect(r.getX()+.5f,r.getBottom()-4-(layerCount-rank)*lane,juce::jmax(.5f,r.getWidth()-1),juce::jmax(.8f,lane-.3f));}++rank;}}
            if(pressed){g.setColour(juce::Colours::white.withAlpha(.92f));g.drawRect(r.reduced(.45f),.9f);g.fillRoundedRectangle(r.getCentreX()-juce::jmin(2.0f,r.getWidth()*.25f),r.getBottom()-4,juce::jmin(4.0f,r.getWidth()*.5f),2.0f,1.0f);}
            if(!black&&(n%12==0||(n==first&&keys.getWidth()>=600))){g.setColour(juce::Colour(0xffc6d1de));g.setFont(uiFont(11));g.drawText(noteName(n),juce::jmin((int)r.getX(),(int)keys.getRight()-26),(int)keys.getBottom()+2,26,17,juce::Justification::centredLeft);}
        }
    }
};
class PlayableKeyboard final:public juce::Component,public juce::SettableTooltipClient {
    Engine& engine;KeyGeometry geometry;int held=-1,visualHeld=-1,observed=-1;std::array<bool,128> keys{};KeyboardLayers layers{};bool customLayers=false;
    void visualChanged(){repaint();if(onVisualChange)onVisualChange();}
    void press(juce::Point<float> point){int note=geometry.hitKey(point,getLocalBounds().toFloat());if(note==held)return;if(note<0){release();return;}held=visualHeld=note;if(onPreview)onPreview(note,100);else engine.preview(note,100);visualChanged();}
public:
    std::function<void(int,int)> onPreview;
    std::function<void()> onVisualChange;
    explicit PlayableKeyboard(Engine& e):engine(e){setWantsKeyboardFocus(false);setMouseCursor(juce::MouseCursor::PointingHandCursor);setTooltip(jp("押した鍵盤が光ります。緑は画面の試聴、黄色はMIDI入力。薄い色の帯は音源ごとの入力範囲です。"));}
    void release(){if(held<0)return;held=visualHeld=-1;engine.preview(-1,0);visualChanged();}
    void mouseDown(const juce::MouseEvent& e)override{if(e.mods.isLeftButtonDown())press(e.position);}
    void mouseDrag(const juce::MouseEvent& e)override{press(e.position);}
    void mouseUp(const juce::MouseEvent&)override{release();}
    void visibilityChanged()override{if(!isVisible())release();}
    void setLayers(KeyboardLayers value){customLayers=true;if(layers!=value){layers=value;visualChanged();}}
    int visualPreview()const{return visualHeld>=0?visualHeld:engine.previewKey.load();}
    juce::String pressedLabel()const{int preview=visualPreview(),count=0;juce::String names;for(int n=0;n<128;++n)if(engine.inputKeys[(size_t)n].load()!=0||n==preview){if(count<3)names+=(names.isEmpty()?juce::String():jp(" · "))+noteName(n);++count;}if(count>3)names+=jp(" ほか")+juce::String(count-3)+jp("音");return count?jp("押下中  ")+names:jp("押下なし");}
    juce::Colour readoutColour()const{for(auto& key:engine.inputKeys)if(key.load())return pressedKeyColour(true);return visualPreview()>=0?pressedKeyColour(false):juce::Colour(0xffa7b9ca);}
    void paint(juce::Graphics& g)override{geometry.keyboard(g,getLocalBounds().toFloat(),engine,layers,visualHeld);}
    // Mouse ownership outlives its immediate visual cue, so key-up is always sent.
    // After the first UI refresh, the engine's actual preview state drives the display.
    void refresh(){int preview=engine.previewKey.load();bool changed=observed!=preview||visualHeld>=0;visualHeld=-1;observed=preview;for(int n=0;n<128;++n){bool down=engine.inputKeys[(size_t)n]!=0;changed|=keys[(size_t)n]!=down;keys[(size_t)n]=down;}if(!customLayers){auto next=playingKeyboardLayers(engine);changed|=next!=layers;layers=next;}if(changed)visualChanged();}
    juce::Image testImage(){return createComponentSnapshot(getLocalBounds());}
    bool testHighlighted(int note,bool midi){auto picture=testImage();auto r=geometry.keyBounds(note,getLocalBounds().toFloat());auto pixel=picture.getPixelAt(juce::jlimit(0,getWidth()-1,(int)r.getCentreX()),(int)(r.getY()+r.getHeight()*.75f));auto colour=pressedKeyColour(midi);return std::abs(pixel.getFloatRed()-colour.getFloatRed())<.28f&&std::abs(pixel.getFloatGreen()-colour.getFloatGreen())<.28f&&std::abs(pixel.getFloatBlue()-colour.getFloatBlue())<.28f;}
    unsigned testLayerMask(int note)const{return keyboardLayerMask(layers,note);}
    void testPress(int note,bool blackTop=false,bool drag=false){auto b=getLocalBounds().toFloat();juce::Point<float> point{geometry.centres[(size_t)note]*b.getWidth(),blackTop?5.0f:(b.getHeight()-19)*.85f};juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys::leftButtonModifier,0,0,0,0,0,this,this,juce::Time::getCurrentTime(),point,juce::Time::getCurrentTime(),1,false);if(drag)mouseDrag(e);else mouseDown(e);}
};
}
