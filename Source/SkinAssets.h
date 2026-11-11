#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace SkratchSkin
{
constexpr int width = 1060, height = 856;
constexpr float deckScale = 0.4f, mixerScale = 0.465f;
inline const juce::Point<float> deckOrigin{51, 114}, mixerOrigin{579, 122};
inline const juce::Point<float> deckAxis{612.57f, 732.65f};
constexpr float platterDiameter = 343.0f;
inline const juce::Rectangle<int> mixerCrop{21, 27, 835, 1026};

enum class FontRole
{
    display,
    control,
    label,
    data,
    lcd,
    hardwareButton
};
juce::Font font(FontRole, float);
void text(juce::Graphics &, FontRole, const juce::String &, juce::Rectangle<int>, float, juce::Colour,
          int justification = juce::Justification::centredLeft);
juce::Point<float> deckPoint(float, float);
juce::Point<float> mixerPoint(float, float);
juce::Rectangle<int> around(juce::Point<float>, int width, int height);
juce::Rectangle<int> sliderRail(juce::Point<float> start, juce::Point<float> end, bool vertical,
                                int crossSize, int margin);
void image(juce::Graphics &, const juce::Image &, juce::Rectangle<float>);
void rotate(juce::Graphics &, const juce::Image &, juce::Point<float>, float diameter, float angle);
void drawPlatter(juce::Graphics &, juce::Point<float>, float angle);

struct Art
{
    juce::Image deck, mixer, metal, vinyl, logo, knob, cap, crossCap, deckCap, deckPitchCap;
};
const Art &art();
} // namespace SkratchSkin
