#pragma once
#include <JuceHeader.h>
namespace stage {
class ResetSlider final:public juce::Slider {
    struct ValueListener final:juce::MouseListener {
        ResetSlider& owner;
        explicit ValueListener(ResetSlider& s):owner(s){}
        void mouseDoubleClick(const juce::MouseEvent& e)override{
            if(e.eventComponent==&owner||owner.getSliderStyle()!=juce::Slider::IncDecButtons)return;
            for(auto* c=e.eventComponent;c&&c!=&owner;c=c->getParentComponent())if(dynamic_cast<juce::Button*>(c))return;
            owner.resetIncrementValue();
        }
    } valueListener{*this};
    void resetIncrementValue(){if(!isEnabled()||!isDoubleClickReturnEnabled())return;hideTextBox(true);if(onDragStart)onDragStart();setValue(getDoubleClickReturnValue(),juce::sendNotificationSync);if(onDragEnd)onDragEnd();}
public:
    ResetSlider(){addMouseListener(&valueListener,true);}
    ~ResetSlider()override{removeMouseListener(&valueListener);}
    void mouseDoubleClick(const juce::MouseEvent& e)override{if(getSliderStyle()==juce::Slider::IncDecButtons)resetIncrementValue();else juce::Slider::mouseDoubleClick(e);}
    bool testValueDoubleClick(){for(int i=0;i<getNumChildComponents();++i)if(auto* label=dynamic_cast<juce::Label*>(getChildComponent(i))){auto p=label->getLocalBounds().toFloat().getCentre();juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys::leftButtonModifier,0,0,0,0,0,label,label,juce::Time::getCurrentTime(),p,juce::Time::getCurrentTime(),2,false);static_cast<juce::Component*>(label)->mouseDoubleClick(e);valueListener.mouseDoubleClick(e);return getValue()==getDoubleClickReturnValue()&&!label->isBeingEdited();}return false;}
};
inline void enableReset(juce::Slider& slider,double value){slider.setDoubleClickReturnValue(true,value,juce::ModifierKeys::noModifiers);}
inline void simulateDoubleClick(juce::Slider& slider){auto p=slider.getLocalBounds().toFloat().getCentre();juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys::leftButtonModifier,0,0,0,0,0,&slider,&slider,juce::Time::getCurrentTime(),p,juce::Time::getCurrentTime(),2,false);slider.mouseDoubleClick(e);}
}
