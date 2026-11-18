#pragma once
#include "PluginHosting.h"
#include "Locale.h"
#include <set>
#include <map>
namespace stage {
// A VST3 bundle is a single candidate. Never descend into its helper DLLs.
inline juce::Array<juce::File> findPluginCandidates(const juce::StringArray& roots,
                                                  const std::function<bool()>& cancelled) {
    juce::Array<juce::File> result;
    std::set<juce::String> seen;
    std::function<void(const juce::File&)> visit=[&](const juce::File& file){
        if(cancelled()||file.isSymbolicLink())return;
        auto key=file.getFullPathName().toLowerCase();
        if(!seen.insert(key).second)return;
        if(file.hasFileExtension("vst3")&&file.exists()){result.add(file);return;}
        if(file.isDirectory()){
            for(juce::DirectoryEntry entry:juce::RangedDirectoryIterator(file,false,"*",juce::File::findFilesAndDirectories)){
                if(cancelled())return;visit(entry.getFile());
            }
        }else if(file.hasFileExtension("dll")&&checkPluginFile(file).isEmpty())result.add(file);
    };
    for(const auto& root:roots){if(cancelled())break;visit(juce::File(root));}
    return result;
}
inline juce::StringArray defaultPluginRoots(){
    juce::StringArray roots;
    auto program=juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
    for(auto relative:{"Vstplugins","Steinberg/VstPlugins","Common Files/VST2","Common Files/VST3"})
        roots.addIfNotAlreadyThere(program.getChildFile(relative).getFullPathName(),true);
    auto local=juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA",{});
    if(local.isNotEmpty())roots.add(juce::File(local).getChildFile("Programs/Common/VST3").getFullPathName());
    roots.add("C:\\VSTPlugins");roots.add("C:\\VST64");
    auto registry=juce::WindowsRegistry::getValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\VST\\VSTPluginsPath");
    if(registry.isNotEmpty())roots.addIfNotAlreadyThere(registry,true);
    return roots;
}
class InstrumentBrowser final : public juce::Component, private juce::ListBoxModel,
                                private juce::Timer, private juce::Thread {
    static juce::String jp(const char* text){return juce::String::fromUTF8(text);}
    struct Entry {juce::File file;juce::String name,format,message;bool instrument=false;};
    juce::CriticalSection mutex;
    std::vector<Entry> discovered,entries;
    std::vector<int> visible;
    juce::String progress;
    juce::StringArray roots,customRoots;
    juce::File config;
    juce::TextEditor search;
    juce::ComboBox format;
    juce::ToggleButton failures{jp("確認できなかったファイルも表示")};
    juce::TextButton rescan{jp("もう一度検索")},stop{jp("検索を停止")},folder{jp("検索先…")},add{jp("選んだ音源を追加")};
    juce::Label title,hint,status,details;
    juce::ListBox list{jp("見つかった音源"),this};
    std::unique_ptr<juce::FileChooser> chooser;
    std::function<void(juce::File)> onSelect;
    juce::String selectedPath;
    int revision=0,shownRevision=-1;
    const bool includeDefaultRoots;
    bool ignoreCache=false, pendingScan=false, pendingForce=false;
    static bool within(const juce::File& file,const juce::StringArray& locations) {
        for(const auto& path:locations)if(file==juce::File(path)||file.isAChildOf(juce::File(path)))return true;
        return false;
    }
    void publish(Entry entry) {
        juce::ScopedLock lock(mutex);
        auto it=std::find_if(discovered.begin(),discovered.end(),[&](const Entry& e){return e.file==entry.file;});
        if(it==discovered.end())discovered.push_back(std::move(entry));else *it=std::move(entry);
        ++revision;
    }
    static Entry cachedEntry(const juce::var& item) {
        juce::File file(item["file"].toString());
        return {file,item["name"].toString(),file.hasFileExtension("dll")?"VST2":"VST3",item["message"].toString(),(bool)item["instrument"]};
    }
    void saveRoots() {
        config.getParentDirectory().createDirectory();
        if(!config.replaceWithText(customRoots.joinIntoString("\n")))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,jp("検索先"),jp("検索先を保存できませんでした。"));
        begin();
    }
    void chooseFolders() {
        chooser=std::make_unique<juce::FileChooser>(jp("追加する音源フォルダーを選択"));
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories|juce::FileBrowserComponent::canSelectMultipleItems,
            [safe=juce::Component::SafePointer<InstrumentBrowser>(this)](const juce::FileChooser& fc){
                if(!safe)return;
                bool added=false;
                for(const auto& dir:fc.getResults())if(dir.isDirectory()){
                    safe->customRoots.addIfNotAlreadyThere(dir.getFullPathName(),true);
                    added=true;
                }
                if(added)safe->saveRoots();
            });
    }
    void folderMenu() {
        juce::PopupMenu menu;
        menu.addItem(1,jp("検索先を追加…"));
        juce::PopupMenu standard;
        for(const auto& path:defaultPluginRoots())standard.addItem(1000+standard.getNumItems(),path,false);
        menu.addSubMenu(jp("標準の検索先"),standard);
        juce::PopupMenu remove;
        for(int i=0;i<customRoots.size();++i)remove.addItem(100+i,customRoots[i]);
        if(customRoots.size()>0)menu.addSubMenu(jp("追加した検索先を外す"),remove);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&folder),
            [safe=juce::Component::SafePointer<InstrumentBrowser>(this)](int id){
                if(!safe)return;
                if(id==1)safe->chooseFolders();
                if(id>=100&&id-100<safe->customRoots.size()){
                    safe->customRoots.remove(id-100);
                    safe->saveRoots();
                }
            });
    }
    static juce::String fingerprint(const juce::File& file){
        auto binary=file.isDirectory()?file.getChildFile("Contents/x86_64-win").getChildFile(file.getFileName()):file;
        return juce::String(file.getLastModificationTime().toMilliseconds())+":"+juce::String(binary.getLastModificationTime().toMilliseconds())+":"+juce::String(binary.getSize());
    }
    void run() override {
        auto candidates=findPluginCandidates(roots,[&]{return threadShouldExit();});
        auto cacheFile=config.getSiblingFile("instrument-cache.json");auto cache=juce::JSON::parse(cacheFile);
        juce::Array<juce::var> updatedCache;
        std::map<juce::String,juce::var> previous;
        if(auto* items=cache.getArray())for(const auto& item:*items)
            previous[item["file"].toString().toLowerCase()]=item;
        for(int i=0;i<candidates.size()&&!threadShouldExit();++i){
            auto file=candidates[i];
            {juce::ScopedLock lock(mutex);progress=jp("確認中 ")+juce::String(i+1)+" / "+juce::String(candidates.size())+"  "+file.getFileName();}
            const auto key=file.getFullPathName().toLowerCase();
            const auto prior=previous.find(key);
            if(!ignoreCache&&prior!=previous.end()&&(bool)prior->second["instrument"]&&prior->second["stamp"].toString()==fingerprint(file)){
                publish(cachedEntry(prior->second));updatedCache.add(prior->second);previous.erase(prior);continue;
            }
            auto report=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("StagePilot-library-"+juce::Uuid().toString()+".xml");
            juce::ChildProcess process;
            const auto start=juce::Time::getMillisecondCounter();
            bool started=process.start(juce::StringArray{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),"--scan",file.getFullPathName(),report.getFullPathName()},0);
            while(started&&process.isRunning()&&!threadShouldExit()&&juce::Time::getMillisecondCounter()-start<20000)wait(50);
            bool timedOut=started&&process.isRunning();if(timedOut)process.kill();
            auto xml=juce::parseXML(report);report.deleteFile();if(threadShouldExit())break;
            Entry entry{file,file.getFileNameWithoutExtension(),file.hasFileExtension("dll")?"VST2":"VST3",{},false};
            bool effect=false;
            if(xml)for(auto* child:xml->getChildIterator()){
                juce::PluginDescription d;if(!d.loadFromXml(*child))continue;
                if(d.isInstrument){entry.name=d.name;entry.instrument=true;entry.message=d.manufacturerName;break;}effect=true;
            }
            if(!entry.instrument)entry.message=effect?jp("エフェクト専用のため音源枠には追加できません"):timedOut?jp("20秒以内に確認できませんでした"):jp("音源として確認できませんでした");
            auto* item=new juce::DynamicObject();item->setProperty("file",file.getFullPathName());item->setProperty("stamp",fingerprint(file));item->setProperty("name",entry.name);item->setProperty("message",entry.message);item->setProperty("instrument",entry.instrument);updatedCache.add(juce::var(item));
            publish(std::move(entry));
            if(prior!=previous.end())previous.erase(prior);
        }
        // Keep unvisited entries after cancelling, and remember unplugged libraries.
        for(const auto& [key,item]:previous)if((bool)item["instrument"])updatedCache.add(item);
        cacheFile.getParentDirectory().createDirectory();
        juce::TemporaryFile temporary(cacheFile);
        if(temporary.getFile().replaceWithText(juce::JSON::toString(updatedCache)))
            temporary.overwriteTargetFileWithTemporary();
        if(!threadShouldExit()){
            juce::ScopedLock lock(mutex);
            const std::set<juce::File> found(candidates.begin(),candidates.end());
            std::erase_if(discovered,[&](const Entry& e){return found.count(e.file)==0;});
            ++revision;
        }
        {juce::ScopedLock lock(mutex);progress=threadShouldExit()?jp("検索を停止しました。見つかった音源は選べます。"):jp("検索が完了しました。音源を選び、右下の追加ボタンを押してください。");}
    }
    void begin(bool force=false){
        if(isThreadRunning()){
            pendingScan=true;pendingForce|=force;
            signalThreadShouldExit();notify();
            return;
        }
        pendingScan=false;pendingForce=false;
        ignoreCache=force;
        {juce::ScopedLock lock(mutex);progress=jp("インストール済みの音源を探しています…");}
        roots=includeDefaultRoots?defaultPluginRoots():juce::StringArray();for(const auto& root:customRoots)roots.addIfNotAlreadyThere(root,true);
        {
            juce::ScopedLock lock(mutex);
            std::erase_if(discovered,[&](const Entry& e){return !within(e.file,roots)||!e.file.exists();});
            ++revision;
        }
        auto cached=juce::JSON::parse(config.getSiblingFile("instrument-cache.json"));
        if(auto* items=cached.getArray())for(const auto& item:*items){
            auto e=cachedEntry(item);
            if(e.instrument&&within(e.file,roots)&&e.file.exists()&&item["stamp"].toString()==fingerprint(e.file))publish(std::move(e));
        }
        folder.setTooltip(jp("現在の検索先：\n")+roots.joinIntoString("\n"));startThread(juce::Thread::Priority::low);timerCallback();
    }
    void filter(){
        visible.clear();auto query=search.getText().trim();
        for(size_t i=0;i<entries.size();++i){auto& e=entries[i];
            if(!e.instrument&&!failures.getToggleState())continue;
            if(format.getSelectedId()==2&&e.format!="VST2")continue;
            if(format.getSelectedId()==3&&e.format!="VST3")continue;
            if(query.isNotEmpty()&&!(e.name+" "+e.message+" "+e.file.getFullPathName()).containsIgnoreCase(query))continue;
            visible.push_back((int)i);
        }
        list.updateContent();int row=-1;
        for(size_t i=0;i<visible.size();++i)if(entries[(size_t)visible[i]].file.getFullPathName()==selectedPath)row=(int)i;
        if(row>=0)list.selectRow(row);else list.deselectAllRows();
        selectedRowsChanged(row);list.repaint();
    }
    void timerCallback() override {
        if(pendingScan&&!isThreadRunning()){begin(pendingForce);return;}
        bool changed=false;juce::String message;
        {juce::ScopedLock lock(mutex);message=progress;if(shownRevision!=revision){entries=discovered;shownRevision=revision;changed=true;}}
        if(changed)filter();int count=0;for(const auto& e:entries)if(e.instrument)++count;
        status.setText(jp("音源 ")+juce::String(count)+jp(" 件  ·  ")+message,juce::dontSendNotification);
        bool running=isThreadRunning();stop.setEnabled(running);
    }
    int getNumRows() override{return (int)visible.size();}
    void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected) override {
        if(row<0||row>=getNumRows())return;auto& e=entries[(size_t)visible[(size_t)row]];
        g.fillAll(selected?juce::Colour(0xff28513f):juce::Colour(row%2?0xff202933:0xff1b222b));
        g.setColour(e.instrument?juce::Colour(0xffeef3f6):juce::Colour(0xffadb3bb));g.setFont(uiFont(17));
        g.drawText(e.name,12,4,w-108,25,juce::Justification::centredLeft);
        g.setFont(uiFont(13));g.drawText(e.format,w-82,4,70,25,juce::Justification::centredRight);
        g.setColour(juce::Colour(0xffa8b5c4));g.drawText(e.message+"  ·  "+e.file.getFullPathName(),12,30,w-24,h-34,juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override {
        bool valid=row>=0&&row<getNumRows();add.setEnabled(valid&&entries[(size_t)visible[(size_t)row]].instrument);
        if(valid){auto& e=entries[(size_t)visible[(size_t)row]];selectedPath=e.file.getFullPathName();details.setText(selectedPath,juce::dontSendNotification);}
        else{selectedPath.clear();details.setText(jp("名前で絞り込めます。見つからないときは「検索先…」を押してください。"),juce::dontSendNotification);}
    }
    void addSelected(){auto row=list.getSelectedRow();if(row>=0&&row<getNumRows()){auto e=entries[(size_t)visible[(size_t)row]];if(e.instrument&&onSelect)onSelect(e.file);}}
    void listBoxItemDoubleClicked(int,const juce::MouseEvent&) override{addSelected();}
public:
    InstrumentBrowser(std::function<void(juce::File)> select,const juce::File& configFile,bool automatic=true,bool includeStandard=true)
      :juce::Thread("Instrument search"),config(configFile),onSelect(std::move(select)),includeDefaultRoots(includeStandard){
        for(auto* c:std::vector<juce::Component*>{&search,&format,&failures,&rescan,&stop,&folder,&add,&title,&hint,&status,&details,&list})addAndMakeVisible(c);
        title.setText(jp("使いたい音源を選ぶ"),juce::dontSendNotification);title.setFont(uiFont(23));
        hint.setText(jp("VST2・VST3の標準フォルダーを自動検索します。音源の確認は別プロセスで行います。"),juce::dontSendNotification);
        search.setTextToShowWhenEmpty(jp("音源名を入力（例：Keyzone）"),juce::Colour(0xff94a2b3));search.onTextChange=[this]{filter();};
        format.addItem(jp("すべての形式"),1);format.addItem("VST2",2);format.addItem("VST3",3);format.setSelectedId(1);format.onChange=[this]{filter();};
        failures.onClick=[this]{filter();};
        rescan.onClick=[this]{begin(true);};
        stop.onClick=[this]{pendingScan=false;pendingForce=false;signalThreadShouldExit();notify();};
        add.onClick=[this]{addSelected();};
        folder.onClick=[this]{folderMenu();};
        if(config.existsAsFile()){customRoots=juce::StringArray::fromLines(config.loadFileAsString());customRoots.removeEmptyStrings();}
        list.setRowHeight(64);add.setEnabled(false);setSize(920,650);startTimer(120);if(automatic)begin();
    }
    ~InstrumentBrowser() override{stopTimer();signalThreadShouldExit();notify();stopThread(-1);list.setModel(nullptr);}
    void paint(juce::Graphics& g) override{g.fillAll(juce::Colour(0xff101419));}
    void resized() override{
        title.setBounds(22,14,getWidth()-44,34);hint.setBounds(22,50,getWidth()-44,26);
        search.setBounds(22,88,getWidth()-218,34);format.setBounds(getWidth()-184,88,162,34);
        rescan.setBounds(22,135,150,32);folder.setBounds(184,135,150,32);stop.setBounds(346,135,132,32);failures.setBounds(492,135,getWidth()-514,32);
        status.setBounds(22,178,getWidth()-44,40);list.setBounds(22,224,getWidth()-44,getHeight()-326);
        details.setBounds(22,getHeight()-94,getWidth()-44,30);add.setBounds(getWidth()-246,getHeight()-52,224,34);
    }
    bool testFiltering(const juce::File& snapshot){
        stopTimer();entries={{juce::File("C:/VST/Test Piano.dll"),"Test Piano","VST2","Example",true},{juce::File("C:/VST/Test Synth.vst3"),"Test Synth","VST3","Example",true},{juce::File("C:/VST/Helper.dll"),"Helper","VST2",jp("音源として確認できませんでした"),false}};
        filter();bool ok=getNumRows()==2;format.setSelectedId(2,juce::dontSendNotification);filter();ok&=getNumRows()==1;
        search.setText("piano",false);filter();list.selectRow(0);ok&=add.isEnabled();addSelected();
        search.setText("missing",false);filter();ok&=getNumRows()==0&&!add.isEnabled();search.setText({},false);format.setSelectedId(1,juce::dontSendNotification);failures.setToggleState(true,juce::dontSendNotification);filter();list.selectRow(2);ok&=getNumRows()==3&&!add.isEnabled();
        list.selectRow(0);auto img=createComponentSnapshot(getLocalBounds());juce::FileOutputStream out(snapshot);out.setPosition(0);out.truncate();juce::PNGImageFormat png;return png.writeImageToStream(img,out)&&ok;
    }
    bool testRescan(const juce::File& output) {
        const bool first=testInstalled(output);
        const auto foundMarker=[&]{
            return std::any_of(entries.begin(),entries.end(),[](const Entry& e){return e.name=="ScanMarker";});
        };
        bool both=foundMarker()&&customRoots.size()==2;
        search.setText({},false);
        begin(true);
        bool retains=std::any_of(entries.begin(),entries.end(),[](const Entry& e){return e.instrument&&e.name.containsIgnoreCase("Keyzone");});
        begin(true); // Request another scan while the first is still running.
        const auto started=juce::Time::getMillisecondCounter();
        while((pendingScan||isThreadRunning())&&juce::Time::getMillisecondCounter()-started<45000)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        timerCallback();
        bool retry=!pendingScan&&!isThreadRunning()&&foundMarker();
        auto report=std::make_unique<juce::DynamicObject>();
        const bool success=first&&both&&retains&&retry;
        report->setProperty("foundBothRoots",both);
        report->setProperty("retainedDuringRefresh",retains);
        report->setProperty("restartCompleted",retry);
        report->setProperty("passed",success);
        output.getChildFile("rescan.json").replaceWithText(juce::JSON::toString(juce::var(report.release())));
        return success;
    }
    bool testInstalled(const juce::File& output){
        auto started=juce::Time::getMillisecondCounter();
        while(isThreadRunning()&&juce::Time::getMillisecondCounter()-started<600000)juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        bool finished=!isThreadRunning();if(!finished){signalThreadShouldExit();notify();stopThread(-1);}timerCallback();
        juce::Array<juce::var> values;bool keyzone=false;
        for(const auto& e:entries){auto* item=new juce::DynamicObject();item->setProperty("name",e.name);item->setProperty("file",e.file.getFullPathName());item->setProperty("instrument",e.instrument);item->setProperty("detail",e.message);values.add(juce::var(item));keyzone|=e.instrument&&e.name.containsIgnoreCase("Keyzone");}
        output.createDirectory();output.getChildFile("catalog.json").replaceWithText(juce::JSON::toString(values));
        search.setText("Keyzone",false);filter();if(getNumRows()>0)list.selectRow(0);
        auto img=createComponentSnapshot(getLocalBounds());juce::FileOutputStream out(output.getChildFile(jp("音源一覧.png")));out.setPosition(0);out.truncate();juce::PNGImageFormat png;
        return png.writeImageToStream(img,out)&&finished&&keyzone;
    }
};
}
