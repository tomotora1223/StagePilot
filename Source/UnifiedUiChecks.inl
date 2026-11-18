    int runUnifiedUiChecks(const File& folder) {
        folder.createDirectory();stopTimer();live=false;
        Array<var> results;int failures=0;
        auto check=[&](bool passed,const String& name){auto* row=new DynamicObject();row->setProperty("test",name);row->setProperty("passed",passed);results.add(var(row));if(!passed)++failures;};
        check(testMode&&!attached&&devices.getCurrentAudioDevice()==nullptr,"Headless checks never open a physical audio device");
        AudioBuffer<float> block(2,128);
        auto drain=[&]{engine.render(block.getArrayOfWritePointers(),2,block.getNumSamples());timerCallback();};
        auto snapshot=[&](Component& component,const String& name){FileOutputStream stream(folder.getChildFile(name));stream.setPosition(0);stream.truncate();PNGImageFormat format;return format.writeImageToStream(component.createComponentSnapshot(component.getLocalBounds()),stream);};
        suspend([&]{for(int i=0;i<2;++i){auto synth=makeTestSynth();PluginDescription description;synth->fillInPluginDescription(description);description.name=i==0?jp("共有ピアノ・温かい音色"):jp("パッド・広がる音色");putPlugin(i,std::move(synth),description);}engine.prepare(48000,128);});
        select(0);drain();auto* shared=engine.slots[0].plugin.get();audioStrip.focus(0);makeSharedVariant();drain();
        check(mixerPage&&mixer.editingScene()==0&&engine.slots[0].plugin.get()==shared&&engine.slots[2].plugin==nullptr,"Use source again creates an editable sound without another plugin instance");
        mixer.testEditPart({0,127,12,.8f,true,false,true});bool registered=mixer.registerCurrent();drain();
        check(registered&&mixer.getScenes()[0].registered&&engine.activeScene==0&&engine.slots[0].transpose==12,"First shared-source variation registers its own transpose");
        makeSharedVariant();drain();mixer.testEditPart({0,127,-12,.6f,true,true,true});mixer.setScenePedal(1,0,true);registered=mixer.registerCurrent();drain();
        check(registered&&mixer.getScenes()[1].registered&&engine.activeScene==1&&engine.slots[0].transpose== -12&&engine.slots[0].plugin.get()==shared,"Second variation reuses the same plugin and keeps independent pitch and pedal settings");
        mixerPage=effectsPage=pianoPage=mappingPage=false;updatePage();setSize(1180,820);timerCallback();
        check(padViewport.isVisible()&&!singleBank.isVisible()&&!sceneBank.isVisible()&&pads[0].isVisible()&&pads[8].isVisible()&&pads[9].isVisible(),"Standalone and constructed sounds appear together without a bank selector");
        pads[0].onClick();drain();check(engine.activeScene==-1&&engine.selected==0,"One click selects a standalone instrument from the common list");
        pads[8].onClick();drain();check(engine.activeScene==0&&engine.slots[0].transpose==12&&engine.slots[0].plugin.get()==shared,"One click selects the +12 shared variation from the same list");
        pads[9].onClick();drain();check(engine.activeScene==1&&engine.slots[0].transpose== -12&&engine.slots[0].ignorePedal,"One click selects the -12 shared variation from the same list");
        pads[1].onClick();drain();stepPad(1);drain();check(engine.activeScene==0,"Next sound crosses directly from standalone to constructed sounds");stepPad(-1);drain();check(engine.activeScene==-1&&engine.selected==1,"Previous sound crosses directly back to standalone instruments");
        check(snapshot(*this,jp("統合した音色一覧.png")),"Common performance list screenshot written");
        setSize(1120,700);timerCallback();bool minimum=padViewport.getBottom()<=loadButton.getY()&&padViewport.getWidth()>=372&&audioStrip.getBottom()<=status.getY()&&audioStrip.getY()>nextButton.getBottom();
        check(minimum&&snapshot(*this,jp("統合した音色一覧・最小サイズ.png")),"Common list and fixed audio meters fit the minimum window");
        addBuiltinFx(0);addBuiltinFx(0);addBuiltinFx(8);drain();
        check(chain(0).count()==2&&chain(8).count()==1,"Source and final-output FX chains can be added independently");
        auto* first=chain(0).nodes[0].plugin.get();auto* second=chain(0).nodes[1].plugin.get();
        suspend([&]{chain(0).nodes[0].plugin->getParameters()[0]->setValueNotifyingHost(.13f);chain(0).nodes[0].bypassed=true;chain(0).nodes[1].plugin->getParameters()[0]->setValueNotifyingHost(.71f);chain(8).nodes[0].plugin->getParameters()[0]->setValueNotifyingHost(.29f);chain(0).moveNode(0,1);});
        check(chain(0).nodes[0].plugin.get()==second&&chain(0).nodes[1].plugin.get()==first&&chain(0).nodes[1].bypassed,"FX reordering keeps each instance and its bypass setting");
        {
            FxPanel panel(engine,0);panel.setLookAndFeel(&theme);panel.onBypass=[&](int target,int index,bool bypassed){chain(target).nodes[(size_t)index].bypassed=bypassed;};
            check(panel.testLayoutChecks(),"FX panel controls fit minimum and expanded sizes and lock structural editing");
            check(snapshot(panel,jp("FX・音源ごとの設定.png")),"Per-source FX panel screenshot written");panel.setTarget(8);check(snapshot(panel,jp("FX・全体出力.png")),"Final-output FX panel screenshot written");panel.setLookAndFeel(nullptr);
        }
        var saved;suspend([&]{saved=toJson(folder);});auto file=folder.getChildFile("shared-fx-roundtrip.stagepilot");file.replaceWithText(JSON::toString(saved));loadSession(file);engine.prepare(48000,128);drain();
        bool restored=chain(0).count()==2&&chain(8).count()==1&&chain(0).nodes[1].bypassed&&!chain(0).nodes[0].bypassed&&std::abs(chain(0).nodes[0].plugin->getParameters()[0]->getValue()-.71f)<.001f&&std::abs(chain(0).nodes[1].plugin->getParameters()[0]->getValue()-.13f)<.001f&&std::abs(chain(8).nodes[0].plugin->getParameters()[0]->getValue()-.29f)<.001f;
        check(restored,"Session roundtrip preserves FX order, individual parameter state, bypass and master chain");
        check(mixer.getScenes()[0].parts[0].transpose==12&&mixer.getScenes()[1].parts[0].transpose== -12&&engine.slots[2].plugin==nullptr,"Session restores both shared-source variations without loading redundant sources");
        var broken=JSON::parse(JSON::toString(saved));auto* fxArray=(*broken["slots"].getArray())[0]["fx"].getArray();PluginDescription missing;missing.name=jp("見つからない検証用FX");missing.pluginFormatName="VST3";missing.fileOrIdentifier=folder.getChildFile("missing-effect.vst3").getFullPathName();missing.isInstrument=false;missing.numInputChannels=missing.numOutputChannels=2;(*fxArray)[0].getDynamicObject()->setProperty("description",missing.createXml()->toString());
        auto invalid=folder.getChildFile("missing-fx.stagepilot");invalid.replaceWithText(JSON::toString(broken));auto* oldSource=engine.slots[0].plugin.get();auto* oldFx=chain(0).nodes[0].plugin.get();loadSession(invalid);
        check(engine.slots[0].plugin.get()==oldSource&&chain(0).nodes[0].plugin.get()==oldFx&&chain(0).count()==2&&chain(8).count()==1,"Missing FX rejects the new session and keeps the current sound and both FX chains intact");
        var malformed=JSON::parse(JSON::toString(saved));(*(*malformed["slots"].getArray())[0]["fx"].getArray())[0].getDynamicObject()->setProperty("state","invalid base64!");invalid.replaceWithText(JSON::toString(malformed));loadSession(invalid);
        check(engine.slots[0].plugin.get()==oldSource&&chain(0).nodes[0].plugin.get()==oldFx,"Malformed FX state preserves the current session instead of partially replacing it");
        mixer.createVariant(2,0,0);drain();mixer.testEditPart({10,100,7,.4f,true,false,true});engine.control(SelectScene,0);drain();makeSharedVariant();drain();check(mixer.editingScene()==3&&mixer.getScenes()[2].parts[0].transpose==7&&!mixer.getScenes()[2].registered,"Creating a shared variation preserves another unfinished draft");
        pianoPage=true;updatePage();audioStrip.focus(8);playTab.onClick();drain();check(audioStrip.focus()==0&&editSlot.getSelectedId()==1,"Returning from sequencer restores the active instrument as editor and FX target");
        suspend([&]{for(int i=2;i<slotCount;++i){auto synth=makeTestSynth();PluginDescription description;synth->fillInPluginDescription(description);description.name=jp("検証用の音源 ")+String(i+1);putPlugin(i,std::move(synth),description);}engine.prepare(48000,128);});
        for(int scene=2;scene<sceneCount;++scene){mixer.createVariant(scene,0,0);mixer.registerCurrent();drain();}
        mixerPage=mappingPage=effectsPage=pianoPage=false;updatePage();setSize(1120,700);timerCallback();padViewport.setViewPosition(0,padCanvas.getHeight());
        bool allReachable=true;int visibleCount=0;for(auto& pad:pads){visibleCount+=pad.isVisible();if(!pad.isVisible())allReachable=false;}
        allReachable=allReachable&&visibleCount==16&&padViewport.getViewPositionY()>0&&pads[15].getBottom()<=padViewport.getViewPositionY()+padViewport.getViewHeight();
        check(allReachable,"All sixteen mixed sounds remain reachable by scrolling at the minimum window size");check(snapshot(*this,jp("統合した音色一覧・16個スクロール下部.png")),"Full mixed-sound list bottom screenshot written");
        auto* report=new DynamicObject();report->setProperty("failures",failures);report->setProperty("results",results);report->setProperty("physicalAudioOutput",false);report->setProperty("cameraActivated",false);folder.getChildFile("unified-ui-tests.json").replaceWithText(JSON::toString(var(report)));dirty=false;return failures?1:0;
    }
