#pragma once
#include "VideoRecorder.h"
#include "Typography.h"
#include "ResetSlider.h"
#include <functional>
#include <cmath>

namespace stage {
class CameraPanel final : public juce::Component,private juce::Timer {
    using Config=VideoRecorder::Config;
    VideoRecorder& previewDevice;
    std::function<void(Config)> record;
    std::function<void()> chooseFolder;
    std::function<juce::File()> folder;
    juce::Array<VideoRecorder::Camera> devices;
    juce::Label title,hint,path,status;
    juce::Label cameraTitle,qualityTitle,delayTitle,summary,resolutionTitle,fpsTitle,bitrateTitle,rateTitle,qualityValueTitle,encoderTitle,profileTitle,gopTitle,audioTitle,formatTitle,detailHelp;
    juce::ComboBox camera,preset,resolution,fps,rateControl,encoder,profile,audio;
    juce::TextButton previewButton,recordButton,folderButton,detailsButton;
    juce::ToggleButton strict;
    ResetSlider delay,bitrate,qualityValue,gop;
    juce::Image frame;
    Config current,pendingConfig;
    bool pending=false,blocked=false,updating=false,details=false,simulatedBusy=false;
    struct Canvas final:juce::Component {CameraPanel& owner;explicit Canvas(CameraPanel& panel):owner(panel){}void paint(juce::Graphics& g)override{owner.paintPreview(g);}} canvas{*this};
    juce::Viewport viewport;
    std::vector<juce::Component*> detailControls;
    int previewWidth=400,previewHeight=225;
    bool busy()const{return simulatedBusy||previewDevice.isBusy()||pending;}
    static juce::String jp(const char* value){return juce::String::fromUTF8(value);}
    void label(juce::Label& value,const char* text,int size,bool fixed=false){value.setText(jp(text),juce::dontSendNotification);value.setFont(uiFont((float)size));value.setMinimumHorizontalScale(1.0f);if(fixed)addAndMakeVisible(value);else canvas.addAndMakeVisible(value);}
    void configureSlider(ResetSlider& slider,double minimum,double maximum,double initial,const char* suffix){slider.setRange(minimum,maximum,1);slider.setValue(initial,juce::dontSendNotification);slider.setDoubleClickReturnValue(true,initial);slider.setSliderStyle(juce::Slider::IncDecButtons);slider.setTextBoxStyle(juce::Slider::TextBoxLeft,false,132,32);slider.setTextValueSuffix(suffix);canvas.addAndMakeVisible(slider);}
    static int profileId(VideoRecorder::Profile value){return value==VideoRecorder::Profile::Baseline?2:value==VideoRecorder::Profile::Main?3:value==VideoRecorder::Profile::High?4:1;}
    static VideoRecorder::Profile readProfile(int id){return id==2?VideoRecorder::Profile::Baseline:id==3?VideoRecorder::Profile::Main:id==4?VideoRecorder::Profile::High:VideoRecorder::Profile::Auto;}
    Config presetConfig(int id)const {
        Config c=current;c.rateControl=VideoRecorder::RateControl::CBR;c.quality=85;c.profile=VideoRecorder::Profile::Auto;c.encoder=VideoRecorder::Encoder::Auto;c.keyframeSeconds=0;
        if(id==1){c.preferredWidth=640;c.preferredHeight=480;c.preferredFps=30;c.bitrateMbps=3;c.audioBitrateKbps=128;}
        else if(id==2){c.preferredWidth=1280;c.preferredHeight=720;c.preferredFps=30;c.bitrateMbps=6;c.audioBitrateKbps=160;}
        else if(id==4){c.preferredWidth=3840;c.preferredHeight=2160;c.preferredFps=30;c.bitrateMbps=45;c.audioBitrateKbps=192;c.profile=VideoRecorder::Profile::High;}
        else {c.preferredWidth=1920;c.preferredHeight=1080;c.preferredFps=30;c.bitrateMbps=12;c.audioBitrateKbps=192;}
        return c;
    }
    static bool sameQuality(const Config& a,const Config& b){return a.preferredWidth==b.preferredWidth&&a.preferredHeight==b.preferredHeight&&std::abs(a.preferredFps-b.preferredFps)<.0001&&a.bitrateMbps==b.bitrateMbps&&a.rateControl==b.rateControl&&a.quality==b.quality&&a.profile==b.profile&&a.encoder==b.encoder&&a.keyframeSeconds==b.keyframeSeconds&&a.audioBitrateKbps==b.audioBitrateKbps;}
    int matchingPreset(const Config& value)const{for(int n=1;n<=4;++n)if(sameQuality(value,presetConfig(n)))return n;return 5;}
    void updateSummary(){
        auto qualityText=current.rateControl==VideoRecorder::RateControl::CBR?juce::String(current.bitrateMbps)+jp(" Mbps / 約")+juce::String((current.bitrateMbps+current.audioBitrateKbps/1000.0)*60.0/8.0,0)+jp(" MB/分"):jp("可変画質 ")+juce::String(current.quality)+jp(" / 容量は映像により変わります");
        summary.setText(juce::String(current.preferredWidth)+" × "+juce::String(current.preferredHeight)+" / "+juce::String(current.preferredFps, current.preferredFps==std::floor(current.preferredFps)?0:2)+" fps\n"+qualityText,juce::dontSendNotification);
        summary.setTooltip(jp("容量は動画とAAC音声の目安です。別に保存するWAVの容量は含みません。"));
        detailHelp.setText(jp("4K・高いfpsには対応するカメラが必要です。指定形式が使えない場合、必須設定では開始せず理由を表示します。\n自動エンコーダーは対応するハードウェアの利用を許可しますが、実際の使用方式は環境によります。"),juce::dontSendNotification);
    }
    void applyControls(const Config& value){
        updating=true;current=value;int resolutionId=0;const int heights[]={480,720,1080,1440,2160};const int widths[]={640,1280,1920,2560,3840};for(int n=0;n<5;++n)if(value.preferredHeight==heights[n]&&value.preferredWidth==widths[n])resolutionId=n+1;
        resolution.setSelectedId(resolutionId,juce::dontSendNotification);if(resolutionId==0)resolution.setText(juce::String(value.preferredWidth)+" × "+juce::String(value.preferredHeight),juce::dontSendNotification);
        fps.setSelectedId(0,juce::dontSendNotification);fps.setText(juce::String(value.preferredFps,value.preferredFps==std::floor(value.preferredFps)?0:3)+" fps",juce::dontSendNotification);
        rateControl.setSelectedId(value.rateControl==VideoRecorder::RateControl::QualityVbr?2:1,juce::dontSendNotification);encoder.setSelectedId(value.encoder==VideoRecorder::Encoder::Software?2:1,juce::dontSendNotification);profile.setSelectedId(profileId(value.profile),juce::dontSendNotification);audio.setSelectedId(value.audioBitrateKbps,juce::dontSendNotification);
        bitrate.setValue(value.bitrateMbps,juce::dontSendNotification);qualityValue.setValue(value.quality,juce::dontSendNotification);gop.setValue(value.keyframeSeconds,juce::dontSendNotification);gop.updateText();delay.setValue(value.videoDelayMs,juce::dontSendNotification);strict.setToggleState(value.strictFormat,juce::dontSendNotification);
        int cameraId=0;for(int n=0;n<devices.size();++n)if(devices[n].id==value.cameraId)cameraId=n+1;if(cameraId==0&&!devices.isEmpty()&&value.cameraId.isEmpty())cameraId=1;camera.setSelectedId(cameraId,juce::dontSendNotification);if(cameraId>0)current.cameraId=devices[cameraId-1].id;
        preset.setSelectedId(matchingPreset(current),juce::dontSendNotification);updating=false;updateSummary();refreshEnabled();
    }
    void controlsChanged(bool qualityChanged){
        if(updating||busy()){if(!updating)applyControls(current);return;}
        Config next=current;if(camera.getSelectedId()>0)next.cameraId=devices[camera.getSelectedId()-1].id;
        int id=resolution.getSelectedId();if(id>0){const int widths[]={640,1280,1920,2560,3840},heights[]={480,720,1080,1440,2160};next.preferredWidth=widths[id-1];next.preferredHeight=heights[id-1];}
        double frameRate=fps.getText().getDoubleValue();if(frameRate>0)next.preferredFps=frameRate;
        next.bitrateMbps=(int)bitrate.getValue();next.quality=(int)qualityValue.getValue();next.keyframeSeconds=(int)gop.getValue();next.videoDelayMs=(int)delay.getValue();next.strictFormat=strict.getToggleState();next.audioBitrateKbps=audio.getSelectedId();next.profile=readProfile(profile.getSelectedId());next.encoder=encoder.getSelectedId()==2?VideoRecorder::Encoder::Software:VideoRecorder::Encoder::Auto;next.rateControl=rateControl.getSelectedId()==2?VideoRecorder::RateControl::QualityVbr:VideoRecorder::RateControl::CBR;
        current=next;if(qualityChanged){updating=true;preset.setSelectedId(5,juce::dontSendNotification);updating=false;}updateSummary();refreshEnabled();if(settingsChanged)settingsChanged(current);
    }
    void selectPreset(int id){if(updating||id==5)return;if(busy()){applyControls(current);return;}applyControls(presetConfig(id));if(settingsChanged)settingsChanged(current);}
    void refreshEnabled(){
        bool idle=!busy();for(auto* component:std::vector<juce::Component*>{&camera,&preset,&resolution,&fps,&rateControl,&encoder,&profile,&audio,&delay,&bitrate,&qualityValue,&gop,&strict})component->setEnabled(idle);
        bitrate.setEnabled(idle&&current.rateControl==VideoRecorder::RateControl::CBR);qualityValue.setEnabled(idle&&current.rateControl==VideoRecorder::RateControl::QualityVbr);
        folderButton.setEnabled(idle);previewButton.setEnabled(!blocked&&!pending&&camera.getSelectedId()>0);recordButton.setEnabled(!blocked&&!pending&&camera.getSelectedId()>0);
        previewButton.setButtonText(previewDevice.isBusy()?jp("確認を止める"):jp("映像を確認"));
    }
    void timerCallback() override {
        const auto phase=previewDevice.phase();if(previewDevice.isBusy())frame=previewDevice.preview();
        if(pending&&!previewDevice.isBusy()&&!simulatedBusy){auto requested=pendingConfig;pending=false;refreshEnabled();record(requested);return;}
        refreshEnabled();
        if(phase==VideoRecorder::Phase::Preparing)status.setText(jp("カメラを準備しています…"),juce::dontSendNotification);
        else if(previewDevice.isBusy()&&phase==VideoRecorder::Phase::Previewing)status.setText(jp("確認中：")+previewDevice.formatDescription(),juce::dontSendNotification);
        else if(phase==VideoRecorder::Phase::Failed)status.setText(previewDevice.error(),juce::dontSendNotification);
        else if(!previewDevice.isBusy()&&frame.isValid())status.setText(jp("映像の確認を停止しました。設定を変更できます。"),juce::dontSendNotification);
        path.setText(jp("保存先：")+folder().getFullPathName(),juce::dontSendNotification);path.setTooltip(path.getText());status.setTooltip(status.getText());canvas.repaint(0,0,previewWidth,previewHeight);
    }
    void paintPreview(juce::Graphics& g){
        g.setColour(juce::Colour(0xff1b222b));g.fillRoundedRectangle(0,0,(float)previewWidth,(float)previewHeight,8);
        if(frame.isValid())g.drawImageWithin(frame,0,0,previewWidth,previewHeight,juce::RectanglePlacement::centred);
        else{g.setColour(juce::Colour(0xffa6b5c6));g.setFont(uiFont(16));g.drawFittedText(jp("「映像を確認」を押すと\nカメラが有効になります"),20,previewHeight/2-38,previewWidth-40,76,juce::Justification::centred,3);}
    }
public:
    std::function<void(Config)> settingsChanged;
    CameraPanel(VideoRecorder& preview,std::function<void(Config)> start,std::function<void()> selectFolder,std::function<juce::File()> destination,bool silent=false,bool enumerate=true,Config initial=Config{})
        :previewDevice(preview),record(std::move(start)),chooseFolder(std::move(selectFolder)),folder(std::move(destination)),current(initial),blocked(silent){
        label(title,"カメラと演奏を録画",24,true);label(hint,"カメラ映像 ＋ StagePilotの演奏音。マイクの音は録音しません。",14,true);label(path,"",13,true);label(status,"MP4と、バックアップ用の音声WAVを保存します。\n確認用の映像は最大8fps。録画は設定したfpsで保存します。",14,true);
        addAndMakeVisible(viewport);viewport.setViewedComponent(&canvas,false);viewport.setScrollBarsShown(true,false);viewport.setScrollBarThickness(10);
        label(cameraTitle,"カメラ",14);label(qualityTitle,"画質プリセット",14);label(delayTitle,"映像の補正",13);label(summary,"",13);label(resolutionTitle,"解像度",14);label(fpsTitle,"動きのなめらかさ",14);label(bitrateTitle,"映像ビットレート",14);label(rateTitle,"画質の決め方",14);label(qualityValueTitle,"可変画質（1〜100）",14);label(encoderTitle,"エンコーダー",14);label(profileTitle,"H.264プロファイル",14);label(gopTitle,"キーフレームの間隔",14);label(audioTitle,"動画の音声（AAC）",14);label(formatTitle,"カメラの形式",14);label(detailHelp,"",13);
        for(auto* component:std::vector<juce::Component*>{&camera,&preset,&resolution,&fps,&rateControl,&encoder,&profile,&audio,&previewButton,&detailsButton,&strict})canvas.addAndMakeVisible(component);for(auto* component:std::vector<juce::Component*>{&recordButton,&folderButton})addAndMakeVisible(component);
        juce::String error;if(enumerate&&!silent)devices=VideoRecorder::cameras(error);for(int n=0;n<devices.size();++n)camera.addItem(devices[n].name,n+1);camera.setTextWhenNothingSelected(jp(initial.cameraId.isNotEmpty()?"保存したカメラが接続されていません":"カメラが見つかりません"));
        for(int n=1;n<=5;++n)preset.addItem(jp(n==1?"軽量：480p / 3 Mbps":n==2?"標準：720p / 6 Mbps":n==3?"高画質：1080p / 12 Mbps":n==4?"最高画質：4K / 45 Mbps":"カスタム"),n);
        const char* resolutions[]={"480p（640 × 480）","720p（1280 × 720）","1080p（1920 × 1080）","1440p（2560 × 1440）","4K（3840 × 2160）"};for(int n=0;n<5;++n)resolution.addItem(jp(resolutions[n]),n+1);for(int n:{15,24,25,30,50,60})fps.addItem(juce::String(n)+" fps",n);
        rateControl.addItem(jp("ビットレートを指定"),1);rateControl.addItem(jp("画質を優先（可変）"),2);encoder.addItem(jp("自動（ハードウェアも利用）"),1);encoder.addItem(jp("ソフトウェア"),2);
        profile.addItem(jp("自動"),1);profile.addItem("Baseline",2);profile.addItem("Main",3);profile.addItem("High",4);for(int n:{96,128,160,192})audio.addItem(juce::String(n)+" kbps",n);
        configureSlider(delay,-1000,1000,0," ms");delay.setRange(-1000,1000,10);configureSlider(bitrate,1,100,12," Mbps");configureSlider(qualityValue,1,100,85,"");configureSlider(gop,0,10,0,"");gop.textFromValueFunction=[](double value){return value==0?jp("自動"):juce::String((int)value)+jp(" 秒");};gop.valueFromTextFunction=[](const juce::String& text){return text.getDoubleValue();};
        delay.setTooltip(jp("映像を遅らせたいときはプラス。早めたいときはマイナス。ダブルクリックで0に戻します。"));bitrate.setTooltip(jp("数値が大きいほど高画質・大容量。ダブルクリックで12 Mbpsに戻します。"));qualityValue.setTooltip(jp("画質優先モード用です。ダブルクリックで85に戻します。"));gop.setTooltip(jp("0はエンコーダーに任せます。ダブルクリックで自動に戻します。"));
        strict.setButtonText(jp("指定の解像度・fpsを必須にする"));strict.setTooltip(jp("オフでは最も近いカメラ形式を使用し、実際の形式を表示します。オンでは対応しない設定のまま開始しません。"));
        detailsButton.setButtonText(jp("詳細設定を開く"));previewButton.setButtonText(jp("映像を確認"));recordButton.setButtonText(jp("この設定で録画を始める"));folderButton.setButtonText(jp("保存先を変更…"));recordButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff286448));
        camera.onChange=[this]{controlsChanged(false);};preset.onChange=[this]{selectPreset(preset.getSelectedId());};for(auto* combo:{&resolution,&fps,&rateControl,&encoder,&profile,&audio})combo->onChange=[this]{controlsChanged(true);};for(auto* slider:{&bitrate,&qualityValue,&gop})slider->onValueChange=[this]{controlsChanged(true);};delay.onValueChange=[this]{controlsChanged(false);};strict.onClick=[this]{controlsChanged(false);};detailsButton.onClick=[this]{details=!details;resized();viewport.setViewPosition(0,details?300:0);};
        previewButton.onClick=[this]{if(blocked||pending)return;if(previewDevice.isBusy())previewDevice.requestStop();else{juce::String err;if(!previewDevice.startPreview(requestedConfig(),err))status.setText(err,juce::dontSendNotification);}refreshEnabled();};
        recordButton.onClick=[this]{if(blocked||pending||simulatedBusy)return;pendingConfig=requestedConfig();if(previewDevice.isBusy()){pending=true;previewDevice.requestStop();status.setText(jp("確認を終了して録画を準備しています…"),juce::dontSendNotification);refreshEnabled();}else record(pendingConfig);};folderButton.onClick=[this]{if(!busy())chooseFolder();};
        detailControls={&resolutionTitle,&fpsTitle,&bitrateTitle,&rateTitle,&qualityValueTitle,&encoderTitle,&profileTitle,&gopTitle,&audioTitle,&formatTitle,&detailHelp,&resolution,&fps,&bitrate,&rateControl,&qualityValue,&encoder,&profile,&gop,&audio,&strict};
        applyControls(initial);if(error.isNotEmpty())status.setText(error,juce::dontSendNotification);if(silent)status.setText(jp("無音プレビューではカメラを開かず、録画も行いません。"),juce::dontSendNotification);setSize(900,640);startTimerHz(8);timerCallback();
    }
    ~CameraPanel() override{stopTimer();viewport.setViewedComponent(nullptr,false);previewDevice.requestStop();}
    Config requestedConfig()const{return current;}
    bool setRequestedConfig(const Config& value){if(busy()||VideoRecorder::validateConfig(value).isNotEmpty())return false;applyControls(value);return true;}
    void paint(juce::Graphics& g) override{g.fillAll(juce::Colour(0xff101419));}
    void resized() override{
        int w=getWidth(),h=getHeight();title.setBounds(24,16,w-48,36);hint.setBounds(24,55,w-48,32);viewport.setBounds(24,98,w-48,juce::jmax(100,h-242));canvas.setSize(juce::jmax(600,viewport.getWidth()-12),details?792:338);
        int cw=canvas.getWidth();previewWidth=juce::roundToInt(cw*.56f);previewHeight=225;int x=previewWidth+22,right=cw-x;cameraTitle.setBounds(x,0,right,24);camera.setBounds(x,27,right,34);qualityTitle.setBounds(x,78,right,24);preset.setBounds(x,105,right,34);previewButton.setBounds(x,163,right,34);delayTitle.setBounds(x,214,right,24);delay.setBounds(x,242,right,34);summary.setBounds(0,238,previewWidth,52);detailsButton.setBounds(0,302,cw,32);
        int gap=24,column=(cw-gap)/2;auto field=[&](juce::Label& label,juce::Component& control,int col,int row){int left=col*(column+gap),top=350+row*74;label.setBounds(left,top,column,24);control.setBounds(left,top+27,column,34);};
        field(resolutionTitle,resolution,0,0);field(fpsTitle,fps,1,0);field(bitrateTitle,bitrate,0,1);field(rateTitle,rateControl,1,1);field(qualityValueTitle,qualityValue,0,2);field(encoderTitle,encoder,1,2);field(profileTitle,profile,0,3);field(gopTitle,gop,1,3);field(audioTitle,audio,0,4);field(formatTitle,strict,1,4);detailHelp.setBounds(0,728,cw,58);for(auto* component:detailControls)component->setVisible(details);detailsButton.setButtonText(details?jp("詳細設定を閉じる"):jp("詳細設定を開く"));
        path.setBounds(24,h-137,w-242,40);folderButton.setBounds(w-194,h-135,170,34);status.setBounds(24,h-91,w-48,42);recordButton.setBounds(w-340,h-48,316,36);
    }
    bool testSilent()const{return !recordButton.isEnabled()&&!previewButton.isEnabled();}
    void testPreset(int id){preset.setSelectedId(id,juce::sendNotificationSync);}
    void testSetDetails(bool value){details=value;resized();viewport.setViewPosition(0,details?300:0);}
    void testScrollDetailsToBottom(){viewport.setViewPosition(0,canvas.getHeight());}
    void testSimulateBusy(bool value){simulatedBusy=value;refreshEnabled();}
    int testPresetSelection()const{return preset.getSelectedId();}
    bool testCustomControls(){
        resolution.setSelectedId(4,juce::sendNotificationSync);fps.setSelectedId(60,juce::sendNotificationSync);bitrate.setValue(32,juce::sendNotificationSync);profile.setSelectedId(4,juce::sendNotificationSync);encoder.setSelectedId(2,juce::sendNotificationSync);gop.setValue(3,juce::sendNotificationSync);audio.setSelectedId(160,juce::sendNotificationSync);delay.setValue(70,juce::sendNotificationSync);strict.setToggleState(false,juce::dontSendNotification);strict.onClick();
        return current.preferredWidth==2560&&current.preferredHeight==1440&&current.preferredFps==60&&current.bitrateMbps==32&&current.profile==VideoRecorder::Profile::High&&current.encoder==VideoRecorder::Encoder::Software&&current.keyframeSeconds==3&&current.audioBitrateKbps==160&&current.videoDelayMs==70&&!current.strictFormat&&preset.getSelectedId()==5;
    }
    bool testRateControls(){rateControl.setSelectedId(2,juce::sendNotificationSync);bool ok=!bitrate.isEnabled()&&qualityValue.isEnabled()&&current.rateControl==VideoRecorder::RateControl::QualityVbr;rateControl.setSelectedId(1,juce::sendNotificationSync);return ok&&bitrate.isEnabled()&&!qualityValue.isEnabled()&&current.rateControl==VideoRecorder::RateControl::CBR;}
    bool testControlsLocked()const{return !camera.isEnabled()&&!preset.isEnabled()&&!resolution.isEnabled()&&!fps.isEnabled()&&!rateControl.isEnabled()&&!encoder.isEnabled()&&!profile.isEnabled()&&!audio.isEnabled()&&!delay.isEnabled()&&!bitrate.isEnabled()&&!qualityValue.isEnabled()&&!gop.isEnabled()&&!strict.isEnabled()&&!folderButton.isEnabled();}
    // The headless UI test uses a dummy record callback and never opens a camera.
    void testStagePendingRequest(){pendingConfig=requestedConfig();pending=true;simulatedBusy=true;refreshEnabled();}
    void testCompletePendingRequest(){simulatedBusy=false;timerCallback();}
    bool testLayoutChecks(){bool ok=true;for(auto size:std::array<juce::Point<int>,2>{{{760,580},{900,700}}}){setSize(size.x,size.y);testSetDetails(true);auto bounds=getLocalBounds();for(auto* component:std::vector<juce::Component*>{&title,&hint,&viewport,&path,&status,&recordButton,&folderButton})ok=ok&&bounds.contains(component->getBounds());ok=ok&&viewport.getBottom()<=path.getY()&&status.getBottom()<=recordButton.getY();for(auto* component:detailControls)ok=ok&&canvas.getLocalBounds().contains(component->getBounds());testScrollDetailsToBottom();ok=ok&&viewport.getViewPositionY()>0&&detailHelp.getBottom()<=viewport.getViewPositionY()+viewport.getViewHeight();}setSize(760,580);testSetDetails(false);viewport.setViewPosition(0,0);return ok;}
    bool testResets(){Config before=current;bool ok=true;bitrate.setValue(40,juce::sendNotificationSync);ok=bitrate.testValueDoubleClick()&&current.bitrateMbps==12&&ok;gop.setValue(5,juce::sendNotificationSync);ok=gop.testValueDoubleClick()&&current.keyframeSeconds==0&&ok;delay.setValue(120,juce::sendNotificationSync);ok=delay.testValueDoubleClick()&&current.videoDelayMs==0&&ok;rateControl.setSelectedId(2,juce::sendNotificationSync);qualityValue.setValue(60,juce::sendNotificationSync);ok=qualityValue.testValueDoubleClick()&&current.quality==85&&ok;applyControls(before);return ok;}
};
}
