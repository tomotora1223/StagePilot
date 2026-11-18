    File recordingFolder() const {
        auto stored=settingsFile().getSiblingFile("recording-folder.txt").loadFileAsString().trim();
        return File::isAbsolutePath(stored)?File(stored):File::getSpecialLocation(File::userMusicDirectory).getChildFile("StagePilot");
    }
    void startRecording() {
        if(engine.movie.isBusy()){inform(jp("カメラ付き録画中です。録画を停止してから音声録音を開始してください。"));return;}
        if(silentPreview){inform(jp("無音プレビューでは録音できません。通常版を開いてください。"));return;}
        auto* device=devices.getCurrentAudioDevice();
        if(!device||!attached){inform(jp("録音する前に「接続設定」で出力機器を選んでください。"));return;}
        auto target=recordingFolder().getNonexistentChildFile(jp("演奏-")+Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"),".wav",false);
        String error;
        if(engine.recorder.start(target,device->getCurrentSampleRate(),error))
            inform(jp("録音を開始しました。もう一度押すと停止・保存します。"));
        else inform(jp("録音を開始できません：")+error);
    }
    void refreshRecording() {
        refreshVideo();
        auto& recording=engine.recorder;
        if(recording.pollFinished()) {
            lastRecording=recording.getFile();
            auto why=recording.getReason();
            if(why==Recorder::Reason::WriteError)inform(jp("録音の書き込みに失敗しました。保存先の空き容量を確認してください：")+lastRecording.getFullPathName());
            else if(why==Recorder::Reason::Overflow)inform(jp("録音バッファー不足で停止。保存できた分を残しました。演奏は継続します：")+lastRecording.getFileName());
            else if(why==Recorder::Reason::DeviceChanged)inform(jp("音声接続・音源の変更により録音を停止して保存しました：")+lastRecording.getFileName());
            else if(why==Recorder::Reason::FileLimit)inform(jp("WAVの容量上限に達したため録音を停止して保存しました：")+lastRecording.getFileName());
            else inform(jp("録音を保存しました：")+lastRecording.getFileName()+jp("（録音ボタンの右クリックで保存先を開けます）"));
            if(quitAfterRecording&&!engine.movie.isBusy()&&!cameraPreview.isBusy()){quitAfterRecording=false;requestQuit();}
        }
        const bool capturing=recording.isCapturing(),saving=recording.isBusy()&&!capturing;
        int seconds=(int)recording.seconds();
        auto elapsed=String::formatted("%02d:%02d:%02d",seconds/3600,(seconds/60)%60,seconds%60);
        auto text=capturing?jp("■ 録音停止 ")+elapsed:saving?jp("録音を保存中…"):jp("演奏を録音");
        const bool videoBusy=engine.movie.isBusy();
        if(videoBusy){auto phase=engine.movie.phase();seconds=(int)engine.movie.seconds();elapsed=String::formatted("%02d:%02d:%02d",seconds/3600,(seconds/60)%60,seconds%60);
            text=phase==VideoRecorder::Phase::Preparing?jp("録画準備中…（停止）"):phase==VideoRecorder::Phase::Saving?jp("動画を保存中…"):jp("■ 録画停止 ")+elapsed;
        }else if(!capturing&&!saving)text=jp("演奏を録音　▾");
        if(recordButton.getButtonText()!=text)recordButton.setButtonText(text);
        recordButton.setTooltip(videoBusy?engine.movie.formatDescription():jp("クリックで音声録音。右端の▼からカメラ付き録画と画質設定を開きます。"));
        recordButton.setToggleState(capturing||engine.movie.isCapturing(),dontSendNotification);
        recordButton.setEnabled(!silentPreview&&!saving&&(!videoBusy||engine.movie.phase()!=VideoRecorder::Phase::Saving));
    }
    void showRecordingOptions() {
        PopupMenu menu;
        menu.addItem(10,jp("カメラ付きで録画…"),!silentPreview&&!engine.recorder.isBusy()&&!engine.movie.isBusy());
        menu.addItem(11,jp("動画の保存先を開く"));
        menu.addSeparator();
        menu.addItem(1,jp("音声の保存先を開く"));
        menu.addItem(2,jp("保存先を変更…"),!engine.recorder.isBusy());
        menu.addItem(3,jp("前回の録音を表示"),lastRecording.existsAsFile());
        menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&recordButton),[safe=Component::SafePointer<Main>(this)](int result){
            if(!safe)return;
            if(result==10)safe->showCamera();
            if(result==11){auto dir=safe->videoFolder();if(dir.createDirectory().wasOk())dir.startAsProcess();else safe->inform(jp("動画の保存先を開けません。"));}
            if(result==1){auto dir=safe->recordingFolder();if(dir.createDirectory().wasOk())dir.startAsProcess();else safe->inform(jp("保存先フォルダーを開けません。"));}
            if(result==3&&safe->lastRecording.existsAsFile())safe->lastRecording.revealToUser();
            if(result==2&&!safe->engine.recorder.isBusy()){
                safe->recordingChooser=std::make_unique<FileChooser>(jp("録音の保存先を選ぶ"),safe->recordingFolder());
                safe->recordingChooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectDirectories,[safe](const FileChooser& chooser){
                    if(!safe||safe->engine.recorder.isBusy()||!chooser.getResult().isDirectory())return;
                    auto config=safe->settingsFile().getSiblingFile("recording-folder.txt");
                    if(config.getParentDirectory().createDirectory().wasOk()&&config.replaceWithText(chooser.getResult().getFullPathName()))safe->inform(jp("録音の保存先：")+chooser.getResult().getFullPathName());
                    else safe->inform(jp("録音の保存先を記憶できませんでした。"));
                });
            }
        });
    }
