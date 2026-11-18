#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
namespace stage {
inline juce::Colour instrumentColour(int index){static constexpr juce::uint32 colours[]={0xff72d6c2,0xffffbb70,0xff94baff,0xffeaa0db,0xffc3d77a,0xffbdabff,0xffff9299,0xff7dd7ef};return juce::Colour(colours[index%8]);}
constexpr int sceneCount=8;
struct ScenePart {
    int low,high,transpose;
    float gain;
    bool enabled,ignorePedal,included;
};
struct Scene {Scene(){parts.fill({0,127,0,1,false,false});}juce::String name;bool registered=false;int splitPoint=60;std::array<ScenePart,8> parts;};
inline bool validPart(ScenePart p){return p.low>=0&&p.high<=127&&p.low<=p.high&&p.transpose>=-24&&p.transpose<=24&&std::isfinite(p.gain)&&p.gain>=0&&p.gain<=1;}
inline juce::var partsJson(const std::array<ScenePart,8>& parts){juce::Array<juce::var> a;for(auto p:parts){auto* o=new juce::DynamicObject();o->setProperty("enabled",p.enabled);o->setProperty("included",p.included||p.enabled);o->setProperty("ignorePedal",p.ignorePedal);o->setProperty("low",p.low);o->setProperty("high",p.high);o->setProperty("transpose",p.transpose);o->setProperty("gain",p.gain);a.add(juce::var(o));}return a;}
inline bool readParts(const juce::var& value,std::array<ScenePart,8>& result){auto* a=value.getArray();if(!a||a->size()!=8)return false;std::array<ScenePart,8> pending;for(int i=0;i<8;++i){auto v=(*a)[i];ScenePart p{(int)v["low"],(int)v["high"],(int)v["transpose"],(float)v["gain"],(bool)v["enabled"],(bool)v.getProperty("ignorePedal",false),(bool)v.getProperty("included",v["enabled"])};if(!v.hasProperty("low")||!v.hasProperty("high")||!validPart(p))return false;pending[(size_t)i]=p;}result=pending;return true;}
inline juce::var scenesJson(const std::array<Scene,sceneCount>& scenes){juce::Array<juce::var> a;for(auto& s:scenes){auto* o=new juce::DynamicObject();o->setProperty("name",s.name);o->setProperty("registered",s.registered);o->setProperty("splitPoint",s.splitPoint);o->setProperty("parts",partsJson(s.parts));a.add(juce::var(o));}return a;}
inline bool readScenes(const juce::var& value,std::array<Scene,sceneCount>& result){auto* a=value.getArray();if(!a||a->size()!=sceneCount)return false;std::array<Scene,sceneCount> pending;for(int i=0;i<sceneCount;++i){auto v=(*a)[i];pending[(size_t)i].splitPoint=(int)v.getProperty("splitPoint",60);if(pending[(size_t)i].splitPoint<1||pending[(size_t)i].splitPoint>127)return false;if(!readParts(v["parts"],pending[(size_t)i].parts))return false;pending[(size_t)i].registered=v.hasProperty("registered")?(bool)v["registered"]:std::any_of(pending[(size_t)i].parts.begin(),pending[(size_t)i].parts.end(),[](auto part){return part.enabled;});pending[(size_t)i].name=v["name"].toString().substring(0,40);}result=pending;return true;}
}
