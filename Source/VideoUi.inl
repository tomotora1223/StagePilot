    VideoRecorder::Config readVideoPreferences(const File& file) const {
        VideoRecorder::Config config;String error;if(file.existsAsFile()){VideoRecorder::Config loaded;if(VideoRecorder::configFromJson(JSON::parse(file),loaded,error))config=loaded;}return config;
    }
    bool writeVideoPreferences(const File& file,const VideoRecorder::Config& config,String& error){
        error=VideoRecorder::validateConfig(config);if(error.isNotEmpty())return false;if(!file.getParentDirectory().createDirectory().wasOk()){error=jp("録画設定の保存先を作成できません。");return false;}TemporaryFile temporary(file);if(!temporary.getFile().replaceWithText(JSON::toString(VideoRecorder::configToJson(config)))||!temporary.overwriteTargetFileWithTemporary()){error=jp("録画設定を保存できません。以前の設定はそのままです。");return false;}return true;
    }
    void rememberVideoPreferences(const VideoRecorder::Config& config){if(testMode)return;String error;if(!writeVideoPreferences(settingsFile().getSiblingFile("video-settings.json"),config,error))inform(error);}
    File videoFolder() const {
        auto stored=settingsFile().getSiblingFile("video-folder.txt").loadFileAsString().trim();
        return File::isAbsolutePath(stored)?File(stored):File::getSpecialLocation(File::userMoviesDirectory).getChildFile("StagePilot");
    }
    void chooseVideoFolder(){
        if(engine.movie.isBusy())return;
        videoChooser=std::make_unique<FileChooser>(jp("動画の保存先を選ぶ"),videoFolder());
        videoChooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectDirectories,[safe=Component::SafePointer<Main>(this)](const FileChooser& chooser){
            if(!safe||safe->engine.movie.isBusy()||!chooser.getResult().isDirectory())return;
            auto file=safe->settingsFile().getSiblingFile("video-folder.txt");
            if(!file.getParentDirectory().createDirectory().wasOk()||!file.replaceWithText(chooser.getResult().getFullPathName()))safe->inform(jp("動画の保存先を記憶できませんでした。"));
        });
    }
    void startVideo(VideoRecorder::Config config){
        if(silentPreview||engine.recorder.isBusy()||engine.movie.isBusy()){inform(jp("録音・録画を停止し、保存が終わってから開始してください。"));return;}
        auto* device=devices.getCurrentAudioDevice();if(!device||!attached){inform(jp("録画する前に「接続設定」で出力機器を選んでください。"));return;}
        rememberVideoPreferences(config);config.sampleRate=device->getCurrentSampleRate();
        config.target=videoFolder().getNonexistentChildFile(jp("演奏動画-")+Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+"-"+Uuid().toString().substring(0,4),".mp4",false);
        String error;if(engine.movie.start(config,error))inform(jp("カメラを準備しています。演奏音はWAVにも保存します。"));else inform(jp("録画を開始できません：")+error);
    }
    void showCamera(){
        if(silentPreview){inform(jp("無音プレビューではカメラを開きません。通常版を起動してください。"));return;}
        if(engine.recorder.isBusy()||engine.movie.isBusy()){inform(jp("録音・録画を停止してからカメラの設定を開いてください。"));return;}
        if(cameraWindow){cameraWindow->toFront(true);return;}
        auto* panel=new CameraPanel(cameraPreview,[safe=Component::SafePointer<Main>(this)](VideoRecorder::Config config){
            MessageManager::callAsync([safe,config]{if(!safe)return;safe->cameraWindow.reset();safe->startVideo(config);});
        },[this]{chooseVideoFolder();},[this]{return videoFolder();},false,true,readVideoPreferences(settingsFile().getSiblingFile("video-settings.json")));
        panel->settingsChanged=[this](VideoRecorder::Config config){rememberVideoPreferences(config);};
        cameraWindow=std::make_unique<FloatingWindow>(jp("カメラ付き録画"),panel);cameraWindow->setResizeLimits(760,610,1400,1100);cameraWindow->setResizable(true,false);
        cameraWindow->close=[this]{cameraPreview.requestStop();cameraWindow.reset();};
    }
    void refreshVideo(){
        cameraPreview.pollFinished();
        if(engine.movie.pollFinished()){
            lastVideo=engine.movie.movieFile();
            if(engine.movie.phase()==VideoRecorder::Phase::Failed)inform(jp("録画に問題がありました：")+engine.movie.error()+(engine.movie.audioFile().existsAsFile()?jp(" 音声WAVは動画の保存先に残しています。"):String()));
            else inform(jp("動画を保存しました：")+lastVideo.getFileName()+jp("（音声WAVも保存済み）"));
        }
        if(quitAfterRecording&&!engine.recorder.isBusy()&&!engine.movie.isBusy()&&!cameraPreview.isBusy()){quitAfterRecording=false;requestQuit();}
    }
