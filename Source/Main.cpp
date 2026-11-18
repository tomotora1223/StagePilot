#include "Engine.h"
#include "Locale.h"
#include "Extras.h"
#include "PianoRoll.h"
#include "SceneEditor.h"
#include "AudioStrip.h"
#include "PluginHosting.h"
#include "InstrumentBrowser.h"
#include "RemotePanel.h"
#include "CameraPanel.h"
#include "FxPanel.h"
#include <functional>
#include <process.h>

using namespace juce;
using namespace stage;
namespace stage {int runReuseTests(const juce::File&);}
namespace {
const Colour bg(0xff101419), panel(0xff1b222b), ink(0xffeef3f6), muted(0xff94a2b3), mint(0xff8ce8bd);
class Theme : public LookAndFeel_V4 {
public:
    Theme() {
        setDefaultSansSerifTypeface(UiTypefaces::get().regular);
        setColour(ResizableWindow::backgroundColourId,bg);
        setColour(Label::textColourId,ink);setColour(TextButton::buttonColourId,Colour(0xff2b3643));
        setColour(TextButton::textColourOffId,ink);setColour(TextButton::buttonOnColourId,Colour(0xff286448));
        setColour(ComboBox::backgroundColourId,panel);setColour(ComboBox::textColourId,ink);
        setColour(ComboBox::outlineColourId,Colour(0xff3c4857));setColour(PopupMenu::backgroundColourId,panel);
        setColour(PopupMenu::textColourId,ink);setColour(Slider::thumbColourId,mint);setColour(Slider::trackColourId,mint);
        setColour(TextEditor::backgroundColourId,panel);setColour(TextEditor::textColourId,ink);
        setColour(ListBox::backgroundColourId,panel);
    }
    Typeface::Ptr getTypefaceForFont(const Font& font) override {
        if(font.getTypefaceName()==Font::getDefaultSansSerifFontName()||font.getTypefaceName()=="Zen Kaku Gothic New"||font.getTypefaceName()=="Yu Gothic UI")return font.isBold()?UiTypefaces::get().bold:UiTypefaces::get().regular;
        return LookAndFeel_V4::getTypefaceForFont(font);
    }
    Font getTextButtonFont(TextButton&,int) override { return Font(uiFont(14)); }
    Font getComboBoxFont(ComboBox&) override { return Font(uiFont(14)); }
    Font getPopupMenuFont() override {return Font(uiFont(15));}
    Label* createSliderTextBox(Slider& slider)override{auto* label=LookAndFeel_V4::createSliderTextBox(slider);label->setFont(uiFont(13));return label;}
    void drawButtonBackground(Graphics& g,Button& b,const Colour& colour,bool hover,bool down)override{
        auto area=b.getLocalBounds().toFloat().reduced(.75f);auto fill=colour;
        if(hover)fill=fill.brighter(.1f);if(down)fill=fill.brighter(.18f);
        if(!b.isEnabled())fill=fill.withAlpha(.5f);g.setColour(fill);g.fillRoundedRectangle(area,6);
        g.setColour(b.getToggleState()?mint.withAlpha(.55f):Colour(0xff3c4857).withAlpha(.55f));g.drawRoundedRectangle(area,6,1);
        if(b.hasKeyboardFocus(true)){g.setColour(ink);g.drawRoundedRectangle(area.reduced(2),4,1.5f);}
    }
    void drawButtonText(Graphics& g,TextButton& b,bool,bool)override{
        bool pad=(bool)b.getProperties()["stagePad"];
        g.setColour(b.findColour(b.getToggleState()?TextButton::textColourOnId:TextButton::textColourOffId).withAlpha(b.isEnabled()?1.0f:.5f));
        auto text=b.getButtonText();auto area=b.getLocalBounds().reduced(pad?12:b.getWidth()<50?3:8,pad?7:3);
        if(pad){auto title=area.removeFromTop(area.getHeight()-17);g.setFont(uiFont(16,Font::bold));g.drawFittedText(text.upToFirstOccurrenceOf("\n",false,false),title,Justification::centredLeft,2,1.0f);g.setColour(b.getToggleState()?ink:muted);g.setFont(uiFont(12));g.drawText(text.fromFirstOccurrenceOf("\n",false,false),area,Justification::centredLeft);}
        else{g.setFont(getTextButtonFont(b,b.getHeight()));g.drawFittedText(text,area,Justification::centred,jmax(1,area.getHeight()/18),1.0f);}
    }
};
class FloatingWindow : public DocumentWindow {
public:
    std::function<void()> close;
    FloatingWindow(String title,Component* content) : DocumentWindow(title,panel,DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true);setContentOwned(content,true);setResizable(true,false);centreWithSize(content->getWidth(),content->getHeight());setVisible(true);
    }
    void closeButtonPressed() override {if(close) close();}
};
class FriendlyList : public ListBox {
public:
    using ListBox::ListBox;
    void paintOverChildren(Graphics& g) override {
        if(getListBoxModel()&&getListBoxModel()->getNumRows()==0){g.setColour(muted);g.setFont(uiFont(16));g.drawFittedText(juce::String::fromUTF8("まだ割り当てはありません。\n左で操作を選んで登録してください。"),20,48,getWidth()-40,80,Justification::centred,3);}
    }
};
class ConnectionPanel : public Component {
    Label title,hint;
    Viewport viewport;
public:
    void paint(Graphics& g) override {g.fillAll(bg);}
    ConnectionPanel(AudioDeviceManager& devices) {
        title.setText(juce::String::fromUTF8("1  出力機器を選ぶ    2  使うMIDI入力にチェック"),dontSendNotification);title.setFont(uiFont(19));
        hint.setText(juce::String::fromUTF8("対応機器なら出力方式にASIOを選びます。音が出ないときは出力先とMIDI入力を確認してください。"),dontSendNotification);hint.setFont(uiFont(14));
        addAndMakeVisible(title);addAndMakeVisible(hint);addAndMakeVisible(viewport);
        auto* selector=new AudioDeviceSelectorComponent(devices,0,0,0,2,true,false,true,false);selector->setItemHeight(30);viewport.setViewedComponent(selector,true);
        setSize(800,680);
    }
    void resized() override {
        title.setBounds(24,16,getWidth()-48,32);hint.setBounds(24,54,getWidth()-48,40);
        viewport.setBounds(12,104,getWidth()-24,getHeight()-116);
        auto* selector=viewport.getViewedComponent();selector->setSize(viewport.getWidth()-20,selector->getHeight());
    }
};
class RecordingButton : public TextButton {
public:
    std::function<void()> options;
    void mouseDown(const MouseEvent& event) override {if(event.mods.isPopupMenu()||(getButtonText().containsChar(0x25be)&&event.x>getWidth()-24)){if(options)options();return;}TextButton::mouseDown(event);}
    void paintButton(Graphics& g,bool hover,bool down) override {
        if(!getButtonText().containsChar(0x25be)){TextButton::paintButton(g,hover,down);return;}
        auto& look=getLookAndFeel();look.drawButtonBackground(g,*this,findColour(getToggleState()?buttonOnColourId:buttonColourId),hover,down);
        g.setColour(findColour(textColourOffId).withAlpha(isEnabled()?1.0f:.5f));g.setFont(look.getTextButtonFont(*this,getHeight()));
        g.drawFittedText(getButtonText().dropLastCharacters(2),getLocalBounds().withTrimmedRight(28).reduced(6,2),Justification::centred,1);
        auto x=(float)getWidth()-13;auto y=(float)getHeight()/2;Path arrow;arrow.addTriangle(x-4,y-2,x+4,y-2,x,y+3);g.fillPath(arrow);
        g.setColour(Colour(0xff94a2b3).withAlpha(.35f));g.drawVerticalLine(getWidth()-26,4.0f,(float)getHeight()-4);
    }
};
class Main final : public Component, private Timer, private ListBoxModel {
    Theme theme;
    struct EditCanvas:Component {std::function<void(Graphics&)> draw;void paint(Graphics& g)override{if(draw)draw(g);}} editCanvas;
    Viewport editViewport;
    std::vector<Component*> canvasControls;
    Engine engine;
    RecordingButton recordButton;
    VideoRecorder cameraPreview;
    std::unique_ptr<FloatingWindow> cameraWindow;
    std::unique_ptr<FileChooser> videoChooser;
    File lastVideo;
    #include "VideoUi.inl"
    bool quitAfterRecording=false;
    File lastRecording;
    std::unique_ptr<FileChooser> recordingChooser;
    #include "RecordingUi.inl"
    RemoteService remote{engine};
    TextButton remoteButton{jp("スマホ連携…")};
    std::unique_ptr<FloatingWindow> remoteWindow;
    ScenePanel mixer{engine,[this]{return editable();},[this]{changed();},[this](String message){inform(message);},[this](const std::function<void()>& fn){suspend(fn);}};
    EffectsPanel effects{engine,[this]{return editable();},[this](const std::function<void()>& fn){suspend(fn);},[this]{changed();},[this](String message){inform(message);}};
    PianoRollPanel piano{engine,[this]{return editable();},[this]{changed();},[this](String message){inform(message);},[this](int i){copySequenceSound(i);},[this]{showSequenceEditor();},[this](int i){chooseSequenceExport(i);}};
    LiveView liveView{engine};
    AudioStrip audioStrip{engine};
    bool performanceDetails=false,forceUi=true;uint32 nextUiRefresh=0,nextMidiPoll=0;int midiInputsEnabled=0;
    TextButton detailButton{jp("切替・消音の設定")},sourceTools{jp("音源の管理…")},shiftReset{jp("0に戻す")};
    ResetSlider globalShift;Label globalTitle,globalValue;
    AudioDeviceManager devices;
    AudioPluginFormatManager formats;
    bool attached=false, live=false, dirty=false, busy=false;
    #include "FxUi.inl"
    bool mappingPage=false, advanced=false, testMode=false, silentPreview=false;
    AudioBuffer<float> silentBuffer;
    bool mixerPage=false,effectsPage=false,pianoPage=false, combinationBank=false;
    TextButton combinationEdit{jp("音色の編集・使い回し…")};
    TextButton singleBank{jp("単体の音源")},sceneBank{jp("作った音色")};
    void playPad(int i){if(i<0||i>=16)return;if(i<8){select(i);return;}i-=8;if(mixer.getScenes()[(size_t)i].registered)engine.control(SelectScene,i);}
    void stepPad(int direction){engine.control(direction>0?NextPerformance:PreviousPerformance);}
    void makeSharedVariant(){
        if(!editable())return;int destination=-1;for(int i=0;i<8;++i)if(!mixer.getScenes()[(size_t)i].registered&&engine.activeScene!=i&&mixer.getScenes()[(size_t)i].name==jp("組み合わせ ")+String(i+1)&&std::none_of(mixer.getScenes()[(size_t)i].parts.begin(),mixer.getScenes()[(size_t)i].parts.end(),[](auto part){return part.enabled||part.included;})){destination=i;break;}if(destination<0){inform(jp("音色の保存場所8個は作成途中も含めて使用中です。「音色を作る」で既存の音色を編集してください。"));return;}
        int source=juce::jlimit(0,7,audioStrip.focus());if(engine.activeScene<0&&!engine.slots[(size_t)source].plugin){inform(jp("先に使い回す音源を追加してください。"));return;}mixer.createVariant(destination,engine.activeScene.load(),source);mixerPage=true;effectsPage=pianoPage=mappingPage=false;updatePage();inform(jp("音源を読み直さず別の音色を作りました。移調や範囲を変更して「演奏画面に追加」で登録します。"));
    }
    void soundMenu(){if(!editable())return;PopupMenu menu;menu.addItem(1,jp("今の音色を編集"),engine.activeScene>=0);menu.addItem(2,jp("同じ音源で別の音色を作る…"));menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&combinationEdit),[safe=Component::SafePointer<Main>(this)](int result){if(!safe||!safe->editable())return;if(result==2)safe->makeSharedVariant();if(result==1){safe->mixer.showScene(safe->engine.activeScene.load());safe->mixerPage=true;safe->effectsPage=safe->pianoPage=safe->mappingPage=false;safe->updatePage();}});}
    bool performancePage() const {return !mappingPage&&!mixerPage&&!effectsPage&&!pianoPage;}
    int observedSlot=0,observedScene=-2;
    TooltipWindow tooltips{this,650};
    File sessionFile;
    std::unique_ptr<FileChooser> chooser;
    std::unique_ptr<FloatingWindow> editor,settings,library;
    std::unique_ptr<ChildProcess> scanner;
    File scanResult;
    uint32 scanStarted=0;
    int scanSlot=0;
    struct PluginDestination {int slot=0,scene=-1,inherit=-1;};
    PluginDestination scanDestination;
    PluginDestination currentDestination()const{return {editSlot.getSelectedId()-1,-1,-1};}
    void completePluginLoad(PluginDestination destination){
        if(destination.scene>=0){editSlot.setSelectedId(destination.slot+1,dontSendNotification);mapSlot.setSelectedId(destination.slot+1,dontSendNotification);mixer.includeSource(destination.scene,destination.slot,destination.inherit,true);mixerPage=true;mappingPage=effectsPage=pianoPage=false;updatePage();audioStrip.focus(destination.slot);inform(jp("音源を重ねに追加しました。下の鍵盤で試聴して、「演奏画面に追加」で登録できます。"));}
        else select(destination.slot);refreshParameters();
    }
    void addBuilderSource(Component& anchor,int inherit){
        if(!editable())return;int scene=mixer.editingScene(),empty=-1;for(int i=0;i<8;++i)if(!engine.slots[(size_t)i].plugin){empty=i;break;}
        PopupMenu menu;menu.addItem(100,jp("インストール済みの音源から…"),empty>=0);menu.addItem(101,jp("内蔵音源を追加"),empty>=0);menu.addItem(102,jp("VSTファイルから…"),empty>=0);
        PopupMenu existing;for(int i=0;i<8;++i)if(engine.slots[(size_t)i].plugin&&!mixer.getScenes()[(size_t)scene].parts[(size_t)i].enabled)existing.addItem(i+1,String(i+1)+"  "+engine.slots[(size_t)i].name);if(existing.getNumItems()>0)menu.addSubMenu(jp("読み込み済みの音源を使う"),existing);
        if(empty<0){menu.addSeparator();menu.addItem(200,jp("音源8枠は使用中です。読み込み済みの音源を選べます。"),false);}
        menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&anchor),[safe=Component::SafePointer<Main>(this),scene,empty,inherit](int result){if(!safe||!safe->editable()||result==0)return;if(result>0&&result<=8){safe->mixer.includeSource(scene,result-1,inherit);safe->audioStrip.focus(result-1);return;}if(empty<0||safe->engine.slots[(size_t)empty].plugin){safe->inform(jp("音源枠が変わりました。もう一度「音源を追加」を選んでください。"));return;}PluginDestination destination{empty,scene,inherit};if(result==100)safe->showLibrary(destination);if(result==101)safe->installDemo(destination);if(result==102)safe->choosePlugin(destination);});
    }
    std::array<TextButton,16> pads;
    Component padCanvas;Viewport padViewport;
    void layoutPads(){int position=0,empty=-1;for(int i=0;i<8;++i)if(!engine.slots[(size_t)i].plugin&&empty<0)empty=i;for(int i=0;i<16;++i){bool show=performancePage()&&(i<8?(engine.slots[(size_t)i].plugin!=nullptr||i==empty):mixer.getScenes()[(size_t)(i-8)].registered);pads[(size_t)i].setVisible(show);if(show){pads[(size_t)i].setBounds((position%2)*191,(position/2)*72,181,64);++position;}}padCanvas.setSize(372,juce::jmax(padViewport.getHeight(),((position+1)/2)*72));}

    TextButton liveButton{"LIVE LOCK"}, audioButton{"Audio / MIDI"},saveButton{"Save"},openButton{"Open"};
    TextButton loadButton{"Load VST3"},demoButton{"Stage Sine"},editButton{"Plugin editor"},clearButton{"Clear slot"};
    TextButton keyzoneButton{jp("ファイルから選ぶ")};
    TextButton nextButton{"Next  >"},prevButton{"<  Previous"},fadeButton{"FADE OUT"},restoreButton{"RESTORE"},panicButton{"PANIC  /  Esc"};
    TextButton learnButton{"Learn MIDI"},addButton{"Add mapping"},deleteButton{"Remove"};
    TextButton playTab{juce::String::fromUTF8("演奏")},mapTab{juce::String::fromUTF8("パッド・ノブの割り当て")},advancedButton{juce::String::fromUTF8("詳しい設定を表示")};
    TextButton diagnosticsButton{juce::String::fromUTF8("遅延・負荷を確認")};
    TextButton mixerTab{jp("音色を作る")},effectsTab{jp("効果音")},pianoTab{jp("打ち込み・鍵盤")};
    Label guideTitle,guideText,mappingHelp,learnHelp,sourceSummary,savedTitle,modeHint;
    ComboBox editSlot,transition,sourceKind,sourceChannel,actionBox,mapSlot,paramBox;
    Slider master,fadeTime,transitionTime,tailTime,number,rangeMin,rangeMax;
    Label status,telemetry,midiStatus,editorLabel,masterLabel,fadeLabel,transitionLabel,tailLabel;
    Label mappingTitle,sourceLabel,channelLabel,numberLabel,actionLabel,slotLabel,paramLabel,rangeLabel;
    FriendlyList mappingList{juce::String::fromUTF8("割り当て一覧"),this};
    std::vector<Mapping> mappings;
    int selectedMapping=-1;
    File settingsFile() const {return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("StagePilot/audio.xml");}
    void style(Label& l,String text,float size=14) {l.setText(text,dontSendNotification);l.setFont(Font(uiFont(size)));addAndMakeVisible(l);}
    void slider(Slider& s,double min,double max,double step,double value,String suffix={}) {
        addAndMakeVisible(s);s.setRange(min,max,step);enableReset(s,value);s.setValue(value,dontSendNotification);s.setSliderStyle(Slider::LinearHorizontal);
        s.setTextBoxStyle(Slider::TextBoxRight,false,70,24);s.setTextValueSuffix(suffix);
    }
    void button(TextButton& b,std::function<void()> f) {addAndMakeVisible(b);b.onClick=std::move(f);}
    void inform(String message) {status.setText(message,dontSendNotification);}
    void publishRemote(){
        Array<var> items;auto add=[&](String kind,int i,String name){auto* o=new DynamicObject();o->setProperty("kind",kind);o->setProperty("index",i);o->setProperty("name",name);o->setProperty("color",instrumentColour(i).toDisplayString(false));items.add(var(o));};
        for(int i=0;i<8;++i){if(engine.slots[(size_t)i].plugin)add("instrument",i,engine.slots[(size_t)i].name);if(mixer.getScenes()[(size_t)i].registered)add("scene",i,mixer.getScenes()[(size_t)i].name);if(engine.clips[(size_t)i].data.audio.getNumSamples()>0)add("effect",i,engine.clips[(size_t)i].data.name);}
        auto* o=new DynamicObject();o->setProperty("items",items);o->setProperty("session",sessionFile.existsAsFile()?sessionFile.getFileNameWithoutExtension():jp("編集中のセット"));remote.publish(var(o));
    }
    void showRemote(){if(silentPreview)return;publishRemote();remoteWindow=std::make_unique<FloatingWindow>(jp("スマホ連携"),new RemotePanel(remote,settingsFile().getSiblingFile("remote.json")));remoteWindow->close=[this]{remoteWindow.reset();};}
    void suspend(const std::function<void()>& fn) {
        remote.allowCommands(false);
        const bool reattach=attached;
        if(attached){devices.removeAudioCallback(&engine);attached=false;}
        engine.sequence.pattern=piano.getPattern();engine.scenes=mixer.getScenes();
        fn();engine.bindings.fill({});for(size_t i=0;i<mappings.size();++i)engine.bindings[i]=mappings[i];if(reattach){devices.addAudioCallback(&engine);attached=true;}
        publishRemote();remote.allowCommands(!busy&&!silentPreview&&(testMode||devices.getCurrentAudioDevice()!=nullptr));
    }
    bool editable() {if(live){inform(juce::String::fromUTF8("演奏モード中です。「設定に戻る」を押すと編集できます。"));return false;}return !busy;}
    void closeEditor() {fxEditor.reset();editor.reset();}
    void changed() {dirty=true;forceUi=true;}
    void select(int i) {
        audioStrip.focus(i);
        editSlot.setSelectedId(i+1,dontSendNotification);mapSlot.setSelectedId(i+1,dontSendNotification);closeEditor();refreshParameters();
        if(engine.slots[(size_t)i].plugin)engine.control(Select,i);
        else inform(juce::String::fromUTF8("音色 ")+String(i+1)+juce::String::fromUTF8(" を選びました。「音源を追加」または「内蔵音源で試す」を押してください。"));
    }
    void updatePage() {
        for(auto* c:std::vector<Component*>{&singleBank,&sceneBank,&combinationEdit,&loadButton,&keyzoneButton,&demoButton,&editButton,&clearButton,&prevButton,&nextButton,&transition,&master,&fadeTime,&transitionTime,&tailTime,&editorLabel,&masterLabel,&fadeLabel,&transitionLabel,&tailLabel,&guideTitle,&guideText,&modeHint,&diagnosticsButton})c->setVisible(performancePage());
        padViewport.setVisible(performancePage());layoutPads();singleBank.setVisible(false);sceneBank.setVisible(false);
        mixer.setVisible(mixerPage);effects.setVisible(effectsPage);piano.setVisible(pianoPage);
        for(auto* c:std::vector<Component*>{&learnButton,&deleteButton,&actionBox,&mappingTitle,&actionLabel,&mappingList,&mappingHelp,&learnHelp,&sourceSummary,&savedTitle,&advancedButton})c->setVisible(mappingPage);
        bool needsSlot=instrumentTarget(actionBox.getSelectedId()-1)||clipTarget(actionBox.getSelectedId()-1)||sceneTarget(actionBox.getSelectedId()-1);
        slotLabel.setText(sceneTarget(actionBox.getSelectedId()-1)?jp("切り替える組み合わせ"):clipTarget(actionBox.getSelectedId()-1)?jp("操作する効果音"):jp("操作する音色"),dontSendNotification);
        bool needsParam=actionBox.getSelectedId()==Parameter+1;
        for(auto* c:std::vector<Component*>{&mapSlot,&slotLabel})c->setVisible(mappingPage&&needsSlot);
        for(auto* c:std::vector<Component*>{&paramBox,&paramLabel})c->setVisible(mappingPage&&needsParam);
        for(auto* c:std::vector<Component*>{&sourceKind,&sourceChannel,&number,&sourceLabel,&channelLabel,&numberLabel,&rangeMin,&rangeMax,&rangeLabel,&addButton})c->setVisible(mappingPage&&advanced);
        playTab.setToggleState(performancePage(),dontSendNotification);mapTab.setToggleState(mappingPage,dontSendNotification);mixerTab.setToggleState(mixerPage,dontSendNotification);effectsTab.setToggleState(effectsPage,dontSendNotification);pianoTab.setToggleState(pianoPage,dontSendNotification);
        advancedButton.setButtonText(advanced?juce::String::fromUTF8("詳しい設定を閉じる"):juce::String::fromUTF8("番号や操作範囲を手動で設定"));
        if(pianoPage)audioStrip.focus(8);else if(mixerPage)audioStrip.focus(mixer.focusedSource());
        forceUi=true;resized();repaint();
    }
    void refreshParameters() {
        paramBox.clear();int i=mapSlot.getSelectedId()-1;
        if(i>=0) if(auto* p=engine.slots[(size_t)i].plugin.get()) {
            auto& parameters=p->getParameters();
            for(int n=0;n<parameters.size();++n) paramBox.addItem(String(n+1)+"  "+parameters[n]->getName(60),n+1);
        }
        if(paramBox.getNumItems()>0) paramBox.setSelectedItemIndex(0);
    }
    void putPlugin(int index,std::unique_ptr<AudioPluginInstance> p,const PluginDescription& description) {
        auto& s=engine.slots[(size_t)index];s.plugin=std::move(p);s.description=description;s.name=description.name;
        if(!s.plugin){s.fx.clear();s.layer=false;s.transpose=0;s.gain=1;s.ignorePedal=false;}s.trim.setCurrentAndTargetValue(s.gain.load());
        if(s.plugin) {s.plugin->enableAllBuses();auto layout=s.plugin->getBusesLayout();
            for(auto& input:layout.inputBuses) input=AudioChannelSet::disabled();
            for(int b=0;b<layout.outputBuses.size();++b) layout.outputBuses.getReference(b)=b==0?AudioChannelSet::stereo():AudioChannelSet::disabled();
            s.plugin->setBusesLayout(layout);
        }
    }
    void installDemo(){installDemo(currentDestination());}
    void installDemo(PluginDestination destination) {
        if(!editable())return;closeEditor();int index=destination.slot;
        suspend([&]{auto p=makeTestSynth();if(destination.scene>=0)p->getParameters()[1]->setValueNotifyingHost(.05f);PluginDescription d;p->fillInPluginDescription(d);putPlugin(index,std::move(p),d);});
        completePluginLoad(destination);changed();if(destination.scene<0)inform(juce::String::fromUTF8("内蔵音源を追加しました。画面の鍵盤でも試聴できます。"));
    }
    void choosePlugin(){choosePlugin(currentDestination());}
    void choosePlugin(PluginDestination destination) {
        if(!editable())return;
        chooser=std::make_unique<FileChooser>(juce::String::fromUTF8("使う音源（64bit VST2 / VST3）を選んでください"),File("C:/Program Files"),"*.vst3;*.dll");
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles|FileBrowserComponent::canSelectDirectories,[safe=Component::SafePointer<Main>(this),destination](const FileChooser& fc){
            if(!safe)return;auto f=fc.getResult();if(f.exists()&&f.hasFileExtension("vst3;dll"))safe->loadPlugin(f,destination);
        });
    }
    void showLibrary(){showLibrary(currentDestination());}
    void showLibrary(PluginDestination destination) {
        if(!editable())return;if(library){library->toFront(true);return;}
        auto safe=Component::SafePointer<Main>(this);
        auto* content=new InstrumentBrowser([safe,destination](File file){
            MessageManager::callAsync([safe,file,destination]{if(!safe||!safe->editable())return;safe->library.reset();safe->loadPlugin(file,destination);});
        },settingsFile().getSiblingFile("plugin-folders.txt"));
        content->setLookAndFeel(&theme);
        library=std::make_unique<FloatingWindow>(destination.scene>=0?jp("音色を作る — 音源を選んで追加"):jp("音源を自動検索 — 選んだ音色枠に追加"),content);
        library->close=[safe]{MessageManager::callAsync([safe]{if(safe)safe->library.reset();});};
    }
    void loadPlugin(const File& file){loadPlugin(file,currentDestination());}
    void loadPlugin(const File& file,PluginDestination destination) {
        if(!editable())return;auto error=checkPluginFile(file);if(error.isNotEmpty()){inform(error);return;}
        if(destination.slot<0||destination.slot>=8)return;if(destination.scene>=0&&engine.slots[(size_t)destination.slot].plugin){inform(jp("追加先の音源枠が変わりました。もう一度「音源を追加」を選んでください。"));return;}busy=true;scanDestination=destination;scanSlot=destination.slot;
        scanResult=File::getSpecialLocation(File::tempDirectory).getChildFile("StagePilot-scan-"+Uuid().toString()+".xml");
        scanner=std::make_unique<ChildProcess>();
        StringArray args{File::getSpecialLocation(File::currentExecutableFile).getFullPathName(),"--scan",file.getFullPathName(),scanResult.getFullPathName()};
        if(!scanner->start(args,0)){scanner.reset();busy=false;inform(juce::String::fromUTF8("音源の確認を開始できませんでした。もう一度お試しください。"));return;}
        scanStarted=Time::getMillisecondCounter();inform(juce::String::fromUTF8("音源を確認中：")+file.getFileName()+juce::String::fromUTF8("（最大20秒）"));
    }
    void finishScan() {
        if(scanFxTarget>=0){finishFxScan();return;}
        auto xml=parseXML(scanResult);scanResult.deleteFile();scanner.reset();busy=false;
        if(!xml){inform(juce::String::fromUTF8("音源を確認できませんでした。DLL本体と付属の音源データが元の場所にあるか確認してください。現在の音色設定はそのままです。"));return;}
        PluginDescription candidate;bool found=false;
        for(auto* child:xml->getChildIterator()){PluginDescription d;if(d.loadFromXml(*child)&&d.isInstrument){candidate=d;found=true;break;}}
        if(!found){inform(juce::String::fromUTF8("対応する音源が見つかりませんでした。64bit VST2 / VST3の音源を選んでください。エフェクト専用DLLは未対応です。"));return;}
        closeEditor();busy=true;bool loaded=false;
        suspend([&]{
            String error;auto p=formats.createPluginInstance(candidate,engine.rate.load(),engine.blockSize.load(),error);
            if(!p){inform(juce::String::fromUTF8("音源を読み込めませんでした：")+error);return;}
            putPlugin(scanSlot,std::move(p),candidate);loaded=true;changed();inform(juce::String::fromUTF8("追加しました：")+candidate.name+juce::String::fromUTF8("。「音源の画面を開く」で音色を選べます。"));
        });busy=false;if(loaded)completePluginLoad(scanDestination);
    }
    void showEditor() {
        if(busy)return;closeEditor();int i=editSlot.getSelectedId()-1;auto* p=engine.slots[(size_t)i].plugin.get();
        if(!p){inform(juce::String::fromUTF8("まず、この音色に音源を追加してください。"));return;}
        auto* content=p->createEditorAndMakeActive();if(!content)content=new GenericAudioProcessorEditor(*p);
        editor=std::make_unique<FloatingWindow>(p->getName(),content);
        editor->close=[safe=Component::SafePointer<Main>(this)]{MessageManager::callAsync([safe]{if(safe)safe->closeEditor();});};
        changed();
    }
    void showSettings() {
        if(silentPreview)return;
        if(!editable())return;if(settings){settings->toFront(true);return;}
        auto* content=new ConnectionPanel(devices);content->setLookAndFeel(&theme);
        settings=std::make_unique<FloatingWindow>(juce::String::fromUTF8("接続設定 — 出力機器とMIDIキーボードを選択"),content);
        settings->close=[safe=Component::SafePointer<Main>(this)]{MessageManager::callAsync([safe]{if(safe){safe->settings.reset();safe->saveAudio();}});};
    }
    static void configureSequencePlugin(AudioPluginInstance& p) {
        p.enableAllBuses();auto layout=p.getBusesLayout();for(auto& input:layout.inputBuses)input=AudioChannelSet::disabled();
        for(int i=0;i<layout.outputBuses.size();++i)layout.outputBuses.getReference(i)=i==0?AudioChannelSet::stereo():AudioChannelSet::disabled();p.setBusesLayout(layout);
    }
    void copySequenceSound(int index) {
        if(!editable())return;
        if(index>=0&&!engine.slots[(size_t)index].plugin){inform(jp("その枠には音源がありません。「演奏」で追加するか、内蔵シンセを選んでください。"));return;}
        closeEditor();bool copied=false;
        suspend([&]{
            PluginDescription d;MemoryBlock state;String name,error;std::unique_ptr<AudioPluginInstance> p;
            if(index<0){p=makeTestSynth();p->fillInPluginDescription(d);name=d.name;}
            else{auto& slot=engine.slots[(size_t)index];d=slot.description;name=slot.name;slot.plugin->getStateInformation(state);
                p=d.pluginFormatName=="Internal"?makeTestSynth():formats.createPluginInstance(d,engine.rate.load(),engine.blockSize.load(),error);}
            if(!p){inform(jp("音色をコピーできませんでした：")+error);return;}
            configureSequencePlugin(*p);if(state.getSize())p->setStateInformation(state.getData(),(int)state.getSize());
            engine.sequence.plugin=std::move(p);engine.sequence.description=d;engine.sequence.name=name;copied=true;
        });
        if(copied){changed();inform(jp("打ち込み用に音色をコピーしました。演奏用の音源とは独立して試聴・調整できます。"));}
    }
    void showSequenceEditor() {
        if(!editable()||!engine.sequence.plugin)return;closeEditor();auto* p=engine.sequence.plugin.get();auto* content=p->createEditorAndMakeActive();if(!content)content=new GenericAudioProcessorEditor(*p);
        editor=std::make_unique<FloatingWindow>(jp("打ち込み用の音色 — ")+engine.sequence.name,content);
        editor->close=[safe=Component::SafePointer<Main>(this)]{MessageManager::callAsync([safe]{if(safe)safe->closeEditor();});};changed();
    }
    bool exportSequence(const File& file,int destination) {
        if(destination<0||destination>=clipCount||!engine.sequence.plugin)return false;
        bool hasNotes=false;for(auto n:piano.getPattern().notes)hasNotes|=n.enabled;
        if(!hasNotes){inform(jp("まずマス目に音を置いてください。「お手本を入れる」からも試せます。"));return false;}
        closeEditor();bool success=false;String error;busy=true;
        suspend([&]{
            auto& seq=engine.sequence;auto pattern=seq.pattern;
            constexpr double sr=48000;constexpr int blockSize=256;
            seq.plugin->setNonRealtime(true);seq.prepare(sr,blockSize);seq.pattern.loop=false;
            int latency=seq.plugin->getLatencySamples();
            if(latency<0||latency>sr*10){error=jp("この音源の遅延が大きすぎるため書き出せません。");seq.pattern=pattern;seq.plugin->setNonRealtime(false);return;}
            const int frames=(int)seq.durationFrames()+144000;
            int64_t total=frames;for(int i=0;i<clipCount;++i)if(i!=destination)total+=engine.clips[(size_t)i].data.audio.getNumSamples();
            if(total*8>128*1024*1024){error=jp("効果音の合計が128MBを超えます。不要な効果音を外してください。");seq.pattern=pattern;seq.plugin->setNonRealtime(false);return;}
            AudioBuffer<float> audio(2,frames),block(2,blockSize);seq.start();
            for(int at=0;at<frames+latency;at+=blockSize){int count=jmin(blockSize,frames+latency-at);block.clear();if(at>0)seq.beginBlock();seq.render(block.getArrayOfWritePointers(),2,count);
                int from=jmax(0,latency-at),length=count-from;if(length>0)for(int c=0;c<2;++c)audio.copyFrom(c,at+from-latency,block,c,from,length);}
            seq.stop(true);seq.plugin->reset();seq.plugin->releaseResources();seq.reset();seq.pattern=pattern;seq.plugin->setNonRealtime(false);
            // Retain headroom and fade the final 10 ms, including unusually long VST tails.
            audio.applyGain(0.7f);audio.applyGainRamp(frames-480,480,1.0f,0.0f);
            for(int c=0;c<2;++c)for(int n=0;n<frames;++n)audio.setSample(c,n,jlimit(-1.0f,1.0f,audio.getSample(c,n)));
            TemporaryFile temporary(file);WavAudioFormat format;auto stream=temporary.getFile().createOutputStream();
            if(!stream){error=jp("保存先に書き込めません。");return;}
            std::unique_ptr<AudioFormatWriter> writer(format.createWriterFor(stream.release(),sr,2,24,{},0));
            if(!writer||!writer->writeFromAudioSampleBuffer(audio,0,frames)){error=jp("WAVを書き出せませんでした。");return;}
            writer.reset();if(!temporary.overwriteTargetFileWithTemporary()){error=jp("保存先のWAVを更新できませんでした。");return;}
            ClipData data;data.audio=std::move(audio);data.sampleRate=sr;data.file=file;data.name=file.getFileNameWithoutExtension();
            auto& target=engine.clips[(size_t)destination];target.data=std::move(data);target.gain=.8f;target.loop=false;success=true;
        });busy=false;
        if(success){changed();inform(jp("WAVを保存し、効果音 ")+String(destination+1)+jp(" に登録しました。「効果音」画面で再生・キー割り当てできます。"));}
        else inform(error);return success;
    }
    void chooseSequenceExport(int destination) {
        if(!editable())return;
        chooser=std::make_unique<FileChooser>(jp("フレーズをWAVに保存（指定した効果音枠を置き換えます）"),File::getSpecialLocation(File::userDocumentsDirectory).getChildFile(jp("作った効果音.wav")),"*.wav");
        chooser->launchAsync(FileBrowserComponent::saveMode|FileBrowserComponent::warnAboutOverwriting,[safe=Component::SafePointer<Main>(this),destination](const FileChooser& fc){if(!safe||!safe->editable()||fc.getResult()==File())return;safe->exportSequence(fc.getResult().withFileExtension("wav"),destination);});
    }
    void saveAudio() {if(testMode)return;if(auto xml=devices.createStateXml()){auto f=settingsFile();f.getParentDirectory().createDirectory();xml->writeTo(f);}}
    var toJson(const File& base={}) {
        auto* root=new DynamicObject();root->setProperty("format","StagePilot");root->setProperty("version",5);root->setProperty("performanceTranspose",engine.performanceTranspose.load());
        root->setProperty("selected",engine.selected.load());root->setProperty("mode",engine.mode.load());
        root->setProperty("master",engine.masterValue.load());root->setProperty("fade",engine.fadeSeconds.load());
        root->setProperty("transition",engine.transitionSeconds.load());root->setProperty("tail",engine.tailSeconds.load());
        Array<var> slots;
        for(auto& slot:engine.slots) {
            auto* o=new DynamicObject();o->setProperty("name",slot.name);
            if(slot.plugin) {
                o->setProperty("description",slot.description.createXml()->toString());
                MemoryBlock state;slot.plugin->getStateInformation(state);o->setProperty("state",state.toBase64Encoding());
            }
            o->setProperty("fx",fxJson(slot.fx));o->setProperty("gain",slot.trim.getTargetValue());o->setProperty("layer",slot.layer.load());o->setProperty("transpose",slot.transpose.load());o->setProperty("ignorePedal",slot.ignorePedal.load());slots.add(var(o));
        }
        root->setProperty("masterFx",fxJson(engine.masterFx));root->setProperty("slots",slots);Array<var> maps;
        for(auto& m:mappings) {auto* o=new DynamicObject();o->setProperty("kind",m.kind);o->setProperty("channel",m.channel);o->setProperty("number",m.number);
            if(m.action==Parameter)if(auto* p=engine.slots[(size_t)m.slot].plugin.get())if(m.parameter>=0&&m.parameter<p->getParameters().size())o->setProperty("parameterId",p->getHostedParameter(m.parameter)->getParameterID());
            o->setProperty("action",m.action);o->setProperty("slot",m.slot);o->setProperty("parameter",m.parameter);o->setProperty("min",m.minimum);o->setProperty("max",m.maximum);maps.add(var(o));}
        root->setProperty("mappings",maps);
        Array<var> clips;for(auto& c:engine.clips){auto* o=new DynamicObject();o->setProperty("file",base.isDirectory()&&c.data.file.isAChildOf(base)?c.data.file.getRelativePathFrom(base):c.data.file.getFullPathName());o->setProperty("builtin",c.data.builtin);o->setProperty("gain",c.gain.load());o->setProperty("loop",c.loop.load());clips.add(var(o));}root->setProperty("clips",clips);
        auto sequence=Sequencer::toJson(engine.sequence.pattern);auto* seq=sequence.getDynamicObject();seq->setProperty("name",engine.sequence.name);seq->setProperty("gain",engine.sequence.gain.load());
        if(engine.sequence.plugin){seq->setProperty("description",engine.sequence.description.createXml()->toString());MemoryBlock data;engine.sequence.plugin->getStateInformation(data);seq->setProperty("state",data.toBase64Encoding());}
        root->setProperty("sequence",sequence);root->setProperty("scenes",scenesJson(engine.scenes));root->setProperty("activeScene",engine.activeScene.load());root->setProperty("standalone",partsJson(engine.standalone));return var(root);
    }
    void saveSession() {
        if(!editable())return;chooser=std::make_unique<FileChooser>(juce::String::fromUTF8("演奏セットを保存"),sessionFile.existsAsFile()?sessionFile:File::getSpecialLocation(File::userDocumentsDirectory).getChildFile(juce::String::fromUTF8("演奏セット.stagepilot")),"*.stagepilot");
        chooser->launchAsync(FileBrowserComponent::saveMode|FileBrowserComponent::warnAboutOverwriting,[safe=Component::SafePointer<Main>(this)](const FileChooser& fc){
            if(!safe||!safe->editable())return;auto f=fc.getResult();if(f==File())return;var json;safe->suspend([&]{json=safe->toJson(f.getParentDirectory());});
            f=f.withFileExtension("stagepilot");TemporaryFile tmp(f);
            if(tmp.getFile().replaceWithText(JSON::toString(json))&&tmp.overwriteTargetFileWithTemporary()){safe->sessionFile=f;safe->dirty=false;safe->inform(juce::String::fromUTF8("保存しました：")+f.getFileName());}
            else safe->inform(juce::String::fromUTF8("保存できませんでした。保存済みのファイルはそのままです。"));
        });
    }
    void chooseSession() {
        if(!editable())return;
        if(dirty) {
            AlertWindow::showAsync(MessageBoxOptions().withIconType(MessageBoxIconType::QuestionIcon).withTitle(juce::String::fromUTF8("別の演奏セットを開きますか？")).withMessage(juce::String::fromUTF8("まだ保存していない変更があります。残したい場合はキャンセルして保存してください。")).withButton(juce::String::fromUTF8("変更を破棄して開く")).withButton(juce::String::fromUTF8("キャンセル")),[safe=Component::SafePointer<Main>(this)](int result){if(safe&&result==1)safe->openChooser();});
        } else openChooser();
    }
    void openChooser() {
        chooser=std::make_unique<FileChooser>(juce::String::fromUTF8("演奏セットを開く"),File::getSpecialLocation(File::userDocumentsDirectory),"*.stagepilot");
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles,[safe=Component::SafePointer<Main>(this)](const FileChooser& fc){if(safe&&fc.getResult().existsAsFile())safe->loadSession(fc.getResult());});
    }
    void loadSession(const File& f) {
        if(!editable())return;
        auto json=JSON::parse(f);auto* a=json["slots"].getArray();auto* mapArray=json["mappings"].getArray();
        if(json["format"].toString()!="StagePilot" || ((int)json["version"]<1||(int)json["version"]>5) || !a || a->size()!=slotCount || !mapArray || mapArray->size()>mappingCount) {inform(juce::String::fromUTF8("このファイルは対応する演奏セットではありません。"));return;}
        int pendingShift=(int)json.getProperty("performanceTranspose",0);if(pendingShift<-24||pendingShift>24){inform(jp("全体の移調値が壊れています。現在のセットはそのままです。"));return;}
        auto* clipArray=json["clips"].getArray();if((int)json["version"]>=2&&(!clipArray||clipArray->size()!=clipCount)){inform(jp("効果音の情報が壊れています。"));return;}
        Pattern pendingPattern;
        if((int)json["version"]>=3&&!Sequencer::fromJson(json["sequence"],pendingPattern)){inform(jp("打ち込みの情報が壊れています。現在のセットはそのままです。"));return;}
        std::array<Scene,sceneCount> pendingScenes;std::array<ScenePart,slotCount> pendingStandalone{};
        int pendingActive=-1;
        for(int i=0;i<sceneCount;++i)pendingScenes[(size_t)i].name=jp("組み合わせ ")+String(i+1);
        if((int)json["version"]>=4){pendingActive=(int)json["activeScene"];if(pendingActive<-1||pendingActive>=sceneCount||!readScenes(json["scenes"],pendingScenes)||!readParts(json["standalone"],pendingStandalone)){inform(jp("組み合わせ・鍵盤範囲の情報が壊れています。現在のセットはそのままです。"));return;}}
        closeEditor();StringArray failures;bool success=true;busy=true;
        suspend([&]{
            std::array<std::unique_ptr<AudioPluginInstance>,slotCount> pending;
            std::array<PluginDescription,slotCount> descriptions;
            std::array<ClipData,clipCount> pendingClips;
            int64_t clipFrames=0;
            if(clipArray)for(int i=0;i<clipCount;++i){auto item=(*clipArray)[i];int builtin=(int)item.getProperty("builtin",-1);
                if(builtin>=0&&builtin<clipCount)pendingClips[(size_t)i]=makeDemoClip(builtin);
                else if(item["file"].toString().isNotEmpty()){String error;auto file=f.getParentDirectory().getChildFile(item["file"].toString());if(!readClip(file,pendingClips[(size_t)i],error)){success=false;failures.add(file.getFileName()+": "+error);return;}}
                clipFrames+=pendingClips[(size_t)i].audio.getNumSamples();if(clipFrames*8>128*1024*1024){success=false;failures.add(jp("効果音の合計が128MBを超えます。"));return;}}
            for(int i=0;i<slotCount;++i) {
                auto item=(*a)[i];auto text=item["description"].toString();if(text.isEmpty())continue;
                auto xml=parseXML(text);auto& d=descriptions[(size_t)i];
                if(!xml||!d.loadFromXml(*xml)){failures.add(juce::String::fromUTF8("音源情報が壊れています"));success=false;break;}
                String error;
                if(d.pluginFormatName=="Internal")pending[(size_t)i]=makeTestSynth();
                else pending[(size_t)i]=formats.createPluginInstance(d,engine.rate.load(),engine.blockSize.load(),error);
                if(!pending[(size_t)i]){failures.add(d.name+": "+error);success=false;break;}
                MemoryBlock state;if(!state.fromBase64Encoding(item["state"].toString())){success=false;failures.add(juce::String::fromUTF8("音色データを読み込めません：")+d.name);break;}
                pending[(size_t)i]->setStateInformation(state.getData(),(int)state.getSize());
            }
            if(!success)return;
            PluginDescription sequenceDescription;std::unique_ptr<AudioPluginInstance> sequencePlugin;String sequenceName;
            if((int)json["version"]>=3){auto item=json["sequence"];String error;
                auto xml=parseXML(item["description"].toString());
                if(!xml||!sequenceDescription.loadFromXml(*xml)){success=false;failures.add(jp("打ち込み用音源の情報が壊れています"));return;}
                sequencePlugin=sequenceDescription.pluginFormatName=="Internal"?makeTestSynth():formats.createPluginInstance(sequenceDescription,engine.rate.load(),engine.blockSize.load(),error);
                MemoryBlock state;if(!sequencePlugin||!state.fromBase64Encoding(item["state"].toString())){success=false;failures.add(jp("打ち込み用音源を読み込めません：")+error);return;}
                configureSequencePlugin(*sequencePlugin);sequencePlugin->setStateInformation(state.getData(),(int)state.getSize());sequenceName=item["name"].toString();
            }else{sequencePlugin=makeTestSynth();sequencePlugin->fillInPluginDescription(sequenceDescription);sequenceName=sequenceDescription.name;}
            std::array<std::unique_ptr<FxChain>,9> pendingFx;String fxError;for(int i=0;i<9;++i){pendingFx[(size_t)i]=std::make_unique<FxChain>();var value=i==8?json["masterFx"]:(*a)[i]["fx"];if(value.isVoid()){if((int)json["version"]>=5){success=false;failures.add(jp("FXの情報がありません。"));return;}continue;}if(!readFx(value,*pendingFx[(size_t)i],fxError)){success=false;failures.add(fxError);return;}}
            engine.scenes=pendingScenes;engine.standalone=pendingStandalone;engine.activeScene=pendingActive;engine.performanceTranspose=pendingShift;
            for(auto& slot:engine.slots){slot.keyLow=0;slot.keyHigh=127;}
            engine.sequence.plugin=std::move(sequencePlugin);engine.sequence.description=sequenceDescription;engine.sequence.name=sequenceName;engine.sequence.pattern=pendingPattern;engine.sequence.gain=jlimit(0.0f,1.0f,(float)json["sequence"].getProperty("gain",1.0f));
            for(int i=0;i<slotCount;++i){putPlugin(i,std::move(pending[(size_t)i]),descriptions[(size_t)i]);auto& s=engine.slots[(size_t)i];s.gain=jlimit(0.0f,1.0f,(float)(*a)[i]["gain"]);s.trim.setCurrentAndTargetValue(s.gain.load());s.layer=(bool)(*a)[i]["layer"];s.transpose=jlimit(-24,24,(int)(*a)[i]["transpose"]);s.ignorePedal=(bool)(*a)[i].getProperty("ignorePedal",false);}
            for(int i=0;i<9;++i)chain(i).swapWith(*pendingFx[(size_t)i]);
            for(int i=0;i<clipCount;++i){auto& c=engine.clips[(size_t)i];c.data=std::move(pendingClips[(size_t)i]);c.gain=clipArray?jlimit(0.0f,1.0f,(float)(*clipArray)[i]["gain"]):0.8f;c.loop=clipArray?(bool)(*clipArray)[i]["loop"]:false;}
            engine.selected=jlimit(0,slotCount-1,(int)json["selected"]);engine.mode=jlimit(0,2,(int)json["mode"]);
            engine.masterValue=jlimit(0.0f,1.0f,(float)json["master"]);engine.fadeSeconds=jlimit(0.01f,20.0f,(float)json["fade"]);
            engine.transitionSeconds=jlimit(0.005f,2.0f,(float)json["transition"]);engine.tailSeconds=jlimit(0.1f,30.0f,(float)json["tail"]);
            mappings.clear();engine.bindings.fill({});
            for(auto& item:*mapArray) {
                Mapping m;m.kind=(int)item["kind"];m.channel=jlimit(0,16,(int)item["channel"]);m.number=jlimit(0,127,(int)item["number"]);
                m.action=(int)item["action"];if(m.action<0||m.action>PreviousPerformance){failures.add(jp("未対応の操作割り当てを省略しました。"));continue;}m.slot=jlimit(0,slotCount-1,(int)item["slot"]);m.parameter=jmax(0,(int)item["parameter"]);
                m.minimum=jlimit(0.0f,1.0f,(float)item["min"]);m.maximum=jlimit(0.0f,1.0f,(float)item["max"]);
                if(m.action==Parameter&&item.hasProperty("parameterId")) {
                    m.parameter=-1;if(auto* p=engine.slots[(size_t)m.slot].plugin.get())for(int n=0;n<p->getParameters().size();++n)if(p->getHostedParameter(n)->getParameterID()==item["parameterId"].toString()){m.parameter=n;break;}
                    if(m.parameter<0){failures.add(juce::String::fromUTF8("見つからないパラメーターの割り当ては省略しました"));continue;}
                }
                if(m.kind==9||m.kind==11||m.kind==12){engine.bindings[mappings.size()]=m;mappings.push_back(m);}
            }
        });
        busy=false;if(!success){inform(juce::String::fromUTF8("読み込みに失敗しました。現在のセットはそのままです：")+failures.joinIntoString("; "));return;}
        piano.sync();mixer.sync();refreshFx();combinationBank=engine.activeScene>=0&&mixer.getScenes()[(size_t)engine.activeScene.load()].registered;
        master.setValue(engine.masterValue.load(),dontSendNotification);transition.setSelectedId(engine.mode.load()+1,dontSendNotification);
        fadeTime.setValue(engine.fadeSeconds.load(),dontSendNotification);transitionTime.setValue(engine.transitionSeconds.load()*1000,dontSendNotification);tailTime.setValue(engine.tailSeconds.load(),dontSendNotification);
        sessionFile=f;dirty=false;forceUi=true;mappingList.updateContent();refreshParameters();inform(juce::String::fromUTF8("開きました：")+f.getFileName()+(failures.isEmpty()?String():" / "+failures.joinIntoString("; ")));
    }
    Mapping currentMapping() {
        Mapping m;m.kind=sourceKind.getSelectedId();m.channel=sourceChannel.getSelectedId()-1;m.number=(int)number.getValue();
        m.action=actionBox.getSelectedId()-1;m.slot=mapSlot.getSelectedId()-1;m.parameter=paramBox.getSelectedId()-1;
        m.minimum=(float)rangeMin.getValue()/100;m.maximum=(float)rangeMax.getValue()/100;return m;
    }
    void addMapping() {
        if(!editable())return;if(mappings.size()>=mappingCount){inform(juce::String::fromUTF8("割り当てが上限の64件になっています。不要な割り当てを削除してください。"));return;}
        auto m=currentMapping();if(m.action==Parameter&&m.parameter<0){inform(juce::String::fromUTF8("先に、操作する音源とパラメーターを選んでください。"));return;}
        for(auto& old:mappings) if(old.kind==m.kind&&old.number==m.number&&(old.channel==0||m.channel==0||old.channel==m.channel)){inform(juce::String::fromUTF8("このパッド・ノブは登録済みです。右の一覧で選んで削除してから、登録し直してください。"));return;}
        Event e;e.type=Event::Bind;e.a=(int)mappings.size();e.mapping=m;if(!engine.post(e)){inform(juce::String::fromUTF8("入力が混み合っています。少し待ってもう一度お試しください。"));return;}
        mappings.push_back(m);mappingList.updateContent();mappingList.selectRow((int)mappings.size()-1);changed();inform(juce::String::fromUTF8("登録しました！ このパッド・ノブで選んだ操作を実行できます。"));
    }
    int getNumRows() override {return (int)mappings.size();}
    void selectedRowsChanged(int row) override {selectedMapping=row;}
    void paintListBoxItem(int row,Graphics& g,int width,int height,bool selected) override {
        if(row<0||row>=(int)mappings.size())return;auto& m=mappings[(size_t)row];
        if(selected)g.fillAll(Colour(0xff314a44));
        StringArray actions{juce::String::fromUTF8("音色を切り替え"),juce::String::fromUTF8("次の音色へ"),juce::String::fromUTF8("前の音色へ"),juce::String::fromUTF8("ゆっくり消音"),juce::String::fromUTF8("音量を戻す"),juce::String::fromUTF8("すべての音を停止"),juce::String::fromUTF8("全体の音量"),juce::String::fromUTF8("音色の音量"),juce::String::fromUTF8("パラメーター"),jp("重ねる・解除"),jp("効果音を再生"),jp("効果音を停止"),jp("効果音を全停止"),jp("移調"),jp("重ね演奏"),jp("打ち込みを再生"),jp("打ち込みを停止"),jp("組み合わせを切り替え"),jp("次の組み合わせ"),jp("前の組み合わせ")};
        String text=m.action<actions.size()?actions[m.action]:m.action==PerformanceTranspose?jp("全体の移調"):m.action==SetIgnorePedal?jp("ペダルの設定"):m.action==SequenceGain?jp("打ち込みの音量"):m.action==PerformanceTransposeStep?jp("全体の移調を増減"):m.action==NextPerformance?jp("次の音色へ"):jp("前の音色へ");
        if(sceneTarget(m.action))text+=jp(" → ")+engine.scenes[(size_t)m.slot].name;
        if(instrumentTarget(m.action))text+=jp(" → 音色 ")+String(m.slot+1);
        if(clipTarget(m.action))text+=jp(" → 効果音 ")+String(m.slot+1)+" / "+engine.clips[(size_t)m.slot].data.name;
        if(m.action==Parameter)if(auto* p=engine.slots[(size_t)m.slot].plugin.get())if(m.parameter>=0&&m.parameter<p->getParameters().size())text+=" / "+p->getParameters()[m.parameter]->getName(40);
        g.setColour(ink);g.setFont(uiFont(15));g.drawText(text,14,5,width-28,25,Justification::centredLeft);
        String source=(m.kind==9?juce::String::fromUTF8("パッド・鍵盤 "):m.kind==11?juce::String::fromUTF8("ノブ・フェーダー "):juce::String::fromUTF8("音色変更信号 "))+String(m.number)+"  /  "+(m.channel?juce::String::fromUTF8("チャンネル ")+String(m.channel):juce::String::fromUTF8("全チャンネル"));
        g.setColour(muted);g.setFont(uiFont(13));g.drawText(source,14,29,width-28,height-30,Justification::centredLeft);
    }
    void timerCallback() override {
        refreshRecording();
        if(silentPreview)for(int i=0;i<10;++i)engine.render(silentBuffer.getArrayOfWritePointers(),2,silentBuffer.getNumSamples());
        if(scanner) {
            if(!scanner->isRunning())finishScan();
            else if(Time::getMillisecondCounter()-scanStarted>20000){scanner->kill();scanner.reset();scanResult.deleteFile();scanFxTarget=-1;busy=false;inform(juce::String::fromUTF8("20秒以内に音源を確認できませんでした。別の音源をお試しください。"));}
        }
        if(engine.activeScene.load()!=observedScene||engine.selected.load()!=observedSlot)forceUi=true;
        audioStrip.refresh();if(performancePage()){liveView.refreshKeys();liveView.refresh(forceUi||testMode);}
        auto now=Time::getMillisecondCounter();if(!testMode&&!forceUi&&now<nextUiRefresh)return;forceUi=false;nextUiRefresh=now+200;
        remote.allowCommands(!busy&&!silentPreview&&(testMode||devices.getCurrentAudioDevice()!=nullptr));if(remote.enabled())publishRemote();
        if(!globalShift.isMouseButtonDown())globalShift.setValue(engine.performanceTranspose.load(),dontSendNotification);globalValue.setText(shiftNumber(engine.performanceTranspose.load())+jp(" 半音 / ")+shiftDescription(engine.performanceTranspose.load()),dontSendNotification);globalValue.setColour(Label::textColourId,engine.performanceTranspose==0?ink:Colour(0xffffbb70));
        if(!master.isMouseButtonDown())master.setValue(engine.masterValue.load(),dontSendNotification);
        int current=engine.selected.load(),activeScene=engine.activeScene.load();if(activeScene!=observedScene){observedScene=activeScene;combinationBank=activeScene>=0;if(activeScene<0)audioStrip.focus(current);else{int focus=audioStrip.focus();if(focus>=8||!engine.slots[(size_t)focus].sceneEnabled)for(int i=0;i<8;++i)if(engine.slots[(size_t)i].sceneEnabled&&engine.slots[(size_t)i].plugin){audioStrip.focus(i);break;}}int focus=juce::jlimit(0,7,audioStrip.focus());editSlot.setSelectedId(focus+1,dontSendNotification);mapSlot.setSelectedId(focus+1,dontSendNotification);resized();}
        if(current!=observedSlot){audioStrip.focus(current);observedSlot=current;editSlot.setSelectedId(current+1,dontSendNotification);}
        int loaded=0;
        for(int i=0;i<slotCount;++i) {
            auto& s=engine.slots[(size_t)i];loaded+=s.plugin!=nullptr;
            String caption=String(i+1)+"  "+(s.plugin?instrumentDisplayName(s.name):jp("音源を追加"))+"\n"+(s.plugin?shiftDescription((activeScene>=0?engine.standalone[(size_t)i].transpose:s.transpose.load())):jp("この枠を選んで追加"));
            pads[(size_t)i].setToggleState(activeScene<0&&i==current,dontSendNotification);pads[(size_t)i].setColour(TextButton::textColourOffId,instrumentColour(i));pads[(size_t)i].setColour(TextButton::textColourOnId,instrumentColour(i));pads[(size_t)i].setButtonText(caption);pads[(size_t)i].setTooltip(s.name);
            auto& scene=mixer.getScenes()[(size_t)i];int count=0;for(auto part:scene.parts)count+=part.enabled;auto& pad=pads[(size_t)(i+8)];pad.setButtonText(scene.name+"\n"+String(count)+jp("音源 / ")+(activeScene==i?jp("演奏中"):jp("押して切替")));pad.setToggleState(scene.registered&&activeScene==i,dontSendNotification);pad.setTooltip(scene.name);pad.setColour(TextButton::textColourOffId,ink);pad.setColour(TextButton::textColourOnId,ink);
            if(sceneTarget(actionBox.getSelectedId()-1))mapSlot.changeItemText(i+1,String(i+1)+" / "+engine.scenes[(size_t)i].name);
            else if(clipTarget(actionBox.getSelectedId()-1))mapSlot.changeItemText(i+1,jp("効果音 ")+String(i+1)+" / "+engine.clips[(size_t)i].data.name);
            else mapSlot.changeItemText(i+1,juce::String::fromUTF8("音色 ")+String(i+1)+(s.plugin?" / "+s.name:juce::String::fromUTF8(" / 未設定")));
        }
        for(auto* c:std::vector<Component*>{&loadButton,&editButton,&sourceTools})c->setVisible(performancePage());layoutPads();for(auto* c:std::vector<Component*>{&keyzoneButton,&demoButton,&clearButton,&editorLabel})c->setVisible(false);combinationEdit.setVisible(performancePage());combinationEdit.setEnabled(!live&&!busy);
        singleBank.setVisible(false);sceneBank.setVisible(false);
        editorLabel.setText(juce::String::fromUTF8("音色 ")+String(editSlot.getSelectedId())+juce::String::fromUTF8(" の設定"),dontSendNotification);
        if(mapSlot.getSelectedItemIndex()>=0)mapSlot.setText(mapSlot.getItemText(mapSlot.getSelectedItemIndex()),dontSendNotification);
        auto* d=devices.getCurrentAudioDevice();int midiEnabled=0;
        if(testMode||now>=nextMidiPoll){nextMidiPoll=now+1000;midiInputsEnabled=0;for(auto& input:MidiInput::getAvailableDevices())if(devices.isMidiInputDeviceEnabled(input.identifier))++midiInputsEnabled;}midiEnabled=midiInputsEnabled;
        if(!d||!midiEnabled){guideTitle.setText(juce::String::fromUTF8("1  出力機器とキーボードを接続する"),dontSendNotification);guideText.setText(juce::String::fromUTF8("右上の「接続設定」で、出力機器を選び、使うMIDI入力にチェックを入れてください。"),dontSendNotification);}
        else if(!loaded){guideTitle.setText(juce::String::fromUTF8("2  演奏する音源を入れる"),dontSendNotification);guideText.setText(juce::String::fromUTF8("下の枠を選び、「音源を追加・交換」を押します。「内蔵音源で試す」ならすぐに始められます。"),dontSendNotification);}
        else{guideTitle.setText(live?juce::String::fromUTF8("3  演奏モード — 音色ボタンで切り替え"):juce::String::fromUTF8("3  音色を選んで、鍵盤を弾く"),dontSendNotification);guideText.setText(live?juce::String::fromUTF8("音源の追加・保存はロック中です。変更したいときは、右上の「設定に戻る」を押してください。"):juce::String::fromUTF8("準備ができたらセットを保存し、右上の「演奏モードにする」で誤操作を防げます。"),dontSendNotification);}
        String audio=d?juce::String::fromUTF8("出力：")+d->getName()+"  /  "+String(engine.rate.load()/1000,1)+juce::String::fromUTF8(" kHz・")+String(engine.blockSize.load())+juce::String::fromUTF8(" サンプル"):juce::String::fromUTF8("出力機器が未接続です");
        audio+=juce::String::fromUTF8("   |   音声処理 ")+String(engine.cpuPercent.load(),1)+juce::String::fromUTF8("%   |   動作中 ")+String(engine.activeVoices.load())+"/8";
        telemetry.setText(silentPreview?jp("無音プレビュー / 音声出力・MIDI接続なし — 鍵盤を押して表示を確認できます"):audio,dontSendNotification);
        midiStatus.setText(midiEnabled?String(midiEnabled)+juce::String::fromUTF8("台のMIDI入力が有効"):juce::String::fromUTF8("MIDI入力が未設定"),dontSendNotification);
        if(activeScene>=0){guideTitle.setText(jp("組み合わせ：")+engine.scenes[(size_t)activeScene].name,dontSendNotification);guideText.setText(jp("「作った音色」で重ねた音色をまとめて選べます。重ね方や鍵盤範囲は編集ボタンから変更できます。"),dontSendNotification);}
        modeHint.setText(juce::String::fromUTF8("音源 ")+String(loaded)+juce::String::fromUTF8("/8  •  ")+(dirty?juce::String::fromUTF8("未保存の変更があります"):juce::String::fromUTF8("保存済み／変更なし")),dontSendNotification);
        int learned=engine.learned.exchange(0);
        if(learned){
            sourceKind.setSelectedId((learned>>20)&15,dontSendNotification);sourceChannel.setSelectedId(((learned>>16)&15)+2,dontSendNotification);number.setValue((learned>>8)&127,dontSendNotification);
            learnButton.setButtonText(juce::String::fromUTF8("パッド・ノブを動かして登録"));
            const auto before=mappings.size();addMapping();
            sourceSummary.setText(mappings.size()>before?juce::String::fromUTF8("登録しました。続けて別の操作も登録できます。"):juce::String::fromUTF8("登録できませんでした。下の案内を確認してください。"),dontSendNotification);
        }
        deleteButton.setEnabled(!live&&selectedMapping>=0&&selectedMapping<(int)mappings.size());
        transitionTime.setEnabled(engine.mode.load()==1);tailTime.setEnabled(engine.mode.load()==0);
        audioStrip.refresh(true);
    }
