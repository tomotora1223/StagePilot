    int runRecordingUiCheck(const File& folder,const File& demo) {
        folder.createDirectory();stopTimer();loadSession(demo);engine.prepare(48000,256);
        auto* result=new DynamicObject();int failures=0;
        auto check=[&](bool passed,const char* name){result->setProperty(name,passed);if(!passed)++failures;};
        check(devices.getCurrentAudioDevice()==nullptr,"audioDeviceNotOpened");
        auto target=folder.getNonexistentChildFile("ui-recording",".wav",false);String error;
        check(engine.recorder.start(target,48000,error),"recordingStarts");
        AudioBuffer<float> block(2,256);engine.preview(60);
        for(int i=0;i<20;++i)engine.render(block.getArrayOfWritePointers(),2,256);
        refreshRecording();check(recordButton.getToggleState()&&recordButton.getButtonText().contains(jp("録音停止")),"recordingStateIsVisible");
        bool fits=true;for(auto size:std::array<Point<int>,2>{{{1120,700},{1180,820}}}){
            setSize(size.x,size.y);
            for(int page=0;page<5;++page){mappingPage=page==4;mixerPage=page==1;effectsPage=page==2;pianoPage=page==3;updatePage();
                fits&=recordButton.isVisible()&&recordButton.getRight()<=getWidth()-24&&recordButton.getX()>telemetry.getRight()&&recordButton.getBottom()<playTab.getY();
            }
        }check(fits,"recordingControlFitsAllFivePagesAtMinimumSize");
        mixerPage=mappingPage=effectsPage=pianoPage=false;updatePage();audioStrip.focus(0);setSize(1180,820);timerCallback();
        auto writeShot=[&](const String& name){FileOutputStream stream(folder.getChildFile(name));stream.setPosition(0);stream.truncate();PNGImageFormat png;return png.writeImageToStream(createComponentSnapshot(getLocalBounds()),stream);};
        check(writeShot(jp("録音中.png")),"recordingScreenshotWritten");
        recordButton.triggerClick();MessageManager::getInstance()->runDispatchLoopUntil(30);
        auto deadline=Time::getMillisecondCounter()+5000;
        while(engine.recorder.isBusy()&&Time::getMillisecondCounter()<deadline){refreshRecording();Thread::sleep(2);}
        refreshRecording();
        check(!engine.recorder.isBusy()&&target.getSize()>44,"stopFinalisesWav");
        check(lastRecording==target&&recordButton.getButtonText().startsWith(jp("演奏を録音")),"asynchronousSaveRestoresReadyStateAndFileLocation");
        check(writeShot(jp("録音停止後.png")),"stoppedScreenshotWritten");
        enableSilentPreview(demo);refreshRecording();check(!recordButton.isEnabled(),"silentPreviewBlocksRecording");
        result->setProperty("failures",failures);folder.getChildFile("recording-ui-tests.json").replaceWithText(JSON::toString(var(result)));dirty=false;return failures?1:0;
    }
