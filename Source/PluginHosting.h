#pragma once
#include <JuceHeader.h>
namespace stage {
inline void addPluginFormats(juce::AudioPluginFormatManager& formats){
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    formats.addFormat(std::make_unique<juce::VSTPluginFormat>());
}
// Inspect the PE header without loading arbitrary DLL code into the UI process.
inline juce::String checkPluginFile(const juce::File& file){
    auto jp=[](const char* text){return juce::String::fromUTF8(text);};
    if(file.hasFileExtension("vst3")&&file.exists())return {};
    if(!file.hasFileExtension("dll")||!file.existsAsFile())return jp("VST3（.vst3）または64bit VST2（.dll）の音源を選んでください。");
    juce::FileInputStream stream(file);
    if(!stream.openedOk()||stream.getTotalLength()<64||stream.readShort()!=0x5a4d)return jp("このDLLはWindows用のプラグインではありません。");
    stream.setPosition(60);auto offset=stream.readInt();
    if(offset<64||offset>stream.getTotalLength()-24)return jp("DLLの形式を読み取れません。正しい音源ファイルを選んでください。");
    stream.setPosition(offset);if(stream.readInt()!=0x4550)return jp("DLLの形式を読み取れません。");
    auto machine=(uint16_t)stream.readShort();
    if(machine==0x014c)return jp("これは32bit版のVST2です。StagePilotでは64bit版（x64）のDLLを選んでください。32bit用ブリッジは未対応です。");
    if(machine!=0x8664)return jp("このDLLのCPU形式には対応していません。Windows x64版を選んでください。");
    return {};
}
inline void scanPlugin(const juce::File& file,juce::OwnedArray<juce::PluginDescription>& types){
    if(file.hasFileExtension("dll")){juce::VSTPluginFormat format;format.findAllTypesForFile(types,file.getFullPathName());}
    else{juce::VST3PluginFormat format;format.findAllTypesForFile(types,file.getFullPathName());}
}
}