public:
    void enableSilentPreview(const File& demo){
        jassert(testMode&&!attached&&devices.getCurrentAudioDevice()==nullptr);
        if(!testMode||attached||devices.getCurrentAudioDevice()!=nullptr)return;
        silentPreview=true;audioButton.setButtonText(jp("無音プレビュー"));audioButton.setEnabled(false);audioButton.setTooltip(jp("この起動では音声デバイスを開きません。"));remoteButton.setEnabled(false);
        silentBuffer.setSize(2,240);if(demo.existsAsFile())loadSession(demo);engine.prepare(48000,240);combinationBank=engine.activeScene>=0;dirty=false;updatePage();timerCallback();inform(jp("無音プレビュー：鍵盤や音色の表示を操作できます。音は出ません。"));
    }
    int runSilentPreviewCheck(const File& report,const File& demo){
        enableSilentPreview(demo);bool quiet=devices.getCurrentAudioDevice()==nullptr&&!attached;showSettings();showRemote();bool guarded=!settings&&!remoteWindow&&!audioButton.isEnabled()&&!remoteButton.isEnabled();
        liveView.testPressKey(61,true);timerCallback();bool pressed=engine.previewKey==61&&liveView.testPreviewHighlight(61);liveView.releaseKey();timerCallback();bool released=engine.previewKey<0;
        auto* result=new DynamicObject();result->setProperty("audioDeviceOpened",!quiet);result->setProperty("hardwareAndRemoteSettingsBlocked",guarded);result->setProperty("keyboardRespondsWithoutPhysicalAudio",pressed);result->setProperty("keyboardReleasesWithoutPhysicalAudio",released);result->setProperty("clearSilentModeLabel",telemetry.getText().startsWith(jp("無音プレビュー")));report.replaceWithText(JSON::toString(var(result)));dirty=false;return quiet&&guarded&&pressed&&released?0:1;
    }
    #include "UnifiedUiChecks.inl"
    #include "VideoUiChecks.inl"
    #include "RecordingUiChecks.inl"
    #include "KeyboardChecks.inl"
    int runDesignCheck(const File& folder,const File& demo){
        folder.createDirectory();stopTimer();loadSession(demo);engine.prepare(48000,64);
        auto snapshot=[&](String name){timerCallback();auto image=createComponentSnapshot(getLocalBounds());FileOutputStream out(folder.getChildFile(name));out.setPosition(0);out.truncate();PNGImageFormat png;return png.writeImageToStream(image,out);};
        bool quiet=devices.getCurrentAudioDevice()==nullptr;
        bool embedded=Font(uiFont(16)).getTypefacePtr()->getName()=="Zen Kaku Gothic New"&&Font(uiFont(16,Font::bold)).getTypefacePtr()->getStyle().containsIgnoreCase("bold");
        engine.control(SelectScene,0);AudioBuffer<float> buffer(2,64);engine.render(buffer.getArrayOfWritePointers(),2,64);combinationBank=true;timerCallback();
        setSize(1180,860);bool rendered=snapshot(jp("PC-演奏.png"));mixerPage=true;updatePage();rendered=snapshot(jp("PC-音色作り.png"))&&rendered;
        mixerPage=false;updatePage();setSize(1366,730);rendered=snapshot(jp("PC-1366x730.png"))&&rendered;
        setSize(1120,700);bool compact=audioStrip.getBottom()<=status.getY()&&audioStrip.getY()>nextButton.getBottom()&&globalValue.getRight()<globalShift.getX();
        compact=compact&&padViewport.getBottom()<=loadButton.getY()&&padViewport.getWidth()>=372;
        rendered=snapshot(jp("PC-1120x700.png"))&&rendered;mixerPage=true;updatePage();bool scrollable=editViewport.getVerticalScrollBar().isVisible();rendered=snapshot(jp("PC-低い画面・編集.png"))&&rendered;editViewport.setViewPosition(0,580);rendered=snapshot(jp("PC-低い画面・鍵盤.png"))&&rendered;editViewport.setViewPosition(0,0);
        mixerPage=false;mappingPage=true;advanced=true;updatePage();rendered=snapshot(jp("PC-低い画面・割り当て.png"))&&rendered;editViewport.setViewPosition(0,580);rendered=snapshot(jp("PC-低い画面・割り当て下部.png"))&&rendered;editViewport.setViewPosition(0,0);
        mappingPage=false;pianoPage=true;updatePage();rendered=snapshot(jp("PC-低い画面・打ち込み.png"))&&rendered;pianoPage=false;effectsPage=true;updatePage();rendered=snapshot(jp("PC-低い画面・効果音.png"))&&rendered;
        effectsPage=false;updatePage();suspend([&]{for(int i=0;i<8;++i){if(!engine.slots[(size_t)i].plugin){auto synth=makeTestSynth();PluginDescription description;synth->fillInPluginDescription(description);putPlugin(i,std::move(synth),description);}engine.slots[(size_t)i].sceneEnabled=true;}engine.activeScene=0;});
        liveView.refresh(true);bool readable=liveView.testReadableRows(8);rendered=snapshot(jp("PC-8音源・スクロール.png"))&&rendered;
        auto* result=new DynamicObject();result->setProperty("audioDeviceOpened",!quiet);result->setProperty("embeddedRegularAndBold",embedded);result->setProperty("performanceFits1120x700",compact);result->setProperty("editorScrollsWithoutMovingMeters",scrollable);result->setProperty("eightSourceRowsKeepReadableHeight",readable);result->setProperty("screenshotsWritten",rendered);folder.getChildFile("design-tests.json").replaceWithText(JSON::toString(var(result)));dirty=false;return quiet&&embedded&&compact&&scrollable&&readable&&rendered?0:1;
    }
    int runUiTest(const File& folder,const String& probePath={}) {
        folder.createDirectory();
        suspend([&]{
            engine.sequence.pattern.notes[0]={60,0,2,100,true};engine.sequence.pattern.notes[1]={64,4,2,86,true};engine.sequence.pattern.notes[2]={67,8,4,100,true};engine.sequence.pattern.notes[3]={72,16,8,115,true};piano.sync();
            for(int i=0;i<4;++i){auto p=makeTestSynth();PluginDescription d;p->fillInPluginDescription(d);d.name=StringArray{juce::String::fromUTF8("内蔵音源 / やわらかい音"),juce::String::fromUTF8("内蔵音源 / 明るい音"),juce::String::fromUTF8("内蔵音源 / 長い余韻"),jp("内蔵音源 / 弾む音")}[i];p->getParameters()[0]->setValueNotifyingHost(i==1?0.9f:0.2f);p->getParameters()[1]->setValueNotifyingHost(i==2?0.8f:0.1f);putPlugin(i,std::move(p),d);}
            mappings={{9,10,36,Select,0,0,0,1},{9,10,37,Select,1,0,0,1},{9,10,38,Select,2,0,0,1},{9,10,39,FadeOut,0,0,0,1},{9,10,40,Restore,0,0,0,1},{9,10,41,Panic,0,0,0,1},{11,1,21,Parameter,0,0,0,1},{11,1,7,Master,0,0,0,1}};
            engine.bindings.fill({});for(size_t i=0;i<mappings.size();++i)engine.bindings[i]=mappings[i];
            engine.slots[1].layer=true;engine.slots[1].ignorePedal=true;engine.slots[1].transpose=12;engine.slots[1].gain=0.45f;engine.slots[1].trim.setCurrentAndTargetValue(0.45f);
            engine.scenes[0].name=jp("やわらかい重ね");engine.scenes[0].parts[0]={0,127,0,1,true};engine.scenes[0].parts[1]={0,127,12,.45f,true,true};
            engine.scenes[1].name=jp("低音域・高音域の重ね");engine.scenes[1].parts[0]={0,59,-12,.8f,true,true};engine.scenes[1].parts[1]={0,59,0,.35f,true,true};engine.scenes[1].parts[2]={60,127,0,.8f,true,false};engine.scenes[1].parts[3]={60,127,12,.4f,true,false};
            for(int i=0;i<slotCount;++i){auto& s=engine.slots[(size_t)i];engine.standalone[(size_t)i]={0,127,s.transpose,s.gain,s.layer,s.ignorePedal};}engine.activeScene=0;mixer.sync();
            engine.clips[0].data=makeDemoClip(0);engine.clips[1].data=makeDemoClip(1);engine.clips[1].gain=0.35f;engine.clips[1].loop=true;
            mappings.push_back({9,10,48,ClipPlay,0,0,0,1});mappings.push_back({9,10,49,ClipPlay,1,0,0,1});mappings.push_back({9,10,50,ClipStopAll,0,0,0,1});mappings.push_back({9,10,51,LayerToggle,1,0,0,1});
            auto wave=folder.getChildFile("Demo-chime.wav");wave.deleteFile();WavAudioFormat format;auto stream=wave.createOutputStream();
            if(stream){std::unique_ptr<AudioFormatWriter> writer(format.createWriterFor(stream.release(),48000,2,16,{},0));if(writer)writer->writeFromAudioSampleBuffer(engine.clips[0].data.audio,0,engine.clips[0].data.audio.getNumSamples());}
            String error;readClip(wave,engine.clips[2].data,error);
            folder.getChildFile("Demo.stagepilot").replaceWithText(JSON::toString(toJson(folder)));
        });
        loadSession(folder.getChildFile("Demo.stagepilot"));
        bool sequenceRoundTrip=piano.getPattern().notes[3].pitch==72&&engine.sequence.plugin&&engine.sequence.pattern.notes[3].length==8;
        bool sceneRoundTrip=engine.activeScene==0&&engine.scenes[1].parts[0].high==59&&engine.scenes[1].parts[2].low==60&&mixer.getScenes()[1].name==jp("低音域・高音域の重ね");
        bool pedalSettingsRoundTrip=engine.scenes[0].parts[1].ignorePedal&&engine.standalone[1].ignorePedal&&engine.slots[1].ignorePedal;
        bool sceneControls=false;mixerPage=true;updatePage();suspend([&]{sceneControls=mixer.testControls();});mixerPage=false;updatePage();
        bool editorWorks=false,wideKeys=false;suspend([&]{editorWorks=piano.testEditing();wideKeys=piano.testWideKeys();engine.prepare(48000,64);AudioBuffer<float> scratch(2,64);engine.render(scratch.getArrayOfWritePointers(),2,64);engine.control(Panic);engine.render(scratch.getArrayOfWritePointers(),2,64);});
        bool waveExport=exportSequence(folder.getChildFile("Demo-phrase.wav"),3);
        ClipData exported;String exportError;waveExport=waveExport&&readClip(folder.getChildFile("Demo-phrase.wav"),exported,exportError)&&exported.audio.getMagnitude(0,exported.audio.getNumSamples())>0&&exported.audio.getNumSamples()==336000;
        suspend([&]{folder.getChildFile("Demo.stagepilot").replaceWithText(JSON::toString(toJson(folder)));});
        bool roundTrip=engine.slots[0].plugin&&engine.slots[2].plugin&&mappings.size()==12&&std::abs(engine.slots[1].plugin->getParameters()[0]->getValue()-0.9f)<0.001f;
        bool extrasRoundTrip=engine.slots[1].layer&&engine.slots[1].transpose==12&&std::abs(engine.slots[1].gain.load()-0.45f)<0.001f&&engine.clips[0].data.builtin==0&&engine.clips[1].loop&&engine.clips[2].data.audio.getNumSamples()==24000;
        const float emptyGainAfterLoad=engine.slots[7].gain.load();
        bool legacyCompatible=false;
        {var legacy;suspend([&]{legacy=toJson(folder);});legacy.getDynamicObject()->setProperty("version",1);legacy.getDynamicObject()->removeProperty("clips");
            for(auto& slot:*legacy["slots"].getArray()){slot.getDynamicObject()->removeProperty("layer");slot.getDynamicObject()->removeProperty("transpose");slot.getDynamicObject()->removeProperty("ignorePedal");}
            auto file=folder.getChildFile("legacy-test.stagepilot");file.replaceWithText(JSON::toString(legacy));loadSession(file);legacyCompatible=engine.slots[0].plugin&&!engine.slots[1].layer&&engine.slots[1].transpose==0&&engine.clips[0].data.audio.getNumSamples()==0&&!engine.slots[1].ignorePedal;file.deleteFile();loadSession(folder.getChildFile("Demo.stagepilot"));}
        bool v04Compatible=false,invalidSceneKeepsSet=false;
        {var legacy;suspend([&]{legacy=toJson(folder);});auto* root=legacy.getDynamicObject();root->setProperty("version",3);root->removeProperty("scenes");root->removeProperty("standalone");root->removeProperty("activeScene");auto file=folder.getChildFile("v04-compatibility-test.stagepilot");file.replaceWithText(JSON::toString(legacy));loadSession(file);v04Compatible=engine.activeScene==-1&&engine.sequence.pattern.notes[3].pitch==72&&engine.slots[1].layer;file.deleteFile();loadSession(folder.getChildFile("Demo.stagepilot"));}
        {var broken;suspend([&]{broken=toJson(folder);});(*(*broken["scenes"].getArray())[0]["parts"].getArray())[0].getDynamicObject()->setProperty("low",128);auto* before=engine.slots[0].plugin.get();auto file=folder.getChildFile("invalid-scene-test.stagepilot");file.replaceWithText(JSON::toString(broken));loadSession(file);invalidSceneKeepsSet=engine.slots[0].plugin.get()==before&&mixer.getScenes()[0].parts[0].low==0;file.deleteFile();}
        bool probeOk=probePath.isEmpty(),sequenceVstExport=probePath.isEmpty(),externalSessionRoundTrip=probePath.isEmpty(),externalEditorCreated=probePath.isEmpty();
        if(probePath.isNotEmpty()) {
            editSlot.setSelectedId(4,dontSendNotification);loadPlugin(File(probePath));
            if(scanner&&scanner->waitForProcessToFinish(20000)){finishScan();probeOk=engine.slots[3].plugin!=nullptr;}
            else if(scanner){scanner->kill();scanner.reset();busy=false;scanResult.deleteFile();}
            if(probeOk){copySequenceSound(3);sequenceVstExport=engine.sequence.description.pluginFormatName==(File(probePath).hasFileExtension("dll")?"VST":"VST3")&&engine.sequence.plugin.get()!=engine.slots[3].plugin.get()&&exportSequence(folder.getChildFile("VST-sequence-probe.wav"),7);
                sequenceVstExport=sequenceVstExport&&engine.clips[7].data.audio.getMagnitude(0,engine.clips[7].data.audio.getNumSamples())>0;
                auto session=folder.getChildFile("External-plugin-test.stagepilot");var saved;float savedParameter=-1;
                suspend([&]{auto& params=engine.slots[3].plugin->getParameters();if(!params.isEmpty()){params[0]->setValue(0.61f);savedParameter=params[0]->getValue();}saved=toJson(folder);});
                session.replaceWithText(JSON::toString(saved));
                suspend([&]{engine.slots[3].plugin.reset();engine.sequence.plugin.reset();});loadSession(session);
                externalSessionRoundTrip=engine.slots[3].plugin&&File(engine.slots[3].description.fileOrIdentifier)==File(probePath)&&engine.sequence.plugin&&File(engine.sequence.description.fileOrIdentifier)==File(probePath);
                if(externalSessionRoundTrip&&savedParameter>=0)externalSessionRoundTrip=std::abs(engine.slots[3].plugin->getParameters()[0]->getValue()-savedParameter)<0.002f;
                if(externalSessionRoundTrip){suspend([&]{auto* p=engine.slots[3].plugin.get();std::unique_ptr<AudioProcessorEditor> testEditor(p->createEditorAndMakeActive());externalEditorCreated=testEditor&&testEditor->getWidth()>0&&testEditor->getHeight()>0;});}
                copySequenceSound(-1);suspend([&]{engine.clips[7].data=ClipData{};});}
        }
        MessageManager::getInstance()->runDispatchLoopUntil(80);
        const float emptyGainBeforePreview=engine.slots[7].gain.load();
        timerCallback();refreshParameters();mappingList.updateContent();inform(juce::String::fromUTF8("接続設定でキーボードを有効にし、音色ボタンを選んで弾いてみてください。"));
        auto snapshot=[&](const String& name){auto img=createComponentSnapshot(getLocalBounds(),true,1.0f);FileOutputStream stream(folder.getChildFile(name));stream.setPosition(0);stream.truncate();PNGImageFormat png;return png.writeImageToStream(img,stream);};
        liveView.refresh(true);bool quietMap=!liveView.refresh(true);for(int i=0;i<20;++i)quietMap&=!liveView.refresh(true);bool imageOk=snapshot("Preview.png")&&quietMap;
        mixerPage=true;updatePage();bool registered=mixer.registerCurrent()&&combinationBank&&performancePage()&&mixer.getScenes()[0].registered;var registeredJson;suspend([&]{engine.performanceTranspose=2;registeredJson=toJson(folder);});std::array<Scene,sceneCount> restored;registered=registered&&readScenes(registeredJson["scenes"],restored)&&restored[0].registered;
        auto registrationFile=folder.getChildFile("registration-test.stagepilot");registrationFile.replaceWithText(JSON::toString(registeredJson));loadSession(registrationFile);registered=registered&&combinationBank&&mixer.getScenes()[0].registered&&engine.performanceTranspose==2;registrationFile.deleteFile();
        suspend([&]{engine.prepare(48000,64);AudioBuffer<float> scratch(2,64);playPad(8);engine.render(scratch.getArrayOfWritePointers(),2,64);registered=registered&&engine.activeScene==0;live=true;playPad(15);live=false;engine.render(scratch.getArrayOfWritePointers(),2,64);registered=registered&&engine.activeScene==0;stepPad(1);engine.render(scratch.getArrayOfWritePointers(),2,64);registered=registered&&engine.activeScene==-1;engine.control(Panic);engine.render(scratch.getArrayOfWritePointers(),2,64);});
        suspend([&]{engine.control(SelectScene,0);AudioBuffer<float> scratch(2,64);engine.render(scratch.getArrayOfWritePointers(),2,64);});bool performancePedalButton=false;liveView.refresh(true);suspend([&]{engine.prepare(48000,64);AudioBuffer<float> scratch(2,64);performancePedalButton=liveView.testPedalButton(0);engine.render(scratch.getArrayOfWritePointers(),2,64);performancePedalButton=performancePedalButton&&engine.slots[0].ignorePedal&&mixer.getScenes()[0].parts[0].ignorePedal;});liveView.refresh(true);suspend([&]{liveView.testPedalButton(0);AudioBuffer<float> scratch(2,64);engine.render(scratch.getArrayOfWritePointers(),2,64);});
        timerCallback();snapshot(jp("作った音色.png"));setSize(1120,860);snapshot(jp("最小サイズ.png"));setSize(1180,860);performanceDetails=true;resized();snapshot(jp("演奏詳細.png"));performanceDetails=false;resized();imageOk&=registered;engine.performanceTranspose=0;globalShift.setValue(0,dontSendNotification);
        pianoPage=true;updatePage();MessageManager::getInstance()->runDispatchLoopUntil(100);snapshot(jp("打ち込み・鍵盤.png"));pianoPage=false;
        auto previewScene=[&](int index){suspend([&]{engine.control(SelectScene,index);AudioBuffer<float> scratch(2,64);engine.render(scratch.getArrayOfWritePointers(),2,64);});};
        mixer.showScene(1);mixer.registerCurrent();
        mixerPage=true;updatePage();previewScene(0);MessageManager::getInstance()->runDispatchLoopUntil(100);snapshot(jp("重ね演奏・移調.png"));
        suspend([&]{engine.prepare(48000,64);AudioBuffer<float> scratch(2,64);engine.control(SelectScene,1);engine.midi(0x90,60,100);engine.midi(0xb0,64,127);engine.render(scratch.getArrayOfWritePointers(),2,64);engine.control(SelectScene,0);engine.render(scratch.getArrayOfWritePointers(),2,64);});mixerPage=false;updatePage();timerCallback();snapshot(jp("切替中・ペダル.png"));mixerPage=true;updatePage();
        previewScene(1);MessageManager::getInstance()->runDispatchLoopUntil(100);snapshot(jp("スプリット.png"));setSize(1120,860);snapshot(jp("最小サイズ・スプリット.png"));setSize(1180,860);mixerPage=false;updatePage();timerCallback();snapshot(jp("スプリット演奏.png"));mixerPage=true;updatePage();engine.control(SelectScene,0);
        mixerPage=false;effectsPage=true;updatePage();MessageManager::getInstance()->runDispatchLoopUntil(100);snapshot(jp("効果音画面.png"));effectsPage=false;
        {ConnectionPanel connection(devices);connection.setLookAndFeel(&theme);FileOutputStream stream(folder.getChildFile(juce::String::fromUTF8("接続設定画面.png")));stream.setPosition(0);stream.truncate();PNGImageFormat png;png.writeImageToStream(connection.createComponentSnapshot(connection.getLocalBounds()),stream);connection.setLookAndFeel(nullptr);}
        mappingPage=true;updatePage();snapshot(juce::String::fromUTF8("割り当て画面.png"));advanced=true;updatePage();snapshot(juce::String::fromUTF8("詳細設定画面.png"));
        auto before=mappings.size();actionBox.setSelectedId(ClipPlay+1,dontSendNotification);mapSlot.setSelectedId(1,dontSendNotification);engine.learned=(0x99<<16)|(52<<8)|100;timerCallback();
        bool learnRegisters=mappings.size()==before+1&&mappings.back().action==ClipPlay&&mappings.back().number==52;
        bool emptySelection;select(7);emptySelection=editSlot.getSelectedId()==8&&mapSlot.getSelectedId()==8;
        bool noPlayControlsInMapping=!master.isVisible()&&!pads[0].isVisible()&&learnButton.isVisible();
        bool keyboardWorks=false,repeatsSuppressed=false;
        suspend([&]{engine.prepare(48000,64);AudioBuffer<float> silent(2,64);live=true;keyPressed(KeyPress('q'));engine.render(silent.getArrayOfWritePointers(),2,64);keyboardWorks=engine.clips[0].playing;
            keyPressed(KeyPress('q'));engine.render(silent.getArrayOfWritePointers(),2,64);int active=0;for(auto& voice:engine.clips[0].voices)active+=voice.active;repeatsSuppressed=active==1;live=false;engine.control(Panic);engine.render(silent.getArrayOfWritePointers(),2,64);});
        bool missingClipKeepsSet=false;
        {var broken; suspend([&]{broken=toJson(folder);});(*broken["clips"].getArray())[2].getDynamicObject()->setProperty("file","missing-file.wav");auto file=folder.getChildFile("missing-clip-test.stagepilot");file.replaceWithText(JSON::toString(broken));loadSession(file);missingClipKeepsSet=engine.slots[0].plugin&&engine.clips[2].data.audio.getNumSamples()==24000;file.deleteFile();}
        auto* result=new DynamicObject();result->setProperty("sessionRoundTrip",roundTrip);result->setProperty("previewRendered",imageOk);result->setProperty("registerCombinationAndPersist",registered);result->setProperty("unchangedKeyboardMapDoesNotRepaint",quietMap);
        bool browserSelected=false;
        InstrumentBrowser browser([&](File f){browserSelected=f==File("C:/VST/Test Piano.dll");},folder.getChildFile("test-folders.txt"),false);browser.setLookAndFeel(&theme);
        bool browserOk=browser.testFiltering(folder.getChildFile(jp("音源一覧テスト.png")))&&browserSelected;
        result->setProperty("instrumentBrowserSearchFilterAndSelection",browserOk);browser.setLookAndFeel(nullptr);imageOk&=browserOk;
        result->setProperty("audioDeviceOpened",devices.getCurrentAudioDevice()!=nullptr);
        result->setProperty("isolatedScanAndHostLoad",probeOk);
        result->setProperty("midiLearnRegistersChosenAction",learnRegisters);result->setProperty("oneSelectionForPlayAndEdit",emptySelection);result->setProperty("separateMappingPage",noPlayControlsInMapping);
        result->setProperty("layersTransposeClipsRoundTrip",extrasRoundTrip);result->setProperty("pcKeyboardEffectTrigger",keyboardWorks);result->setProperty("keyboardRepeatSuppressed",repeatsSuppressed);result->setProperty("missingClipKeepsCurrentSet",missingClipKeepsSet);
        result->setProperty("legacyV01V02SessionCompatible",legacyCompatible);
        result->setProperty("pianoRollSessionRoundTrip",sequenceRoundTrip);result->setProperty("pianoRollEditingAndUndo",editorWorks);result->setProperty("phraseWavExportAndEffectRegistration",waveExport);
        result->setProperty("isolatedVstSequenceExport",sequenceVstExport);
        result->setProperty("externalPluginSetRoundTrip",externalSessionRoundTrip);result->setProperty("externalPluginEditorCreated",externalEditorCreated);
        result->setProperty("pedalSettingsRoundTrip",pedalSettingsRoundTrip);result->setProperty("performancePedalButtonUpdatesScene",performancePedalButton);
        result->setProperty("independentCombinationsRoundTrip",sceneRoundTrip);result->setProperty("splitPresetAndHardwareRangeLearn",sceneControls);
        result->setProperty("wideKeyboardAndAutoScroll",wideKeys);result->setProperty("v04SessionCompatible",v04Compatible);result->setProperty("invalidSceneKeepsCurrentSet",invalidSceneKeepsSet);
        result->setProperty("emptyGainAfterLoad",emptyGainAfterLoad);result->setProperty("emptyGainBeforePreview",emptyGainBeforePreview);
        bool commonMeters=true,commonReset=false,globalResets=true,editorResets=false,effectReset=false,pianoResets=false,sourceFollowsEditor=false,gainPersistence=false;
        var controlsBefore;suspend([&]{controlsBefore=toJson(folder);});
        for(auto test:std::array<std::tuple<Slider*,double,double>,7>{{{&globalShift,7,0},{&fadeTime,3,1.5},{&transitionTime,180,80},{&tailTime,12,8},{&number,21,36},{&rangeMin,20,0},{&rangeMax,50,100}}}){auto* slider=std::get<0>(test);if(slider==&transitionTime||slider==&tailTime){transition.setSelectedId(slider==&transitionTime?2:1,sendNotificationSync);timerCallback();}slider->setValue(std::get<1>(test),sendNotificationSync);simulateDoubleClick(*slider);globalResets=globalResets&&slider->getValue()==std::get<2>(test);}
        globalShift.setValue(9,sendNotificationSync);globalResets=globalShift.testValueDoubleClick()&&globalResets;globalShift.setEnabled(false);globalShift.setValue(5,dontSendNotification);simulateDoubleClick(globalShift);globalResets=globalResets&&globalShift.getValue()==5;globalShift.setEnabled(true);simulateDoubleClick(globalShift);auto controlsFile=folder.getChildFile("controls-before.stagepilot");controlsFile.replaceWithText(JSON::toString(controlsBefore));
        mixerPage=true;mappingPage=effectsPage=pianoPage=false;updatePage();mixer.inspectSource(1);sourceFollowsEditor=audioStrip.focus()==1;
        suspend([&]{commonReset=audioStrip.testReset();AudioBuffer<float> scratch(2,64);engine.render(scratch.getArrayOfWritePointers(),2,64);commonReset=commonReset&&engine.masterValue==.7f&&engine.slots[1].gain==1&&mixer.getScenes()[(size_t)engine.activeScene.load()].parts[1].gain==1;editorResets=mixer.testResets();effectReset=effects.testReset();pianoResets=piano.testResets();});
        suspend([&]{audioStrip.onGain(1,.42f);audioStrip.onGain(8,.55f);AudioBuffer<float> scratch(2,64);engine.render(scratch.getArrayOfWritePointers(),2,64);});var gains;suspend([&]{gains=toJson(folder);});auto gainsFile=folder.getChildFile("gain-controls.stagepilot");gainsFile.replaceWithText(JSON::toString(gains));loadSession(gainsFile);gainPersistence=std::abs(engine.slots[1].gain.load()-.42f)<.001f&&std::abs(mixer.getScenes()[(size_t)engine.activeScene.load()].parts[1].gain-.42f)<.001f&&std::abs(engine.sequence.gain.load()-.55f)<.001f;gainsFile.deleteFile();
        loadSession(controlsFile);controlsFile.deleteFile();
        suspend([&]{engine.prepare(48000,64);AudioBuffer<float> scratch(2,64);engine.midi(0x90,60,105);engine.midi(0x90,48,105);for(int n=0;n<100;++n)engine.render(scratch.getArrayOfWritePointers(),2,64);});
        for(int page=0;page<5;++page){mappingPage=page==4;mixerPage=page==1;effectsPage=page==2;pianoPage=page==3;updatePage();audioStrip.refresh(true);commonMeters=commonMeters&&audioStrip.isVisible()&&audioStrip.getBounds().getBottom()<=status.getY()&&audioStrip.getWidth()>900;timerCallback();snapshot(jp("共通メーター-")+String(page+1)+".png");}
        suspend([&]{engine.control(Panic);AudioBuffer<float> scratch(2,64);engine.render(scratch.getArrayOfWritePointers(),2,64);});
        result->setProperty("commonMetersVisibleOnAllFivePages",commonMeters);result->setProperty("doubleClickVolumeKnobs",commonReset);result->setProperty("doubleClickGlobalAndMappingValues",globalResets);result->setProperty("doubleClickEditorValues",editorResets);result->setProperty("doubleClickEffectVolume",effectReset);result->setProperty("doubleClickPianoValues",pianoResets);result->setProperty("selectedMeterFollowsEditor",sourceFollowsEditor);result->setProperty("sourceAndSequenceGainPersist",gainPersistence);
        bool builderFlow=true,builderVst=probePath.isEmpty(),clickAudition=true,performanceAudition=false,previewLeavesPage=false;
        var builderBefore;suspend([&]{builderBefore=toJson(folder);});auto builderFile=folder.getChildFile("builder-before.stagepilot");builderFile.replaceWithText(JSON::toString(builderBefore));
        mixerPage=true;mappingPage=effectsPage=pianoPage=false;updatePage();mixer.showScene(7);
        installDemo({4,7,-1});builderFlow=mixerPage&&mixer.getScenes()[7].parts[4].enabled&&mixer.editingScene()==7;
        mixer.testEditPart({0,59,-12,.8f,true,false,true});installDemo({5,7,4});builderFlow=builderFlow&&mixerPage&&mixer.getScenes()[7].parts[5].enabled&&mixer.getScenes()[7].parts[5].high==59;mixer.testEditPart({0,59,12,.5f,true,false,true});
        if(probePath.isNotEmpty()){loadPlugin(File(probePath),{6,7,4});if(scanner&&scanner->waitForProcessToFinish(20000))finishScan();else if(scanner){scanner->kill();scanner.reset();busy=false;scanResult.deleteFile();}builderVst=engine.slots[6].plugin&&File(engine.slots[6].description.fileOrIdentifier)==File(probePath)&&mixerPage&&mixer.getScenes()[7].parts[6].enabled&&mixer.getScenes()[7].parts[6].high==59;}
        suspend([&]{engine.prepare(48000,64);AudioBuffer<float> scratch(2,64);mixer.testPressKey(48);for(int n=0;n<20;++n)engine.render(scratch.getArrayOfWritePointers(),2,64);clickAudition=engine.previewKey==48&&engine.activeScene==7&&engine.slots[4].meter.read().rawPeak[0]>0&&engine.slots[5].meter.read().rawPeak[0]>0;if(probePath.isNotEmpty())builderVst=builderVst&&engine.slots[6].meter.read().rawPeak[0]>0;});
        timerCallback();snapshot(jp("音色作り・試聴.png"));setSize(1120,860);snapshot(jp("最小サイズ・音色作り.png"));setSize(1180,860);
        suspend([&]{AudioBuffer<float> scratch(2,64);mixer.testPressKey(49,true,true);engine.render(scratch.getArrayOfWritePointers(),2,64);clickAudition=clickAudition&&engine.previewKey==49;mixer.releaseKey();engine.render(scratch.getArrayOfWritePointers(),2,64);clickAudition=clickAudition&&engine.previewKey==-1;mixer.testPressKey(72);engine.render(scratch.getArrayOfWritePointers(),2,64);clickAudition=clickAudition&&engine.previewKey==-1&&engine.slots[4].midi.getNumEvents()==0;});
        mixer.releaseKey();mixer.onSource(4);builderFlow=builderFlow&&mixerPage&&editor!=nullptr;closeEditor();builderFlow=mixer.registerCurrent()&&builderFlow&&performancePage()&&combinationBank&&mixer.getScenes()[7].registered;
        var combined;suspend([&]{combined=toJson(folder);});auto combinedFile=folder.getChildFile("builder-combination.stagepilot");combinedFile.replaceWithText(JSON::toString(combined));loadSession(combinedFile);builderFlow=builderFlow&&mixer.getScenes()[7].registered&&mixer.getScenes()[7].parts[4].transpose==-12&&mixer.getScenes()[7].parts[5].transpose==12;combinedFile.deleteFile();
        suspend([&]{engine.prepare(48000,64);AudioBuffer<float> scratch(2,64);playPad(15);engine.render(scratch.getArrayOfWritePointers(),2,64);liveView.testPressKey(48);for(int n=0;n<20;++n)engine.render(scratch.getArrayOfWritePointers(),2,64);performanceAudition=engine.previewKey==48&&std::isfinite(scratch.getMagnitude(0,64))&&scratch.getMagnitude(0,64)>0;});
        liveView.refreshKeys();timerCallback();performanceAudition=performanceAudition&&liveView.testPreviewHighlight(48);snapshot(jp("登録した音色・試聴.png"));
        mixerPage=true;updatePage();suspend([&]{AudioBuffer<float> scratch(2,64);engine.render(scratch.getArrayOfWritePointers(),2,64);previewLeavesPage=engine.previewKey==-1;});loadSession(builderFile);builderFile.deleteFile();
        result->setProperty("builderAddLayerAuditionRegisterRoundTrip",builderFlow);result->setProperty("builderVstAdditionAndAudition",builderVst);result->setProperty("builderWhiteBlackDragReleaseAndSplitAudition",clickAudition);result->setProperty("performanceKeyboardAudition",performanceAudition);result->setProperty("leavingKeyboardPageReleasesPreview",previewLeavesPage);
        StringArray backends;for(auto* t:devices.getAvailableDeviceTypes())backends.add(t->getTypeName());result->setProperty("audioBackends",backends.joinIntoString(", "));
        {auto config=folder.getChildFile("remote-ui-test.json");config.replaceWithText("{\"port\":18760,\"token\":\"stagepilot-test\"}");RemotePanel remotePanel(remote,config);remotePanel.setLookAndFeel(&theme);PNGImageFormat png;FileOutputStream stream(folder.getChildFile(jp("スマホ連携.png")));png.writeImageToStream(remotePanel.createComponentSnapshot(remotePanel.getLocalBounds()),stream);remotePanel.setLookAndFeel(nullptr);config.deleteFile();result->setProperty("remoteButtonVisibleAndSilentUi",remoteButton.isVisible()&&devices.getCurrentAudioDevice()==nullptr);}
        folder.getChildFile("ui-tests.json").replaceWithText(JSON::toString(var(result)));dirty=false;return builderFlow&&builderVst&&clickAudition&&performanceAudition&&previewLeavesPage&&commonMeters&&commonReset&&globalResets&&editorResets&&effectReset&&pianoResets&&sourceFollowsEditor&&gainPersistence&&pedalSettingsRoundTrip&&performancePedalButton&&roundTrip&&imageOk&&probeOk&&learnRegisters&&emptySelection&&noPlayControlsInMapping&&extrasRoundTrip&&keyboardWorks&&repeatsSuppressed&&missingClipKeepsSet&&legacyCompatible&&sequenceRoundTrip&&editorWorks&&waveExport&&sequenceVstExport&&sceneRoundTrip&&sceneControls&&wideKeys&&v04Compatible&&invalidSceneKeepsSet&&externalSessionRoundTrip&&externalEditorCreated?0:1;
    }
    Main(bool testing=false) : testMode(testing) {
        setLookAndFeel(&theme);addPluginFormats(formats);
        setWantsKeyboardFocus(true);
        addAndMakeVisible(recordButton);recordButton.setButtonText(jp("演奏を録音"));recordButton.setColour(TextButton::buttonOnColourId,Colour(0xff843e4b));
        recordButton.setTooltip(jp("最終出力をWAVに録音。もう一度押すと停止・保存。右端の▼か右クリックで、カメラ付き録画と保存先を選べます。"));
        recordButton.onClick=[this]{if(engine.movie.isBusy())engine.movie.requestStop();else if(engine.recorder.isCapturing())engine.recorder.requestStop();else if(!engine.recorder.isBusy())startRecording();refreshRecording();};
        recordButton.options=[this]{showRecordingOptions();};
        liveButton.setButtonText(juce::String::fromUTF8("演奏モードにする"));audioButton.setButtonText(juce::String::fromUTF8("接続設定"));saveButton.setButtonText(juce::String::fromUTF8("セットを保存"));openButton.setButtonText(juce::String::fromUTF8("セットを開く"));
        loadButton.setButtonText(juce::String::fromUTF8("音源一覧から追加"));demoButton.setButtonText(juce::String::fromUTF8("内蔵音源で試す"));editButton.setButtonText(juce::String::fromUTF8("音源の画面を開く"));clearButton.setButtonText(juce::String::fromUTF8("音源を外す"));
        nextButton.setButtonText(juce::String::fromUTF8("次の音色  →"));prevButton.setButtonText(juce::String::fromUTF8("←  前の音色"));fadeButton.setButtonText(juce::String::fromUTF8("ゆっくり消音"));restoreButton.setButtonText(juce::String::fromUTF8("音量を戻す"));panicButton.setButtonText(juce::String::fromUTF8("すべての音を止める"));
        learnButton.setButtonText(juce::String::fromUTF8("パッド・ノブを動かして登録"));addButton.setButtonText(juce::String::fromUTF8("この番号で登録"));deleteButton.setButtonText(juce::String::fromUTF8("選んだ割り当てを削除"));
        button(liveButton,[&]{
            if(busy){inform(juce::String::fromUTF8("音源の確認が終わるまでお待ちください。"));return;}
            bool loaded=false;for(auto& s:engine.slots)loaded|=s.plugin!=nullptr;for(auto& c:engine.clips)loaded|=c.data.audio.getNumSamples()>0;for(auto n:piano.getPattern().notes)loaded|=n.enabled;
            if(!live&&!loaded){inform(juce::String::fromUTF8("先に音源を追加してください。「内蔵音源で試す」でも始められます。"));return;}
            live=!live;liveButton.setToggleState(live,dontSendNotification);liveButton.setButtonText(live?juce::String::fromUTF8("設定に戻る"):juce::String::fromUTF8("演奏モードにする"));
            if(auto* panel=fxPanel())panel->setLocked(live);effects.setLocked(live);piano.setLocked(live);mixer.setLocked(live);
            if(live){mixerPage=pianoPage=effectsPage=false;library.reset();engine.learning=false;learnButton.setButtonText(juce::String::fromUTF8("パッド・ノブを動かして登録"));settings.reset();closeEditor();mappingPage=false;}
            for(auto* c:std::vector<Component*>{&combinationEdit,&loadButton,&keyzoneButton,&demoButton,&editButton,&clearButton,&openButton,&saveButton,&audioButton,&learnButton,&addButton,&deleteButton,&actionBox,&mapSlot,&paramBox,&advancedButton})c->setEnabled(!live);
            updatePage();grabKeyboardFocus();
            inform(live?juce::String::fromUTF8("演奏モード：1〜8で音色選択、← →で前後、Fで消音、Rで音量復帰、Escで全音停止。"):juce::String::fromUTF8("設定モード：音源の追加や保存ができます。これらの操作中は音声が一時停止します。"));
        });
        button(audioButton,[&]{showSettings();});button(saveButton,[&]{saveSession();});button(openButton,[&]{chooseSession();});button(remoteButton,[&]{showRemote();});
        addAndMakeVisible(mixer);addAndMakeVisible(effects);addAndMakeVisible(piano);
        button(playTab,[&]{int focus=engine.selected.load();if(engine.activeScene>=0){for(int i=0;i<8;++i)if(engine.slots[(size_t)i].sceneEnabled&&engine.slots[(size_t)i].plugin){focus=i;break;}}audioStrip.focus(focus);editSlot.setSelectedId(focus+1,dontSendNotification);mapSlot.setSelectedId(focus+1,dontSendNotification);pianoPage=false;mappingPage=false;mixerPage=false;effectsPage=false;engine.learning=false;engine.learned=0;learnButton.setButtonText(juce::String::fromUTF8("パッド・ノブを動かして登録"));updatePage();});
        button(mapTab,[&]{pianoPage=false;mappingPage=true;mixerPage=false;effectsPage=false;updatePage();});
        button(mixerTab,[&]{pianoPage=false;mappingPage=false;mixerPage=true;effectsPage=false;engine.learning=false;engine.learned=0;updatePage();});
        button(effectsTab,[&]{pianoPage=false;mappingPage=false;mixerPage=false;effectsPage=true;engine.learning=false;engine.learned=0;updatePage();});
        button(pianoTab,[&]{pianoPage=true;mappingPage=mixerPage=effectsPage=false;engine.learning=false;engine.learned=0;updatePage();});
        button(advancedButton,[&]{advanced=!advanced;updatePage();});
        button(diagnosticsButton,[&]{
            auto* d=devices.getCurrentAudioDevice();
            String text=juce::String::fromUTF8("1バッファーの時間：")+String(engine.blockSize.load()*1000.0/engine.rate.load(),2)+" ms\n";
            if(d)text+=juce::String::fromUTF8("機器が報告する出力遅延：")+String(d->getOutputLatencyInSamples()*1000.0/engine.rate.load(),2)+" ms\n";
            if(auto* p=engine.slots[(size_t)engine.selected.load()].plugin.get())text+=juce::String::fromUTF8("音源が報告する内部遅延：")+String(p->getLatencySamples()*1000.0/engine.rate.load(),2)+" ms\n";
            text+=juce::String::fromUTF8("音声処理の負荷：")+String(engine.cpuPercent.load(),1)+juce::String::fromUTF8(" %\n入力の取りこぼし：")+String(engine.dropped.load());
            if(d)text+=juce::String::fromUTF8("\n音切れの報告（Xrun）：")+(d->getXRunCount()<0?String(juce::String::fromUTF8("取得できません")):String(d->getXRunCount()));
            text+=juce::String::fromUTF8("\n\nこれらは鍵盤から発音までの実測値ではありません。バッファーは「接続設定」で変更できます。");
            AlertWindow::showAsync(MessageBoxOptions().withTitle(juce::String::fromUTF8("遅延・負荷の詳細")).withMessage(text).withButton(juce::String::fromUTF8("閉じる")),[](int){});
        });
        addAndMakeVisible(padViewport);padViewport.setViewedComponent(&padCanvas,false);padViewport.setScrollBarsShown(true,false);padViewport.setScrollBarThickness(8);
        for(int i=0;i<16;++i){button(pads[(size_t)i],[&,i]{playPad(i);});padCanvas.addAndMakeVisible(pads[(size_t)i]);pads[(size_t)i].getProperties().set("stagePad",true);pads[(size_t)i].setWantsKeyboardFocus(false);pads[(size_t)i].setTooltip(juce::String::fromUTF8("クリックすると、この音色を演奏・編集できます。"));}
        for(int i=0;i<slotCount;++i)editSlot.addItem(juce::String::fromUTF8("音色 ")+String(i+1),i+1);editSlot.setSelectedId(1);
        button(loadButton,[&]{showLibrary();});button(demoButton,[&]{installDemo();});button(editButton,[&]{showEditor();});
        button(keyzoneButton,[&]{choosePlugin();});
        keyzoneButton.setTooltip(jp("音源ファイルの場所が分かっている場合に、直接選んで追加します。"));
        button(clearButton,[&]{
            if(!editable())return;int index=editSlot.getSelectedId()-1;if(!engine.slots[(size_t)index].plugin)return;
            AlertWindow::showAsync(MessageBoxOptions().withTitle(juce::String::fromUTF8("この音源を外しますか？")).withMessage(juce::String::fromUTF8("音色 ")+String(index+1)+juce::String::fromUTF8(" の音源と、その音源のパラメーター割り当てを外します。")).withButton(juce::String::fromUTF8("音源を外す")).withButton(juce::String::fromUTF8("キャンセル")),
                [safe=Component::SafePointer<Main>(this),index](int result){if(safe&&result==1&&safe->editable()){safe->closeEditor();safe->suspend([&]{safe->putPlugin(index,nullptr,{});safe->mappings.erase(std::remove_if(safe->mappings.begin(),safe->mappings.end(),[&](const Mapping& m){return m.slot==index&&m.action==Parameter;}),safe->mappings.end());});safe->refreshParameters();safe->mappingList.updateContent();safe->changed();}});
        });
        button(prevButton,[&]{stepPad(-1);});button(nextButton,[&]{stepPad(1);});
        mixer.onNewSource=[this](Component& anchor,int inherit){addBuilderSource(anchor,inherit);};
        mixer.onSource=[this](int i){if(!editable())return;if(!engine.slots[(size_t)i].plugin){addBuilderSource(mixer,-1);return;}editSlot.setSelectedId(i+1,dontSendNotification);mapSlot.setSelectedId(i+1,dontSendNotification);audioStrip.focus(i);refreshParameters();showEditor();};
        button(combinationEdit,[&]{soundMenu();});
        button(singleBank,[&]{combinationBank=false;forceUi=true;updatePage();timerCallback();});button(sceneBank,[&]{combinationBank=true;forceUi=true;updatePage();timerCallback();});
        mixer.onAddToPerformance=[this](int i){combinationBank=true;mixerPage=effectsPage=pianoPage=mappingPage=false;engine.control(SelectScene,i);changed();updatePage();timerCallback();inform(jp("作った音色を演奏画面に追加しました。「セットを保存」で次回も使えます。"));};
        button(fadeButton,[&]{engine.control(FadeOut);inform(juce::String::fromUTF8("設定した時間で消音しています。「音量を戻す」で復帰できます。"));});
        button(restoreButton,[&]{engine.control(Restore);inform(juce::String::fromUTF8("設定した音量へ戻しました。"));});
        button(panicButton,[&]{engine.control(Panic);inform(juce::String::fromUTF8("すべての音に停止指示を送りました。鍵盤を弾くと再び演奏できます。"));});
        panicButton.setColour(TextButton::buttonColourId,Colour(0xff713b40));fadeButton.setColour(TextButton::buttonColourId,Colour(0xff405274));
        loadButton.setColour(TextButton::buttonColourId,Colour(0xff286448));learnButton.setColour(TextButton::buttonColourId,Colour(0xff286448));
        addAndMakeVisible(transition);transition.addItem(juce::String::fromUTF8("余韻を残して切り替える"),1);transition.addItem(juce::String::fromUTF8("音を重ねて切り替える"),2);transition.addItem(juce::String::fromUTF8("すぐに切り替える（5ms）"),3);transition.setSelectedId(1);
        transition.onChange=[&]{engine.mode=transition.getSelectedId()-1;changed();};
        slider(master,0,1,0.01,0.7);master.textFromValueFunction=[](double value){return String(roundToInt(value*100))+" %";};
        master.valueFromTextFunction=[](const String& value){return value.getDoubleValue()/100.0;};
        master.updateText();
        master.onValueChange=[&]{engine.control(Master,0,(float)master.getValue());changed();};
        slider(fadeTime,0.01,20,0.01,1.5,juce::String::fromUTF8(" 秒"));fadeTime.onValueChange=[&]{engine.fadeSeconds=(float)fadeTime.getValue();changed();};
        slider(transitionTime,5,2000,1,80," ms");transitionTime.onValueChange=[&]{engine.transitionSeconds=(float)transitionTime.getValue()/1000;changed();};
        slider(tailTime,0.1,30,0.1,8,juce::String::fromUTF8(" 秒"));tailTime.onValueChange=[&]{engine.tailSeconds=(float)tailTime.getValue();changed();};
        addAndMakeVisible(audioStrip);audioStrip.onMaster=[this](float value){engine.control(Master,0,value);changed();};audioStrip.onGainBegin=[this]{if(engine.activeScene>=0)mixer.beginGainEdit();};audioStrip.onGain=[this](int i,float value){if(i==8){engine.control(SequenceGain,0,value);changed();return;}int scene=engine.activeScene.load();if(scene>=0)mixer.setSceneGain(scene,i,value);else{engine.control(SlotGain,i,value);changed();}};mixer.onFocus=[this](int i){audioStrip.focus(i);};liveView.onFocus=[this](int i){audioStrip.focus(i);editSlot.setSelectedId(i+1,dontSendNotification);};
        liveView.onPedalChange=[this](int i,bool ignore){if(busy)return;int scene=engine.activeScene.load();if(scene>=0)mixer.setScenePedal(scene,i,ignore);else{engine.control(SetIgnorePedal,i,ignore?1.0f:0.0f);changed();}inform(ignore?jp("この音源はサステインペダルを使いません。"):jp("この音源でサステインペダルを使います。"));};
        addAndMakeVisible(liveView);style(globalTitle,jp("全体をトランスポーズ"),17);style(globalValue,"",21);
        slider(globalShift,-24,24,1,0);globalShift.setSliderStyle(Slider::IncDecButtons);globalShift.setTextBoxStyle(Slider::TextBoxLeft,false,62,36);globalShift.textFromValueFunction=[](double v){return shiftNumber((int)v);};globalShift.onValueChange=[this]{engine.control(PerformanceTranspose,0,(float)globalShift.getValue());changed();};
        button(fxButton,[this]{fxMenu();});
        button(shiftReset,[this]{globalShift.setValue(0);});button(detailButton,[this]{performanceDetails=!performanceDetails;detailButton.setButtonText(performanceDetails?jp("詳細設定を閉じる"):jp("切替・消音の設定"));resized();});
        button(sourceTools,[this]{if(!editable())return;PopupMenu menu;menu.addItem(1,jp("内蔵音源で試す"));menu.addItem(2,jp("ファイルから選ぶ"));menu.addItem(3,jp("この音源を外す"));menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&sourceTools),[safe=Component::SafePointer<Main>(this)](int result){if(!safe)return;if(result==1)safe->installDemo();if(result==2)safe->choosePlugin();if(result==3)safe->clearButton.onClick();});});
        style(editorLabel,juce::String::fromUTF8("音色 1 の設定"),16);style(masterLabel,juce::String::fromUTF8("全体の音量"),16);style(fadeLabel,juce::String::fromUTF8("消音までの時間"),14);style(transitionLabel,juce::String::fromUTF8("音を重ねる時間"),14);style(tailLabel,juce::String::fromUTF8("切替前の音の上限"),14);
        style(telemetry,"",13);style(status,juce::String::fromUTF8("まず「接続設定」で出力機器とキーボードを選んでください。"),14);style(midiStatus,"",14);
        style(guideTitle,"",18);style(guideText,"",15);style(modeHint,"",14);
        status.setColour(Label::textColourId,mint);telemetry.setColour(Label::textColourId,muted);midiStatus.setColour(Label::textColourId,muted);
        style(mappingTitle,juce::String::fromUTF8("1  割り当てたい操作を選ぶ"),21);style(mappingHelp,juce::String::fromUTF8("音色の切り替えも、シンセのつまみ操作も登録できます。"),14);
        style(learnHelp,juce::String::fromUTF8("2  下のボタンを押して、\n    実機のパッドを押すか、ノブを動かす"),17);
        style(sourceSummary,juce::String::fromUTF8("入力を受け取ると自動で登録します。"),14);sourceSummary.setColour(Label::textColourId,mint);
        style(savedTitle,juce::String::fromUTF8("登録済みの割り当て"),20);
        style(sourceLabel,juce::String::fromUTF8("入力の種類"),13);style(channelLabel,juce::String::fromUTF8("MIDIチャンネル"),13);style(numberLabel,juce::String::fromUTF8("入力番号"),13);
        style(actionLabel,juce::String::fromUTF8("やりたいこと"),14);style(slotLabel,juce::String::fromUTF8("操作する音色"),14);style(paramLabel,juce::String::fromUTF8("動かすパラメーター"),14);style(rangeLabel,juce::String::fromUTF8("操作範囲：下限 → 上限（%）"),13);
        for(auto* c:{&sourceKind,&sourceChannel,&actionBox,&mapSlot,&paramBox})addAndMakeVisible(c);
        sourceKind.addItem(juce::String::fromUTF8("パッド・鍵盤"),9);sourceKind.addItem(juce::String::fromUTF8("ノブ・フェーダー"),11);sourceKind.addItem(juce::String::fromUTF8("音色変更信号"),12);sourceKind.setSelectedId(9);
        sourceChannel.addItem(juce::String::fromUTF8("すべて"),1);for(int i=1;i<=16;++i)sourceChannel.addItem(juce::String::fromUTF8("チャンネル ")+String(i),i+1);sourceChannel.setSelectedId(1);
        StringArray actions{juce::String::fromUTF8("指定した音色に切り替える"),juce::String::fromUTF8("次の音色に切り替える"),juce::String::fromUTF8("前の音色に切り替える"),juce::String::fromUTF8("ゆっくり消音する"),juce::String::fromUTF8("音量を元に戻す"),juce::String::fromUTF8("すべての音を止める"),juce::String::fromUTF8("全体の音量を動かす"),juce::String::fromUTF8("この音色の音量を動かす"),juce::String::fromUTF8("シンセのパラメーターを動かす"),jp("この音色を重ねる・解除する"),jp("効果音を鳴らす"),jp("この効果音を止める"),jp("効果音をすべて止める"),jp("移調（内部操作）"),jp("重ね演奏（内部操作）"),jp("打ち込みを先頭から再生"),jp("打ち込みを停止"),jp("組み合わせを切り替える"),jp("次の組み合わせへ"),jp("前の組み合わせへ")};
        for(int i=0;i<actions.size();++i)if(i!=SetTranspose&&i!=SetLayer)actionBox.addItem(actions[i],i+1);actionBox.setSelectedId(1);
        actionBox.onChange=[&]{engine.learning=false;learnButton.setButtonText(juce::String::fromUTF8("パッド・ノブを動かして登録"));updatePage();};
        for(int i=0;i<slotCount;++i)mapSlot.addItem(juce::String::fromUTF8("音色 ")+String(i+1),i+1);mapSlot.setSelectedId(1);mapSlot.onChange=[&]{engine.learning=false;if(instrumentTarget(actionBox.getSelectedId()-1))audioStrip.focus(mapSlot.getSelectedId()-1);refreshParameters();};
        slider(number,0,127,1,36);slider(rangeMin,0,100,1,0);slider(rangeMax,0,100,1,100);
        button(learnButton,[&]{
            if(!editable())return;bool arm=!engine.learning.load();
            auto target=currentMapping();
            if(arm&&instrumentTarget(target.action)&&!engine.slots[(size_t)target.slot].plugin){inform(juce::String::fromUTF8("先に「演奏」で、操作したい音色に音源を追加してください。"));return;}
            if(arm&&clipTarget(target.action)&&engine.clips[(size_t)target.slot].data.audio.getNumSamples()==0){inform(jp("先に「効果音」で、使う効果音を追加してください。"));return;}
            if(arm&&target.action==Parameter&&target.parameter<0){inform(juce::String::fromUTF8("動かしたいパラメーターを選んでください。"));return;}
            engine.learned=0;engine.learning=arm;learnButton.setButtonText(arm?juce::String::fromUTF8("入力を待っています… 押すと中止"):juce::String::fromUTF8("パッド・ノブを動かして登録"));
            sourceSummary.setText(arm?juce::String::fromUTF8("実機のパッドを押すか、ノブを動かしてください。"):juce::String::fromUTF8("登録を中止しました。"),dontSendNotification);
        });
        button(addButton,[&]{addMapping();});button(deleteButton,[&]{if(!editable()||selectedMapping<0||selectedMapping>=(int)mappings.size())return;
            mappings.erase(mappings.begin()+selectedMapping);suspend([&]{});selectedMapping=-1;mappingList.deselectAllRows();mappingList.updateContent();changed();inform(juce::String::fromUTF8("選んだ割り当てを削除しました。"));});
        addAndMakeVisible(mappingList);mappingList.setRowHeight(56);mappingList.setOutlineThickness(0);
        loadButton.setTooltip(juce::String::fromUTF8("選んだ音色にVST2（.dll）またはVST3音源を入れます。64bit版に対応。読み込み中は音声が一時停止します。"));
        demoButton.setTooltip(juce::String::fromUTF8("VSTを用意しなくても使える内蔵シンセを追加します。"));
        editButton.setTooltip(juce::String::fromUTF8("選んだ音源の画面を開いて、プリセットやつまみを操作します。"));
        liveButton.setTooltip(juce::String::fromUTF8("音源の交換などをロックし、演奏中の誤操作を防ぎます。"));
        master.setTooltip(juce::String::fromUTF8("すべての音源をまとめた出力音量です。"));
        if(!testMode){auto xml=parseXML(settingsFile());auto error=devices.initialise(0,2,xml.get(),true);if(error.isNotEmpty())inform(juce::String::fromUTF8("接続エラー：")+error);
        devices.addMidiInputDeviceCallback({},&engine);devices.addAudioCallback(&engine);attached=true;}
        canvasControls={&mixer,&effects,&piano,&mappingTitle,&mappingHelp,&actionLabel,&actionBox,&slotLabel,&mapSlot,&paramLabel,&paramBox,&learnHelp,&learnButton,&sourceSummary,&advancedButton,&sourceLabel,&channelLabel,&numberLabel,&sourceKind,&sourceChannel,&number,&rangeLabel,&rangeMin,&rangeMax,&addButton,&savedTitle,&mappingList,&deleteButton};
        addAndMakeVisible(editViewport);editViewport.setViewedComponent(&editCanvas,false);editViewport.setScrollBarsShown(true,false);editViewport.setScrollBarThickness(12);
        for(auto* c:canvasControls)editCanvas.addChildComponent(c);
        editCanvas.draw=[this](Graphics& g){if(mappingPage){g.setColour(panel);g.fillRoundedRectangle(0,0,548,580,10);g.fillRoundedRectangle(568,0,editCanvas.getWidth()-568.0f,580,10);}};
        setSize(1180,820);updatePage();startTimerHz(20);
    }    ~Main() override {fxWindow.reset();fxEditor.reset();padViewport.setViewedComponent(nullptr,false);cameraWindow.reset();cameraPreview.requestStop();engine.movie.requestStop();engine.recorder.requestStop(Recorder::Reason::Shutdown);stopTimer();remoteWindow.reset();remote.stop();library.reset();if(scanner){scanner->kill();scanner.reset();scanResult.deleteFile();}settings.reset();closeEditor();saveAudio();devices.removeMidiInputDeviceCallback({},&engine);if(attached)devices.removeAudioCallback(&engine);engine.recorder.finish();engine.movie.finish();cameraPreview.finish();setLookAndFeel(nullptr);}
    void requestQuit() {
        if(engine.recorder.isBusy()||engine.movie.isBusy()||cameraPreview.isBusy()){quitAfterRecording=true;engine.recorder.requestStop();engine.movie.requestStop();cameraPreview.requestStop();inform(jp("録音・録画を保存してから終了します…"));return;}
        if(!dirty){JUCEApplication::getInstance()->quit();return;}
        AlertWindow::showAsync(MessageBoxOptions().withTitle(juce::String::fromUTF8("StagePilotを終了しますか？")).withMessage(juce::String::fromUTF8("まだ保存していない変更があります。")).withButton(juce::String::fromUTF8("保存せずに終了")).withButton(juce::String::fromUTF8("キャンセル")),[](int r){if(r==1)JUCEApplication::getInstance()->quit();});
    }
    bool keyPressed(const KeyPress& k) override {
        if(k.getKeyCode()==KeyPress::escapeKey){engine.control(Panic);return true;}
        if(pianoPage&&k.getKeyCode()==KeyPress::spaceKey&&dynamic_cast<TextEditor*>(Component::getCurrentlyFocusedComponent())==nullptr){piano.togglePlay();return true;}
        if(!live)return false;
        if(dynamic_cast<TextEditor*>(Component::getCurrentlyFocusedComponent())!=nullptr)return false;
        if(effects.triggerKey(k))return true;
        if(k.getTextCharacter()>='1'&&k.getTextCharacter()<='8'){playPad(k.getTextCharacter()-'1');return true;}
        if(k.getKeyCode()==KeyPress::rightKey){stepPad(1);return true;}
        if(k.getKeyCode()==KeyPress::leftKey){stepPad(-1);return true;}
        if(k.getTextCharacter()=='f'||k.getTextCharacter()=='F'){engine.control(FadeOut);return true;}
        if(k.getTextCharacter()=='r'||k.getTextCharacter()=='R'){engine.control(Restore);return true;}
        return false;
    }
    bool keyStateChanged(bool) override {effects.releaseKeys();return false;}
    void paint(Graphics& g) override {
        g.fillAll(bg);g.setColour(ink);g.setFont(uiFont(28,Font::bold));g.drawText("StagePilot",28,14,210,40,Justification::centredLeft);g.setColour(muted);g.setFont(uiFont(12));g.drawText(jp("ライブ演奏 / 0.22"),30,53,260,22,Justification::centredLeft);
        g.setColour(Colour(0xff2b3643));g.drawHorizontalLine(140,24.0f,getWidth()-24.0f);
        if(performancePage()){g.setColour(panel);g.fillRoundedRectangle(410,globalTitle.getY()-7.0f,getWidth()-434.0f,70,8);}
    }
    void resized() override {
        const int w=getWidth();const int top=148,bottom=getHeight()-126,body=bottom-top;
        audioButton.setBounds(w-580,30,116,38);openButton.setBounds(w-452,30,124,38);saveButton.setBounds(w-316,30,124,38);liveButton.setBounds(w-180,30,156,38);
        playTab.setBounds(24,96,140,36);mixerTab.setBounds(174,96,166,36);effectsTab.setBounds(350,96,94,36);pianoTab.setBounds(454,96,164,36);mapTab.setBounds(628,96,238,36);remoteButton.setBounds(880,96,w-904,36);midiStatus.setBounds(w-266,getHeight()-34,242,30);
        editViewport.setBounds(24,top,w-48,body);editViewport.setVisible(!performancePage());editCanvas.setSize(w-64,mixerPage?608:580);
        piano.setBounds(24,164,w-64,580);mixer.setBounds(24,164,w-64,608);effects.setBounds(24,164,w-64,570);
        guideTitle.setBounds(410,top,w-434,46);guideText.setBounds(410,top+48,w-434,35);guideTitle.setFont(uiFont(20,Font::bold));guideText.setFont(uiFont(13));
        singleBank.setVisible(false);sceneBank.setVisible(false);modeHint.setBounds(24,top+8,372,28);
        padViewport.setBounds(24,top+44,380,juce::jmax(80,body-192));layoutPads();
        const int addY=bottom-140;
        loadButton.setBounds(24,addY,181,32);editButton.setBounds(215,addY,181,32);sourceTools.setBounds(24,addY+37,181,28);fxButton.setBounds(215,addY+37,181,28);combinationEdit.setBounds(24,addY+70,372,28);
        const int detailHeight=performanceDetails?68:36,shiftY=bottom-detailHeight-82;
        liveView.setBounds(410,top+90,w-434,shiftY-top-103);
        globalTitle.setBounds(424,shiftY,260,24);globalValue.setBounds(424,shiftY+28,w-714,30);globalShift.setBounds(w-250,shiftY+12,200,36);shiftReset.setBounds(w-168,shiftY+12,130,36);
        masterLabel.setBounds(410,bottom-36,104,30);master.setBounds(514,bottom-36,w-860,36);fadeButton.setBounds(w-326,bottom-36,145,34);restoreButton.setBounds(w-170,bottom-36,146,34);
        prevButton.setBounds(24,bottom-36,181,34);nextButton.setBounds(215,bottom-36,181,34);diagnosticsButton.setBounds(622,bottom-36,170,34);detailButton.setBounds(410,bottom-36,200,34);panicButton.setBounds(286,30,jmin(280,w-890),38);
        fadeLabel.setBounds(410,bottom-68,190,22);fadeTime.setBounds(405,bottom-45,210,30);transitionLabel.setBounds(635,bottom-68,190,22);transitionTime.setBounds(630,bottom-45,210,30);tailLabel.setBounds(860,bottom-68,w-884,22);tailTime.setBounds(855,bottom-45,w-879,30);transition.setBounds(672,shiftY+40,w-696,30);
        mappingTitle.setBounds(40,182,510,30);mappingHelp.setBounds(40,216,510,24);
        actionLabel.setBounds(40,250,510,23);actionBox.setBounds(40,278,510,36);
        slotLabel.setBounds(40,324,510,22);mapSlot.setBounds(40,349,510,34);
        paramLabel.setBounds(40,391,510,22);paramBox.setBounds(40,416,510,34);
        learnHelp.setBounds(40,459,510,52);learnButton.setBounds(40,521,510,42);sourceSummary.setBounds(40,568,510,25);
        advancedButton.setBounds(40,604,240,29);
        sourceLabel.setBounds(40,638,178,18);channelLabel.setBounds(230,638,166,18);numberLabel.setBounds(404,638,150,18);
        sourceKind.setBounds(40,658,178,28);sourceChannel.setBounds(230,658,166,28);number.setBounds(400,658,150,28);
        rangeLabel.setBounds(40,693,260,18);rangeMin.setBounds(40,710,220,28);rangeMax.setBounds(280,710,270,28);addButton.setBounds(310,604,240,29);
        savedTitle.setBounds(608,182,w-666,30);mappingList.setBounds(608,228,w-656,432);deleteButton.setBounds(608,676,248,36);
        for(auto* c:canvasControls)c->setBounds(c->getBounds().translated(-24,-164));
        audioStrip.setBounds(24,getHeight()-114,w-48,78);telemetry.setBounds(24,78,w-264,18);recordButton.setBounds(w-216,72,192,22);status.setBounds(28,getHeight()-34,w-304,30);
        fxButton.setVisible(performancePage());padViewport.setVisible(performancePage());singleBank.setVisible(false);sceneBank.setVisible(false);telemetry.setVisible(true);liveView.setVisible(performancePage());sourceTools.setVisible(performancePage());for(auto* c:std::vector<Component*>{&keyzoneButton,&demoButton,&clearButton,&editorLabel})c->setVisible(false);loadButton.setVisible(performancePage());editButton.setVisible(performancePage());combinationEdit.setVisible(performancePage());for(auto* c:std::vector<Component*>{&globalTitle,&globalValue,&globalShift,&detailButton})c->setVisible(performancePage());
        for(auto* c:std::vector<Component*>{&fadeTime,&transitionTime,&tailTime,&fadeLabel,&transitionLabel,&tailLabel,&transition})c->setVisible(performancePage()&&performanceDetails);master.setVisible(false);masterLabel.setVisible(false);shiftReset.setVisible(false);fadeButton.setVisible(performancePage()&&!performanceDetails);restoreButton.setVisible(performancePage()&&!performanceDetails);
        diagnosticsButton.setVisible(performancePage()&&!performanceDetails);
    }
};
class App : public JUCEApplication {
    Theme defaultTheme;
    class Window : public DocumentWindow {
    public:
        Window(bool silent=false):DocumentWindow(silent?jp("StagePilot — 無音プレビュー"):String("StagePilot"),bg,allButtons){setUsingNativeTitleBar(true);auto* content=new Main(silent);setContentOwned(content,true);if(silent)content->enableSilentPreview(File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("Demo.stagepilot"));setResizable(true,false);setResizeLimits(1120,700,1800,1400);centreWithSize(1180,820);setVisible(true);}
        void closeButtonPressed() override {static_cast<Main*>(getContentComponent())->requestQuit();}
    };
    std::unique_ptr<Window> window;
    void startUpdater() {
        const auto executable=File::getSpecialLocation(File::currentExecutableFile);
        if(!executable.getFileName().equalsIgnoreCase("StagePilot.exe"))return;
        const auto folder=executable.getParentDirectory();
        const auto updater=folder.getChildFile("Update-StagePilot.ps1");
        if(!updater.existsAsFile()||folder.getChildFile("no-auto-update").exists())return;
        const auto systemRoot=SystemStats::getEnvironmentVariable("SystemRoot","C:/Windows");
        const auto shell=File(systemRoot).getChildFile("System32/WindowsPowerShell/v1.0/powershell.exe");
        ChildProcess update;
        update.start(StringArray{shell.getFullPathName(),"-NoProfile","-NonInteractive","-WindowStyle","Hidden",
                     "-ExecutionPolicy","Bypass","-File",updater.getFullPathName(),
                     "-CurrentVersion",getApplicationVersion(),"-AppProcessId",String(_getpid())},0);
    }
    std::unique_ptr<RemoteFixture> remoteFixture;
public:
    const String getApplicationName() override {return "StagePilot";}
    const String getApplicationVersion() override {return "0.22.0";}
    void initialise(const String& commandLine) override {
        LookAndFeel::setDefaultLookAndFeel(&defaultTheme);installJapanese();
        auto args=StringArray::fromTokens(commandLine,true);args.removeEmptyStrings();for(auto& s:args)s=s.unquoted();
        if(args.size()>=3&&args[0]=="--scan") {
            OwnedArray<PluginDescription> types;auto error=checkPluginFile(File(args[1]));if(error.isEmpty())scanPlugin(File(args[1]),types);XmlElement xml("plugins");xml.setAttribute("error",error);
            for(auto* d:types)xml.addChildElement(d->createXml().release());xml.writeTo(File(args[2]));setApplicationReturnValue(types.isEmpty()?1:0);quit();return;
        }
        if(args.size()>=2&&args[0]=="--unified-ui-check") {auto component=std::make_unique<Main>(true);setApplicationReturnValue(component->runUnifiedUiChecks(File(args[1])));component.reset();quit();return;}
        if(args.size()>=2&&args[0]=="--reuse-test") {setApplicationReturnValue(runReuseTests(File(args[1])));quit();return;}
        if(args.size()>=2&&args[0]=="--fx-test") {setApplicationReturnValue(runFxTests(File(args[1]),args.size()>2?args[2]:String()));quit();return;}
        if(args.size()>=2&&args[0]=="--video-test") {setApplicationReturnValue(runVideoTests(File(args[1])));quit();return;}
        if(args.size()>=3&&args[0]=="--video-ui-check") {auto component=std::make_unique<Main>(true);setApplicationReturnValue(component->runVideoUiCheck(File(args[1]),File(args[2])));component.reset();quit();return;}
        if(args.size()>=2&&args[0]=="--recording-test") {setApplicationReturnValue(runRecordingTests(File(args[1])));quit();return;}
        if(args.size()>=3&&args[0]=="--recording-ui-check") {auto component=std::make_unique<Main>(true);setApplicationReturnValue(component->runRecordingUiCheck(File(args[1]),File(args[2])));component.reset();quit();return;}
        if(args.size()>=2&&args[0]=="--self-test") {int result=runTests(File(args[1]),args.size()>2?args[2]:String());setApplicationReturnValue(result);quit();return;}
        if(args.size()>=2&&args[0]=="--remote-test") {setApplicationReturnValue(runRemoteTests(File(args[1])));quit();return;}
        if(args.size()>=4&&args[0]=="--remote-fixture") {remoteFixture=std::make_unique<RemoteFixture>(File(args[1]),args[2].getIntValue(),args[3]);return;}
        if(args.size()>=2&&args[0]=="--remote-qr-test") {
            File target(args[1]);
            target.getParentDirectory().createDirectory();
            TemporaryFile tmp(target);
            PNGImageFormat png;
            bool written=false;
            {
                FileOutputStream out(tmp.getFile());
                written=out.openedOk()&&png.writeImageToStream(renderPairingQr("stagepilot://10.0.2.2:19780/stagepilot-test"),out);
            }
            setApplicationReturnValue(written&&tmp.overwriteTargetFileWithTemporary()?0:1);
            quit();return;
        }
        if(args.size()>=2&&args[0]=="--catalog-regression") {
            File directory(args[1]);
            InstrumentBrowser browser([](File){},directory.getChildFile("folders.txt"),true,false);
            setApplicationReturnValue(browser.testRescan(directory)?0:1);quit();return;
        }
        if(args.size()>=2&&args[0]=="--catalog-test") {Theme theme;InstrumentBrowser browser([](File){},File(args[1]).getChildFile("folders.txt"));browser.setLookAndFeel(&theme);setApplicationReturnValue(browser.testInstalled(File(args[1]))?0:1);browser.setLookAndFeel(nullptr);quit();return;}
        if(args.size()>=2&&args[0]=="--ui-test") {auto component=std::make_unique<Main>(true);setApplicationReturnValue(component->runUiTest(File(args[1]),args.size()>2?args[2]:String()));component.reset();quit();return;}
        if(args.size()>=3&&args[0]=="--design-check") {auto component=std::make_unique<Main>(true);setApplicationReturnValue(component->runDesignCheck(File(args[1]),File(args[2])));component.reset();quit();return;}
        if(args.size()>=3&&args[0]=="--keyboard-check") {auto component=std::make_unique<Main>(true);setApplicationReturnValue(component->runKeyboardCheck(File(args[1]),File(args[2])));component.reset();quit();return;}
        if(args.size()>=3&&args[0]=="--silent-preview-check") {auto component=std::make_unique<Main>(true);setApplicationReturnValue(component->runSilentPreviewCheck(File(args[1]),File(args[2])));component.reset();quit();return;}
        if(args.contains("--silent-preview")){window=std::make_unique<Window>(true);startUpdater();return;}
        window=std::make_unique<Window>();
        startUpdater();
    }
    void shutdown() override {remoteFixture.reset();window.reset();LookAndFeel::setDefaultLookAndFeel(nullptr);}
    void systemRequestedQuit() override {if(window)window->closeButtonPressed();else quit();}
};
}
START_JUCE_APPLICATION(App)

