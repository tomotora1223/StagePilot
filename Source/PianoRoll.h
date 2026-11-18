#pragma once
#include "Extras.h"

namespace stage {
class PianoRollPanel final : public juce::Component,private juce::Timer {
    using String=juce::String;
    Engine& engine;
    std::function<bool()> editable;
    std::function<void()> changed;
    std::function<void(String)> inform;
    std::function<void(int)> copySound;
    std::function<void()> openSound;
    std::function<void(int)> exportSound;
    Pattern model;
    bool locked=false;double drawnPosition=-2;std::array<unsigned char,128> drawnKeys{};
    int selected=-1,baseNote=60,newLength=2,newVelocity=100;
    int keyboardBase=24,keyboardCount=73;
    std::vector<Pattern> undoHistory;
    juce::Label title,hint,soundName,tempoLabel,lengthLabel,velocityLabel,positionLabel,keyHelp;
    juce::ComboBox source,bars,octave,destination,keyboardSpan;
    juce::TextButton copy{jp("この音色を使う")},editor{jp("音色を調整")},play{jp("▶ 再生")},stop{jp("■ 停止")};
    juce::TextButton remove{jp("選んだ音を削除")},undo{jp("元に戻す")},demo{jp("お手本を入れる")},save{jp("WAV保存・効果音に登録")};
    juce::TextButton clear{jp("すべて消す")};
    juce::ToggleButton loop{jp("くり返す")};
    juce::Slider tempo,length,velocity;
    void label(juce::Label& l,String text,float size=14){addAndMakeVisible(l);l.setText(text,juce::dontSendNotification);l.setFont(uiFont(size));}
    void slider(juce::Slider& s,int min,int max,int value){addAndMakeVisible(s);s.setRange(min,max,1);enableReset(s,value);s.setValue(value,juce::dontSendNotification);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,50,24);}
    void preview(int pitch,int v){Event e;e.type=Event::SequencePreview;e.a=pitch;e.b=v;engine.post(e);}
    void remember(){if(undoHistory.size()>=32)undoHistory.erase(undoHistory.begin());undoHistory.push_back(model);}
    void sendSettings(){Event e;e.type=Event::SequenceSettings;e.a=model.bpm;e.b=model.bars;e.c=model.loop?1:0;engine.post(e);}
    void sendNote(int i){Event e;e.type=Event::SequenceEdit;e.a=i;e.note=model.notes[(size_t)i];engine.post(e);}
    void selectNote(int i){selected=i;if(i>=0){newLength=model.notes[(size_t)i].length;newVelocity=model.notes[(size_t)i].velocity;}length.setValue(newLength,juce::dontSendNotification);velocity.setValue(newVelocity,juce::dontSendNotification);grid.repaint();}
    bool canChange(){return !locked&&editable();}
    bool changeNote(int i,SequenceNote n,bool history=true){
        auto candidate=model;candidate.notes[(size_t)i]=n;
        if(!Sequencer::valid(candidate)){inform(jp("同じ高さの音は重ねず、空いている位置に置いてください。"));return false;}
        if(history)remember();model=candidate;sendNote(i);changed();selectNote(i);return true;
    }
    void restore(Pattern p){
        // Clear first so moving notes cannot temporarily overlap in the audio-thread model.
        for(int i=0;i<sequenceNoteCount;++i){Event e;e.type=Event::SequenceEdit;e.a=i;e.note.enabled=false;engine.post(e);}
        model=p;sendSettings();for(int i=0;i<sequenceNoteCount;++i)if(model.notes[(size_t)i].enabled)sendNote(i);
        syncControls();selected=-1;changed();grid.repaint();
    }
    void erase(){if(!canChange()||selected<0)return;auto n=model.notes[(size_t)selected];n.enabled=false;changeNote(selected,n);selected=-1;}
    void syncControls(){tempo.setValue(model.bpm,juce::dontSendNotification);bars.setSelectedId(model.bars,juce::dontSendNotification);loop.setToggleState(model.loop,juce::dontSendNotification);}
    class Grid final : public juce::Component {
        PianoRollPanel& p;
        int dragging=-1,anchorStep=0,anchorPitch=0;
        SequenceNote original;
        bool resize=false,recorded=false;
        float cell() const{return (getWidth()-66.0f)/(p.model.bars*16);}
        float row() const{return (getHeight()-24.0f)/24;}
        int step(float x) const{return juce::jlimit(0,p.model.bars*16-1,(int)((x-66)/cell()));}
        int pitch(float y) const{return p.baseNote+23-juce::jlimit(0,23,(int)((y-24)/row()));}
    public:
        Grid(PianoRollPanel& owner):p(owner){setWantsKeyboardFocus(true);setMouseCursor(juce::MouseCursor::CrosshairCursor);}
        juce::Rectangle<float> noteRect(const SequenceNote& n) const{return {66+n.start*cell(),24+(p.baseNote+23-n.pitch)*row(),n.length*cell(),row()};}
        int hit(juce::Point<float> point) const {for(int i=0;i<sequenceNoteCount;++i)if(p.model.notes[(size_t)i].enabled&&noteRect(p.model.notes[(size_t)i]).contains(point))return i;return -1;}
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xff141b23));g.setFont(uiFont(11));
            for(int r=0;r<24;++r){int n=p.baseNote+23-r;float y=24+r*row();bool black=juce::MidiMessage::isMidiNoteBlack(n);
                g.setColour(black?juce::Colour(0xff141b23):juce::Colour(0xff1d2732));g.fillRect(66.0f,y,getWidth()-66.0f,row());
                g.setColour(p.engine.sequence.lit[(size_t)n]?juce::Colour(0xff8ce8bd):p.engine.inputKeys[(size_t)n]?juce::Colour(0xff91bbff):(black?juce::Colour(0xff2b3543):juce::Colour(0xffd5dce2)));g.fillRect(0.0f,y,62.0f,row()-0.5f);
                g.setColour(black?juce::Colours::white:juce::Colour(0xff14202a));g.drawText(juce::MidiMessage::getMidiNoteName(n,true,true,4),4,(int)y,54,(int)row(),juce::Justification::centredRight);
            }
            for(int s=0;s<=p.model.bars*16;++s){float x=66+s*cell();g.setColour(s%16==0?juce::Colour(0xff738196):s%4==0?juce::Colour(0xff435163):juce::Colour(0xff293542));g.drawVerticalLine((int)x,24,(float)getHeight());
                if(s%4==0&&s<p.model.bars*16){g.setColour(juce::Colour(0xffbac5d2));g.drawText(s%16==0?String(s/16+1)+jp("小節"):String((s%16)/4+1)+jp("拍"),(int)x+3,0,(int)(cell()*4)-3,23,juce::Justification::centredLeft);}}
            for(int i=0;i<sequenceNoteCount;++i){auto n=p.model.notes[(size_t)i];if(!n.enabled||n.pitch<p.baseNote||n.pitch>=p.baseNote+24)continue;
                auto r=noteRect(n).reduced(1.0f,.8f);g.setColour(juce::Colour(0xff8ce8bd).withAlpha(.35f+.65f*n.velocity/127.0f));g.fillRoundedRectangle(r,2);
                if(i==p.selected){g.setColour(juce::Colours::white);g.drawRoundedRectangle(r,2,1.5f);}
                g.setColour(juce::Colour(0xff1c493b));g.fillRect(r.getRight()-3,r.getY()+2,1.0f,juce::jmax(1.0f,r.getHeight()-4));
            }
            if(p.engine.sequence.playing){g.setColour(juce::Colour(0xffffd27c));g.drawLine(66+(float)p.engine.sequence.position.load()*cell(),24,66+(float)p.engine.sequence.position.load()*cell(),(float)getHeight(),2);}
        }
        void mouseDown(const juce::MouseEvent& e) override {
            if(e.y<24)return;grabKeyboardFocus();
            if(e.x<66){p.preview(pitch(e.position.y),p.newVelocity);return;}
            if(!p.canChange())return;
            auto i=hit(e.position);if(e.mods.isRightButtonDown()){if(i>=0){p.selectNote(i);p.erase();}return;}
            if(i<0){for(int n=0;n<sequenceNoteCount;++n)if(!p.model.notes[(size_t)n].enabled){i=n;break;}
                if(i<0){p.inform(jp("音は128個まで置けます。不要な音を削除してください。"));return;}
                int start=step(e.position.x);SequenceNote n{pitch(e.position.y),start,juce::jmin(p.newLength,p.model.bars*16-start),p.newVelocity,true};if(!p.changeNote(i,n))return;
            }
            p.selectNote(i);p.engine.control(SequenceStop);p.preview(p.model.notes[(size_t)i].pitch,p.model.notes[(size_t)i].velocity);
            dragging=i;original=p.model.notes[(size_t)i];anchorStep=step(e.position.x);anchorPitch=pitch(e.position.y);resize=e.position.x>noteRect(original).getRight()-juce::jmin(7.0f,cell()*.35f);recorded=false;
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            if(dragging<0||!p.canChange())return;auto n=original;
            if(resize)n.length=juce::jlimit(1,p.model.bars*16-n.start,step(e.position.x)-n.start+1);
            else{n.start=juce::jlimit(0,p.model.bars*16-n.length,original.start+step(e.position.x)-anchorStep);n.pitch=juce::jlimit(0,127,original.pitch+pitch(e.position.y)-anchorPitch);}
            if(p.changeNote(dragging,n,!recorded))recorded=true;
        }
        void mouseUp(const juce::MouseEvent&) override {dragging=-1;}
        bool keyPressed(const juce::KeyPress& k) override {
            if(k.getKeyCode()==juce::KeyPress::deleteKey||k.getKeyCode()==juce::KeyPress::backspaceKey){p.erase();return true;}
            if(k.getKeyCode()==juce::KeyPress::spaceKey){p.togglePlay();return true;}
            return false;
        }
    } grid{*this};
    class Keys final : public juce::Component {
        PianoRollPanel& p;
        juce::Rectangle<float> key(int pitch) const {int white=0;for(int n=p.keyboardBase;n<pitch;++n)if(!juce::MidiMessage::isMidiNoteBlack(n))++white;int count=0;for(int n=p.keyboardBase;n<p.keyboardBase+p.keyboardCount;++n)if(!juce::MidiMessage::isMidiNoteBlack(n))++count;float w=(float)getWidth()/count;bool black=juce::MidiMessage::isMidiNoteBlack(pitch);return {white*w-(black?w*.31f:0),0,black?w*.62f:w-1,black?getHeight()*.62f:(float)getHeight()};}
    public:
        Keys(PianoRollPanel& owner):p(owner){}
        void paint(juce::Graphics& g) override {for(int pass=0;pass<2;++pass)for(int n=p.keyboardBase;n<p.keyboardBase+p.keyboardCount;++n){bool black=juce::MidiMessage::isMidiNoteBlack(n);if(black!=(pass==1))continue;g.setColour(p.engine.sequence.lit[(size_t)n]?juce::Colour(0xff8ce8bd):p.engine.inputKeys[(size_t)n]?juce::Colour(0xff91bbff):black?juce::Colour(0xff202a37):juce::Colour(0xffdce4ea));auto r=key(n);g.fillRoundedRectangle(r,2);if(!black){g.setColour(juce::Colour(0xff344255));g.setFont(uiFont(11));g.drawText(juce::MidiMessage::getMidiNoteName(n,true,true,4),r.withTop(r.getBottom()-17),juce::Justification::centred);}}}
        void mouseDown(const juce::MouseEvent& e) override {for(int pass=1;pass>=0;--pass)for(int n=p.keyboardBase;n<p.keyboardBase+p.keyboardCount;++n)if(juce::MidiMessage::isMidiNoteBlack(n)==(pass==1)&&key(n).contains(e.position)){p.preview(n,p.newVelocity);if(n<p.baseNote||n>=p.baseNote+24){p.octave.setSelectedId(juce::jlimit(1,8,n/12));}return;}}
    } keys{*this};
    void timerCallback() override {
        if(!isVisible())return;
        play.setButtonText(engine.sequence.playing?jp("先頭から再生"):jp("▶ 再生"));
        positionLabel.setText(engine.sequence.playing?jp("再生中  ")+String(1+(int)engine.sequence.position.load()/16)+jp("小節"):jp("停止中 ・ 1マス＝16分音符"),juce::dontSendNotification);
        soundName.setText(jp("使用中：")+engine.sequence.name,juce::dontSendNotification);
        for(int i=0;i<slotCount;++i)source.changeItemText(i+2,jp("音色 ")+String(i+1)+" / "+(engine.slots[(size_t)i].plugin?engine.slots[(size_t)i].name:jp("未設定")));
        double position=engine.sequence.playing?engine.sequence.position.load():-1;bool visualChange=position!=drawnPosition;drawnPosition=position;for(int n=0;n<128;++n){unsigned char state=(engine.sequence.lit[(size_t)n]?1:0)|(engine.inputKeys[(size_t)n]?2:0);visualChange|=state!=drawnKeys[(size_t)n];drawnKeys[(size_t)n]=state;}if(visualChange){grid.repaint();keys.repaint();}undo.setEnabled(!locked&&!undoHistory.empty());remove.setEnabled(!locked&&selected>=0);
    }
