#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>

namespace stage {
// The em height is shared by the two applications. Using the font's 1.45-em
// hhea line box as the JUCE height made Japanese glyphs unexpectedly small.
inline juce::FontOptions uiFont(float height,int style=juce::Font::plain) {
    return juce::FontOptions(height,style).withAscentOverride(.88f).withDescentOverride(.12f);
}
struct UiTypefaces {
    juce::Typeface::Ptr regular=juce::Typeface::createSystemTypefaceFor(BinaryData::ZenKakuGothicNewRegular_ttf,BinaryData::ZenKakuGothicNewRegular_ttfSize);
    juce::Typeface::Ptr bold=juce::Typeface::createSystemTypefaceFor(BinaryData::ZenKakuGothicNewBold_ttf,BinaryData::ZenKakuGothicNewBold_ttfSize);
    static UiTypefaces& get(){static UiTypefaces faces;return faces;}
};
}