public:
    PianoRollPanel(Engine& e,std::function<bool()> canEdit,std::function<void()> dirty,std::function<void(String)> status,std::function<void(int)> clone,std::function<void()> show,std::function<void(int)> bounce)
        :engine(e),editable(std::move(canEdit)),changed(std::move(dirty)),inform(std::move(status)),copySound(std::move(clone)),openSound(std::move(show)),exportSound(std::move(bounce)),model(e.sequence.pattern){
        label(title,jp("打ち込みでフレーズ・効果音を作る"),21);
        label(hint,jp("空いたマスで音を追加。ドラッグで移動、右端で長さ変更。停止中は鍵盤をクリックして試聴できます。"),14);
        label(soundName,{},13);label(tempoLabel,jp("速さ"));label(lengthLabel,jp("長さ（マス）"));label(velocityLabel,jp("音の強さ"));label(positionLabel,{},12);
        label(keyHelp,jp("鍵盤の点灯：緑＝打ち込み  青＝MIDI入力"),12);
        for(auto* c:std::vector<juce::Component*>{&source,&bars,&octave,&destination,&keyboardSpan,&copy,&editor,&play,&stop,&remove,&undo,&demo,&clear,&save,&loop,&grid,&keys})addAndMakeVisible(c);
        keyboardSpan.addItem(jp("横の鍵盤：2オクターブ"),1);keyboardSpan.addItem(jp("横の鍵盤：4オクターブ"),2);keyboardSpan.addItem(jp("横の鍵盤：6オクターブ"),3);keyboardSpan.addItem(jp("横の鍵盤：88鍵"),4);keyboardSpan.setSelectedId(3);
        keyboardSpan.onChange=[this]{int id=keyboardSpan.getSelectedId();keyboardBase=id==4?21:id==1?60:id==2?36:24;keyboardCount=id==4?88:id==1?25:id==2?49:73;keys.repaint();};
        source.addItem(jp("内蔵シンセ"),1);for(int i=0;i<slotCount;++i)source.addItem(jp("音色 ")+String(i+1),i+2);source.setSelectedId(1);
        copy.onClick=[this]{if(canChange())copySound(source.getSelectedId()-2);};editor.onClick=[this]{if(canChange())openSound();};
        play.onClick=[this]{engine.control(SequencePlay);};stop.onClick=[this]{engine.control(SequenceStop);};
        slider(tempo,40,240,120);tempo.setTextValueSuffix(" BPM");tempo.setTextBoxStyle(juce::Slider::TextBoxRight,false,76,24);
        tempo.onDragStart=[this]{remember();};tempo.onValueChange=[this]{if(canChange()){if(!tempo.isMouseButtonDown())remember();model.bpm=(int)tempo.getValue();sendSettings();changed();}};
        for(int i=1;i<=4;++i)bars.addItem(String(i)+jp("小節"),i);bars.setSelectedId(2);
        bars.onChange=[this]{if(!canChange())return;remember();model.bars=bars.getSelectedId();for(auto& n:model.notes)if(n.enabled){if(n.start>=model.bars*16)n.enabled=false;else n.length=juce::jmin(n.length,model.bars*16-n.start);}sendSettings();changed();selectNote(-1);grid.repaint();};
        loop.setToggleState(true,juce::dontSendNotification);loop.onClick=[this]{if(canChange()){remember();model.loop=loop.getToggleState();sendSettings();changed();}};
        for(int i=0;i<8;++i)octave.addItem(jp("表示 C")+String(i)+" – B"+String(i+1),i+1);octave.setSelectedId(5);
        octave.onChange=[this]{baseNote=12*octave.getSelectedId();grid.repaint();keys.repaint();};
        slider(length,1,64,newLength);slider(velocity,1,127,newVelocity);
        length.onDragStart=[this]{remember();};velocity.onDragStart=[this]{remember();};
        length.onValueChange=[this]{if(!canChange())return;if(!length.isMouseButtonDown())remember();newLength=(int)length.getValue();if(selected>=0){auto n=model.notes[(size_t)selected];n.length=juce::jmin(newLength,model.bars*16-n.start);changeNote(selected,n,false);}};
        velocity.onValueChange=[this]{if(!canChange())return;if(!velocity.isMouseButtonDown())remember();newVelocity=(int)velocity.getValue();if(selected>=0){auto n=model.notes[(size_t)selected];n.velocity=newVelocity;changeNote(selected,n,false);}};
        remove.onClick=[this]{erase();};undo.onClick=[this]{if(canChange()&&!undoHistory.empty()){auto p=undoHistory.back();undoHistory.pop_back();restore(p);}};
        clear.onClick=[this]{if(canChange()){remember();auto p=model;p.notes.fill({});restore(p);}};
        demo.onClick=[this]{if(canChange()){remember();Pattern p;p.bars=2;p.notes[0]={60,0,2,100,true};p.notes[1]={64,4,2,86,true};p.notes[2]={67,8,4,100,true};p.notes[3]={72,16,8,115,true};restore(p);inform(jp("お手本を入れました。「再生」で聴けます。「元に戻す」で以前の打ち込みへ戻せます。"));}};
        for(int i=0;i<clipCount;++i)destination.addItem(jp("効果音 ")+String(i+1),i+1);destination.setSelectedId(1);
        save.onClick=[this]{if(canChange())exportSound(destination.getSelectedId()-1);};
        save.setTooltip(jp("1回分をステレオWAVに保存し、左で選んだ効果音枠へ登録します。余韻は3秒です。"));
        for(auto* b:{&copy,&editor,&play,&stop,&remove,&undo,&demo,&clear,&save})b->setWantsKeyboardFocus(false);
        play.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff286448));save.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff286448));
        startTimerHz(24);
    }
    void sync(){model=engine.sequence.pattern;undoHistory.clear();selectNote(-1);syncControls();grid.repaint();}
    const Pattern& getPattern() const {return model;}
    void setLocked(bool value){locked=value;for(auto* c:std::vector<juce::Component*>{&copy,&editor,&tempo,&bars,&loop,&length,&velocity,&remove,&undo,&demo,&clear,&save})c->setEnabled(!value);}
    void togglePlay(){engine.control(engine.sequence.playing?SequenceStop:SequencePlay);}
    void paint(juce::Graphics& g) override {g.setColour(juce::Colour(0xff1b222b));g.fillRoundedRectangle(getLocalBounds().toFloat(),12);}
    void resized() override {
        int w=getWidth();title.setBounds(14,0,w-290,30);keyboardSpan.setBounds(w-266,0,252,30);hint.setBounds(14,31,w-28,24);
        source.setBounds(14,62,240,32);copy.setBounds(264,62,144,32);editor.setBounds(418,62,120,32);soundName.setBounds(550,62,w-564,32);
        play.setBounds(14,104,116,32);stop.setBounds(138,104,90,32);tempoLabel.setBounds(242,104,38,32);tempo.setBounds(278,104,180,32);bars.setBounds(474,104,86,32);loop.setBounds(570,104,118,32);positionLabel.setBounds(700,104,w-714,32);
        grid.setBounds(14,146,w-28,292);keys.setBounds(14,447,w-28,45);
        octave.setBounds(14,503,130,30);lengthLabel.setBounds(158,503,96,30);length.setBounds(252,503,145,30);velocityLabel.setBounds(410,503,80,30);velocity.setBounds(490,503,150,30);remove.setBounds(654,503,154,30);undo.setBounds(820,503,118,30);
        demo.setBounds(14,546,148,32);clear.setBounds(174,546,110,32);keyHelp.setBounds(296,546,w-742,32);destination.setBounds(w-434,546,132,32);save.setBounds(w-290,546,276,32);
    }
    // App-owned interaction tests use the actual editor handlers, without sending audio to a device.
    bool testResets(){auto old=model;tempo.setValue(93,juce::dontSendNotification);length.setValue(7,juce::dontSendNotification);velocity.setValue(34,juce::dontSendNotification);simulateDoubleClick(tempo);simulateDoubleClick(length);simulateDoubleClick(velocity);bool ok=tempo.getValue()==120&&length.getValue()==2&&velocity.getValue()==100;restore(old);selectNote(-1);return ok;}
    bool testEditing(){
        sync();auto p=model;int oldLength=newLength,oldVelocity=newVelocity;model=Pattern{};restore(model);selectNote(-1);
        bool added=changeNote(0,{60,0,2,100,true});bool moved=changeNote(0,{64,3,4,70,true});
        bool overlapRejected=!changeNote(1,{64,4,2,100,true});erase();bool deleted=!model.notes[0].enabled;
        if(!undoHistory.empty()){auto previous=undoHistory.back();undoHistory.pop_back();restore(previous);}
        bool undone=model.notes[0].enabled&&model.notes[0].pitch==64;
        restore(Pattern{});auto point=grid.noteRect({60,2,2,100,true}).getCentre();
        auto event=[&](juce::Point<float> at,bool dragged=false){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,&grid,&grid,juce::Time::getCurrentTime(),point,juce::Time::getCurrentTime(),1,dragged);};
        grid.mouseDown(event(point));bool clickWorks=selected>=0;int index=selected;
        auto target=grid.noteRect({64,5,2,100,true}).getCentre();grid.mouseDrag(event(target,true));grid.mouseUp(event(target,true));
        bool dragWorks=index>=0&&model.notes[(size_t)index].pitch==64&&model.notes[(size_t)index].start==6;
        grid.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));bool deleteKeyWorks=index>=0&&!model.notes[(size_t)index].enabled;
        restore(p);newLength=oldLength;newVelocity=oldVelocity;selectNote(-1);
        return added&&moved&&overlapRejected&&deleted&&undone&&clickWorks&&dragWorks&&deleteKeyWorks;
    }
    bool testWideKeys(){
        keyboardSpan.setSelectedId(4,juce::sendNotificationSync);bool full=keyboardBase==21&&keyboardCount==88;
        auto point=juce::Point<float>((float)keys.getWidth()/104,40);
        auto e=juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,&keys,&keys,juce::Time::getCurrentTime(),point,juce::Time::getCurrentTime(),1,false);
        keys.mouseDown(e);bool follows=octave.getSelectedId()==1;
        octave.setSelectedId(5,juce::sendNotificationSync);keyboardSpan.setSelectedId(3,juce::sendNotificationSync);return full&&follows&&keyboardCount==73;
    }
};
}
