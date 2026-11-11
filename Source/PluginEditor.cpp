#include "PluginEditor.h"
using namespace NeedleTheme;

using SkratchSkin::FontRole;
namespace
{
enum Hardware
{
    rotary,
    mixerVertical,
    mixerCross,
    deckVertical,
    deckHorizontal
};
const juce::Identifier hardwareId{"skratchHardware"};
const juce::Identifier physicalButtonId{"skratchPhysicalButton"};
const juce::Identifier mixerButtonId{"skratchMixerButton"};
const juce::Identifier commandIconId{"skratchCommandIcon"};
namespace MixerLayout
{
// Socket and rail positions are measured in the cropped mixer atlas.
constexpr std::array<float, 4> channelX{263, 377, 490, 602};
constexpr float inputX = 118, lowerMotorX = 764;
constexpr float topKnob = 182, topReadout = 230, eqKnob = 322, eqLegend = 368;
constexpr float performanceKnob = 461, channelPlaque = 646, railTop = 706, railBottom = 884,
                channelReadout = 911;
constexpr float crossStart = 349, crossEnd = 519, crossRow = 992;
constexpr int bankRow = 520, buttonPitch = 38, buttonHeight = 23;

juce::Rectangle<int> sideButton(float atlasX, int y, int width)
{
    const auto centre = SkratchSkin::mixerPoint(atlasX, 0).x;
    return {juce::roundToInt(centre - width * 0.5f), y, width, buttonHeight};
}

juce::Rectangle<int> crossLegend(bool left)
{
    constexpr int labelWidth = 44, gutter = 21, height = 16;
    const auto end = SkratchSkin::mixerPoint(left ? crossStart : crossEnd, crossRow);
    return {juce::roundToInt(end.x) + (left ? -labelWidth - gutter : gutter),
            juce::roundToInt(end.y) - height / 2, labelWidth, height};
}
}
using namespace MixerLayout;
const std::array<const char *, 4> knobNames{"BPM", "CUT-IN", "CURVE", "SWING"};
const std::array<const char *, 4> fxNames{"TIME", "FEEDBACK", "DRIVE", "BIT DEPTH"};
const std::array<const char *, 4> eqNames{"LOW", "MID", "HIGH", "FILTER"};
constexpr std::array<dj::Param, 4> fxParams{dj::echoMs, dj::feedback, dj::drive, dj::bits};
constexpr std::array<dj::Param, 4> eqParams{dj::low, dj::mid, dj::high, dj::filter};
struct MixerPosition
{
    dj::Param parameter;
    const char *label;
    float x, y;
};
constexpr std::array<MixerPosition, 4> inputs{{{dj::inputGain, "IN GAIN", inputX, 270},
                                               {dj::inputHp, "HIGH PASS", inputX, 343},
                                               {dj::inputLp, "LOW PASS", inputX, 409},
                                               {dj::noiseThreshold, "THRESHOLD", inputX, 477}}};
constexpr std::array<MixerPosition, 5> motors{{{dj::torque, "TORQUE", 707, 182},
                                               {dj::startMs, "START", 707, 254},
                                               {dj::brakeMs, "BRAKE", lowerMotorX, 660},
                                               {dj::spinSpeed, "SPINBACK", lowerMotorX, 752},
                                               {dj::friction, "FRICTION", lowerMotorX, 835}}};
constexpr std::array<MixerPosition, 4> performanceSlots{{{dj::morphAmount, "MORPH", channelX[0], performanceKnob},
                                                         {dj::mix, "FX MIX", channelX[1], performanceKnob},
                                                         {dj::duty, "STUTTER DUTY", channelX[2], performanceKnob},
                                                         {dj::resonance, "RESONANCE", channelX[3], performanceKnob}}};
const std::array<const char *, 4> faderNames{"MASTER", "PITCH", "SCRATCH", "RESPONSE"};
void lcd(juce::Graphics &g, const juce::String &value, juce::Rectangle<int> bounds, float size)
{
    SkratchSkin::text(g, FontRole::lcd, value, bounds, size, teal, juce::Justification::centred);
}

const juce::Colour silkscreen{0xfff4f2eb};
const juce::Colour deckInk{0xff15181b};

void legend(juce::Graphics &g, FontRole role, const juce::String &value, juce::Rectangle<int> bounds,
            float size, juce::Colour colour = silkscreen,
            juce::Justification justification = juce::Justification::centred)
{
    if (!g.clipRegionIntersects(bounds))
        return;
    auto face = SkratchSkin::font(role, size);
    const auto naturalWidth = juce::GlyphArrangement::getStringWidth(face, value);
    if (naturalWidth > bounds.getWidth())
        face.setHeight(size * static_cast<float>(bounds.getWidth()) / naturalWidth);
    g.setFont(face);
    g.setColour(colour);
    g.drawText(value, bounds, justification, false);
}

void hardwareLegends(juce::Graphics &g)
{
    using namespace SkratchSkin;
    legend(g, FontRole::display, "DIRECT DRIVE", {95, 125, 148, 18}, 15, deckInk,
           juce::Justification::centredLeft);
    legend(g, FontRole::label, "PITCH", around(deckPoint(875, 66), 56, 16), 15, deckInk);
    legend(g, FontRole::label, "SENS.", {475, 569, 52, 16}, 14, deckInk);
    legend(g, FontRole::display, "PERFORMANCE MIXER", {668, 148, 218, 19}, 17);
    for (size_t i = 0; i < channelX.size(); ++i)
        legend(g, FontRole::hardwareButton, faderNames[i],
               around(mixerPoint(channelX[i], channelPlaque), 34, 16), 17);
    legend(g, FontRole::label, "CROSSFADER", around(mixerPoint((crossStart + crossEnd) * 0.5f, crossRow).translated(0, -20), 124, 17), 17);
}

const juce::Image &chassis()
{
    static const auto background = []
    {
        using namespace SkratchSkin;
        const auto &a = art();
        juce::Image image(juce::Image::RGB, width * 2, height * 2, false);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(2.0f));
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.fillAll(xpPanel);
        juce::ColourGradient toolbar(juce::Colour(0xfff5f8ff), 0, 0,
                                     juce::Colour(0xffa9c6ef), 0, 71, false);
        toolbar.addColour(.53, juce::Colour(0xffdbe8fa));
        g.setGradientFill(toolbar);
        g.fillRect(0, 0, width, 72);
        g.setColour(juce::Colour(0xff456faa));
        g.drawHorizontalLine(71, 0, static_cast<float>(width));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4874ac), 0, 72,
                                               juce::Colour(0xff1e477c), 0, 612, false));
        g.fillRect(0, 72, width, 542);
        const auto well = juce::Rectangle<float>(19, 114, width - 38, 495);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff29384e), 0, 114,
                                               juce::Colour(0xff17263a), 0, 609, false));
        g.fillRoundedRectangle(well, 6);
        g.setColour(juce::Colour(0xff182d4c));
        g.drawRoundedRectangle(well.reduced(.5f), 6, 1);
        g.setColour(juce::Colour(0xff7097cc));
        g.drawRoundedRectangle(well.expanded(1), 7, 1);
        g.setColour(juce::Colour(0xff779ccd));
        g.fillRect(0, 613, width, 2);
        juce::ColourGradient footer(juce::Colour(0xffe4eeff), 0, 829,
                                    juce::Colour(0xffb4cbee), 0, height, false);
        g.setGradientFill(footer);
        g.fillRect(0, 829, width, height - 829);
        g.setColour(xpBorder);
        g.drawHorizontalLine(829, 0, static_cast<float>(width));
        const auto brand = juce::Rectangle<float>(19, 5, 365, 58);
        g.setColour(juce::Colour(0xff200e13));
        g.fillRoundedRectangle(brand, 4);
        g.setColour(juce::Colour(0xff5378ab));
        g.drawRoundedRectangle(brand.reduced(.5f), 4, 1);
        SkratchSkin::image(g, a.logo, {30, 6, 343, 56});
        const auto devices = juce::Rectangle<float>(403, 13, 335, 44);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffbfdff), 0, 13,
                                               juce::Colour(0xffcfdef5), 0, 57, false));
        g.fillRoundedRectangle(devices, 5);
        g.setColour(juce::Colour(0xff7898c7));
        g.drawRoundedRectangle(devices.reduced(.5f), 5, 1);
        for (int x : {513, 627})
            g.drawVerticalLine(x, 19.0f, 51.0f);
        g.setColour(groove);
        g.fillRect(19, 74, width - 38, 42);
        g.setColour(chrome.withAlpha(0.45f));
        g.drawRect(19, 74, width - 38, 42);
        g.setOpacity(1.0f);
        g.drawImageTransformed(
            a.deck, juce::AffineTransform::scale(deckScale).translated(deckOrigin.x, deckOrigin.y));
        g.drawImageTransformed(
            a.mixer, juce::AffineTransform::scale(mixerScale).translated(mixerOrigin.x, mixerOrigin.y));
        g.setColour(xpWhite);
        g.fillRect(20, 616, width - 40, 37);
        g.setColour(xpBorder);
        g.drawRect(20, 616, width - 40, 37);
        return image;
    }();
    return background;
}
} // namespace

juce::Font NeedleTheme::font(float size, bool bold)
{
#if JUCE_WINDOWS
    return juce::Font(juce::FontOptions("Tahoma", size, bold ? juce::Font::bold : juce::Font::plain));
#else
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
#endif
}

void NeedleTheme::text(juce::Graphics &g, const juce::String &s, juce::Rectangle<int> r, float size,
                       juce::Colour c, int justify)
{
    g.setColour(c);
    g.setFont(font(size));
    g.drawFittedText(s, r, justify, 1);
}

void NeedleTheme::drawXpTitle(juce::Graphics &g, juce::Rectangle<float> r, bool active)
{
    juce::ColourGradient blue(juce::Colour(active ? 0xff6eb0ff : 0xff91a8ce), 0, r.getY(),
                              juce::Colour(active ? 0xff1551bd : 0xff627ba4), 0, r.getBottom(), false);
    blue.addColour(.23, juce::Colour(active ? 0xff3389f5 : 0xff819ac6));
    blue.addColour(.75, juce::Colour(active ? 0xff215fda : 0xff6f8ab8));
    g.setGradientFill(blue);
    g.fillRect(r);
    g.setColour(juce::Colour(0xff123d89));
    g.drawHorizontalLine(juce::roundToInt(r.getBottom()) - 1, r.getX(), r.getRight());
}

namespace
{
void xpButton(juce::Graphics &g, juce::Rectangle<float> r, bool on, bool hover, bool down, bool focus,
              bool enabled)
{
    const auto top = juce::Colour(down ? 0xffc0d3ed : (on ? 0xfff3f9ff : 0xfffffff9));
    const auto bottom = juce::Colour(down ? 0xffe5eef9 : (on ? 0xff94b9ef : 0xffdedac7));
    const float opacity = enabled ? 1.0f : .55f;
    juce::ColourGradient fill(top.withAlpha(opacity), 0, r.getY(), bottom.withAlpha(opacity),
                               0, r.getBottom(), false);
    fill.addColour(.51, juce::Colour(on ? 0xffcfe3ff : 0xfff7f5ec).withAlpha(opacity));
    g.setGradientFill(fill);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour((focus ? xpFocus : (hover ? xpBlue : juce::Colour(0xff5678a7))).withAlpha(opacity));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(down ? .16f : .68f));
    g.drawRoundedRectangle(r.reduced(1.0f), 3.0f, 1.0f);
    if (hover && enabled && !down)
    {
        g.setColour(juce::Colour(0xfff6ce79));
        g.drawRoundedRectangle(r.reduced(1.3f), 2.0f, 1.0f);
    }
}
}

NeedleLook::NeedleLook()
{
    setColourScheme(getLightColourScheme());
    setColour(juce::ResizableWindow::backgroundColourId, xpPanel);
    setColour(juce::DocumentWindow::textColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, xpPanel);
    setColour(juce::TextButton::buttonOnColourId, xpBorder);
    setColour(juce::TextButton::textColourOffId, xpInk);
    setColour(juce::TextButton::textColourOnId, xpInk);
    setColour(juce::ToggleButton::textColourId, xpInk);
    setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff2d7217));
    setColour(juce::ToggleButton::tickDisabledColourId, xpBorder);
    setColour(juce::Slider::textBoxTextColourId, xpInk);
    setColour(juce::Slider::textBoxBackgroundColourId, xpWhite);
    setColour(juce::Slider::textBoxOutlineColourId, xpBorder);
    setColour(juce::ComboBox::backgroundColourId, xpWhite);
    setColour(juce::ComboBox::textColourId, xpInk);
    setColour(juce::ComboBox::outlineColourId, xpBorder);
    setColour(juce::ComboBox::arrowColourId, xpBlue);
    setColour(juce::PopupMenu::backgroundColourId, xpWhite);
    setColour(juce::PopupMenu::textColourId, xpInk);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, xpBlue);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::Label::textColourId, xpInk);
    setColour(juce::ListBox::backgroundColourId, xpWhite);
    setColour(juce::ListBox::textColourId, xpInk);
    setColour(juce::TabbedComponent::backgroundColourId, xpPanel);
    setColour(juce::TabbedComponent::outlineColourId, xpBorder);
    setColour(juce::ScrollBar::thumbColourId, xpBorder);
    setColour(juce::ScrollBar::trackColourId, xpWhite);
    setColour(juce::AlertWindow::backgroundColourId, xpPanel);
    setColour(juce::AlertWindow::textColourId, xpInk);
    setColour(juce::AlertWindow::outlineColourId, xpBorder);
}

juce::Font NeedleLook::getTextButtonFont(juce::TextButton &, int height)
{
    return font(juce::jlimit(12.0f, 15.0f, height * .53f), true);
}
juce::Font NeedleLook::getComboBoxFont(juce::ComboBox &)
{
    return font(14);
}
juce::Font NeedleLook::getLabelFont(juce::Label &label)
{
    return font(juce::jmax(13.0f, label.getFont().getHeight()), label.getFont().isBold());
}
juce::Font NeedleLook::getPopupMenuFont()
{
    return font(15);
}
int NeedleLook::getTabButtonBestWidth(juce::TabBarButton &button, int)
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font(13, true),
                                                                         button.getButtonText()))) + 28;
}
void NeedleLook::drawTabButton(juce::TabBarButton &button, juce::Graphics &g, bool hover, bool down)
{
    const auto r = button.getActiveArea().toFloat().reduced(1, 0);
    const bool selected = button.isFrontTab();
    if (selected)
    {
        g.setColour(xpPanel);
        g.fillRoundedRectangle(r, 4);
        g.setColour(button.hasKeyboardFocus(false) ? xpFocus : xpBorder);
        g.drawRoundedRectangle(r.reduced(.5f), 4, 1);
        g.setColour(xpPanel);
        g.fillRect(r.withTop(r.getBottom() - 3).reduced(1, 0));
        g.setColour(juce::Colour(0xffeca845));
        g.fillRect(r.withHeight(3).reduced(4, 0));
    }
    else
        xpButton(g, r.withTrimmedTop(3), false, hover, down,
                 button.hasKeyboardFocus(false), button.isEnabled());
    g.setFont(font(13, true));
    g.setColour(xpInk.withAlpha(button.isEnabled() ? 1.0f : .4f));
    g.drawFittedText(button.getButtonText(), button.getTextArea(), juce::Justification::centred, 1);
}
void NeedleLook::drawComboBox(juce::Graphics &g, int w, int h, bool down, int, int, int, int,
                               juce::ComboBox &box)
{
    auto r = juce::Rectangle<float>(0, 0, static_cast<float>(w), static_cast<float>(h)).reduced(.5f);
    g.setColour(box.isEnabled() ? xpWhite : xpPanel);
    g.fillRoundedRectangle(r, 2);
    g.setColour(box.hasKeyboardFocus(true) ? xpFocus : xpBorder);
    g.drawRoundedRectangle(r, 2, 1);
    auto arrow = r.removeFromRight(21).reduced(1);
    xpButton(g, arrow, true, box.isMouseOver(), down, false, box.isEnabled());
    auto c = arrow.getCentre();
    juce::Path caret;
    caret.addTriangle(c.x - 4, c.y - 2, c.x + 4, c.y - 2, c.x, c.y + 3);
    g.setColour(xpInk.withAlpha(box.isEnabled() ? 1.0f : .4f));
    g.fillPath(caret);
}
void NeedleLook::drawTickBox(juce::Graphics &g, juce::Component &component, float x, float y, float w, float h,
                              bool on, bool enabled, bool hover, bool down)
{
    const auto r = juce::Rectangle<float>(x, y, w, h).reduced(1);
    xpButton(g, r, false, hover, down, component.hasKeyboardFocus(true), enabled);
    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath(r.getX() + w * .18f, r.getCentreY());
        tick.lineTo(r.getX() + w * .37f, r.getY() + h * .68f);
        tick.lineTo(r.getX() + w * .73f, r.getY() + h * .23f);
        g.setColour(findColour(enabled ? juce::ToggleButton::tickColourId : juce::ToggleButton::tickDisabledColourId));
        g.strokePath(tick, juce::PathStrokeType(1.9f));
    }
}
void NeedleLook::drawToggleButton(juce::Graphics &g, juce::ToggleButton &b, bool hover, bool down)
{
    drawTickBox(g, b, 1, (b.getHeight() - 16) * .5f, 16, 16, b.getToggleState(), b.isEnabled(), hover, down);
    text(g, b.getButtonText(), b.getLocalBounds().withTrimmedLeft(22), 13,
         xpInk.withAlpha(b.isEnabled() ? 1.0f : .5f));
}
void NeedleLook::drawDocumentWindowTitleBar(juce::DocumentWindow &w, juce::Graphics &g, int width, int height,
                                            int x, int available, const juce::Image *, bool left)
{
    drawXpTitle(g, {0, 0, static_cast<float>(width), static_cast<float>(height)}, w.isActiveWindow());
    g.setFont(font(14, true));
    g.setColour(juce::Colours::white);
    g.drawFittedText(w.getName(), {x + 5, 0, juce::jmax(1, available - 10), height},
                     left ? juce::Justification::centredLeft : juce::Justification::centred, 1);
}
int NeedleLook::getSliderThumbRadius(juce::Slider &slider)
{
    if (slider.getProperties().contains(hardwareId))
        return 12;
    return LookAndFeel_V4::getSliderThumbRadius(slider);
}
void NeedleLook::drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &,
                                      bool hover, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    if (b.getProperties().contains(physicalButtonId))
    {
        const bool round = static_cast<bool>(b.getProperties()[physicalButtonId]);
        g.setColour(juce::Colours::black.withAlpha(down ? 0.82f : 0.61f));
        if (round)
            g.fillEllipse(r);
        else
            g.fillRect(r);
        if (hover || b.hasKeyboardFocus(true) || b.getToggleState())
        {
            g.setColour(b.getToggleState() ? accent : teal);
            if (round)
                g.drawEllipse(r, 1.5f);
            else
                g.drawRect(r, 1.5f);
        }
        return;
    }
    if (b.getProperties().contains(mixerButtonId))
    {
        const bool on = b.getToggleState();
        const auto top = juce::Colour(on ? 0xffd4ed8f : 0xff4b4f52).brighter(hover ? .12f : 0.0f);
        const auto bottom = juce::Colour(on ? 0xff8aa549 : 0xff242629).darker(down ? .23f : 0.0f);
        g.setGradientFill(juce::ColourGradient(top, 0, r.getY(), bottom, 0, r.getBottom(), false));
        g.fillRoundedRectangle(r, 2);
        g.setColour(b.hasKeyboardFocus(true) ? teal : chrome.withAlpha(.55f));
        g.drawRoundedRectangle(r, 2, 1);
        return;
    }
    xpButton(g, r, b.getToggleState(), hover, down, b.hasKeyboardFocus(true), b.isEnabled());
}

void NeedleLook::drawButtonText(juce::Graphics &g, juce::TextButton &b, bool hover, bool down)
{
    if (b.getProperties().contains(mixerButtonId))
    {
        legend(g, FontRole::hardwareButton, b.getButtonText(), b.getLocalBounds().reduced(4, 0),
               19, b.getToggleState() ? groove : silkscreen);
        return;
    }
    if (b.getProperties().contains(commandIconId))
    {
        const auto bounds = b.getLocalBounds().translated(0, down ? 1 : 0);
        const float y = static_cast<float>(bounds.getCentreY()) - 6;
        if (static_cast<bool>(b.getProperties()[commandIconId]))
        {
            g.setColour(juce::Colour(0xff957020));
            g.fillRoundedRectangle(11, y, 9, 7, 1);
            g.setColour(juce::Colour(0xfff3c55b));
            g.fillRoundedRectangle(11, y + 2, 16, 10, 1);
            g.setColour(juce::Colour(0xff8d671f));
            g.drawRoundedRectangle(11, y + 2, 16, 10, 1, 1);
            g.setColour(juce::Colour(0xffffecac));
            g.drawHorizontalLine(juce::roundToInt(y) + 3, 13, 25);
        }
        else
        {
            g.setColour(juce::Colour(0xff2f598d));
            g.drawRoundedRectangle(11, y, 16, 12, 2, 1);
            g.drawHorizontalLine(juce::roundToInt(y) + 4, 14, 24);
            g.drawHorizontalLine(juce::roundToInt(y) + 8, 14, 24);
            g.fillEllipse(16, y + 2, 4, 4);
            g.fillEllipse(21, y + 6, 4, 4);
        }
        g.setColour(xpInk.withAlpha(b.isEnabled() ? 1.0f : .45f));
        g.setFont(getTextButtonFont(b, bounds.getHeight()));
        g.drawFittedText(b.getButtonText(), bounds.withTrimmedLeft(30).withTrimmedRight(8),
                         juce::Justification::centred, 1);
        return;
    }
    if (!b.getProperties().contains(physicalButtonId))
    {
        LookAndFeel_V4::drawButtonText(g, b, hover, down);
        return;
    }
    const bool round = static_cast<bool>(b.getProperties()[physicalButtonId]);
    const auto colour = b.getToggleState() ? teal : silkscreen;
    const float size = round ? (b.getWidth() >= 40 ? 22.0f : 19.0f) : 17.0f;
    legend(g, FontRole::hardwareButton, b.getButtonText(), b.getLocalBounds().reduced(2, 0),
           size, colour.withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.4f));
}

void NeedleLook::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float value, float start,
                                  float end, juce::Slider &slider)
{
    const juce::Rectangle<float> bounds(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                        static_cast<float>(h));
    const auto centre = bounds.getCentre();
    const float diameter = std::min(bounds.getWidth(), bounds.getHeight()) - 4.0f;
    if (slider.isMouseOverOrDragging() || slider.hasKeyboardFocus(true))
    {
        g.setColour(teal.withAlpha(slider.isMouseButtonDown() ? 0.94f : 0.62f));
        g.drawEllipse(juce::Rectangle<float>(diameter + 2, diameter + 2).withCentre(centre), 1.2f);
    }
    g.setOpacity(slider.isEnabled() ? 1.0f : 0.45f);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    const float sweep = end - start;
    SkratchSkin::rotate(g, SkratchSkin::art().knob, centre, diameter, (value - 0.5f) * sweep);
    g.setOpacity(1.0f);
}

void NeedleLook::drawLinearSlider(juce::Graphics &g, int x, int y, int w, int h, float position, float, float,
                                  juce::Slider::SliderStyle, juce::Slider &slider)
{
    const auto &art = SkratchSkin::art();
    const auto kind = static_cast<int>(slider.getProperties().getWithDefault(hardwareId, -1));
    const bool vertical = slider.isVertical();
    const juce::Point<float> centre =
        vertical ? juce::Point<float>(x + w * 0.5f, position) : juce::Point<float>(position, y + h * 0.5f);
    if (kind == -1)
    {
        const auto bar =
            juce::Rectangle<float>(static_cast<float>(x), centre.y - 2.5f, static_cast<float>(w), 5.0f);
        g.setColour(groove);
        g.fillRect(bar);
        g.setColour(teal.withAlpha(0.67f));
        g.fillRect(bar.withRight(std::clamp(position, bar.getX(), bar.getRight())));
    }
    const auto &sprite =
        kind == mixerCross
            ? art.crossCap
            : (kind == deckHorizontal ? art.deckPitchCap : (kind == deckVertical ? art.deckCap : art.cap));
    const juce::Rectangle<float> slot = kind == deckHorizontal  ? juce::Rectangle<float>(25.2f, 22.4f)
                                        : kind == deckVertical  ? juce::Rectangle<float>(21.2f, 24.0f)
                                        : kind == mixerCross    ? juce::Rectangle<float>(30, 22)
                                        : kind == mixerVertical ? juce::Rectangle<float>(28, 17)
                                                                : juce::Rectangle<float>(17, 13);
    const auto bounds = slot.withCentre(centre);
    g.setOpacity(slider.isEnabled() ? 1.0f : 0.48f);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    SkratchSkin::image(g, sprite, bounds);
    g.setOpacity(1.0f);
    if (slider.isMouseOverOrDragging() || slider.hasKeyboardFocus(true))
    {
        g.setColour(teal.withAlpha(slider.isMouseButtonDown() ? 0.9f : 0.6f));
        g.drawRect(bounds.expanded(1.0f), 1.0f);
    }
}
CustomRotaryKnob::CustomRotaryKnob()
{
    setSliderStyle(RotaryVerticalDrag);
    setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setMouseDragSensitivity(155);
}
void CustomRotaryKnob::mouseDown(const juce::MouseEvent &event)
{
    pointer = adjustedPointer = event.position;
    Slider::mouseDown(event);
}
void CustomRotaryKnob::mouseDrag(const juce::MouseEvent &event)
{
    adjustedPointer += (event.position - pointer) * (event.mods.isShiftDown() ? 0.18f : 1.0f);
    pointer = event.position;
    Slider::mouseDrag(event.withNewPosition(adjustedPointer));
}

Platter::Platter(NeedleProcessor &p) : processor(p)
{
    setComponentID("vinyl");
    setTitle("Vinyl platter");
    setDescription("Drag circularly. Keyboard alternative: A backwards, D forwards.");
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}
float Platter::angleAt(juce::Point<float> p) const
{
    return std::atan2(p.y - axis.y, p.x - axis.x);
}
void Platter::mouseDown(const juce::MouseEvent &e)
{
    grabKeyboardFocus();
    previousAngle = angleAt(e.position);
    lastDrag = juce::Time::getMillisecondCounterHiRes();
    processor.mouseSpeed.store(0);
    processor.mouseHeld.store(true);
    previewTurn = processor.platterTurns.load();
    previewSerial = processor.mouseSerial.fetch_add(1) + 1;
    repaint();
}
void Platter::mouseDrag(const juce::MouseEvent &e)
{
    const bool measure = processor.latencyMetrics.enabled.load(std::memory_order_relaxed);
    const auto start = measure ? juce::Time::getHighResolutionTicks() : 0;
    const double now = juce::Time::getMillisecondCounterHiRes(),
                 dt = juce::jlimit(0.002, 0.1, (now - lastDrag) / 1000);
    const float angle = angleAt(e.position);
    const auto offset = e.position - axis;
    const float delta = offset.getDistanceSquaredFromOrigin() < 225.0f
                            ? 0.0f
                            : static_cast<float>(std::remainder(angle - previousAngle, 2 * skratch::pi));
    const double secondsPerTurn = processor.param("rpm") > 0.5f ? 60.0 / 45 : 1.8;
    processor.mouseSpeed.store(
        juce::jlimit(-6.0f, 6.0f, static_cast<float>(delta / (2 * skratch::pi) * secondsPerTurn / dt)));
    previousAngle = angle;
    lastDrag = now;
    previewTurn = processor.platterTurns.load() + juce::jlimit(-0.02, 0.02, delta / (2 * skratch::pi));
    if (measure)
    {
        processor.latencyMetrics.mouseTick.store(start, std::memory_order_relaxed);
        pendingPaintTick = start;
    }
    previewSerial = processor.mouseSerial.fetch_add(1, std::memory_order_release) + 1;
    repaint();
    if (measure)
        processor.latencyMetrics.mouseHandler.add(juce::Time::getHighResolutionTicks() - start);
}
void Platter::stop()
{
    processor.mouseHeld.store(false);
    processor.mouseSpeed.store(0);
    previewSerial = 0;
}
void Platter::mouseUp(const juce::MouseEvent &)
{
    stop();
    repaint();
}
void Platter::refreshFrame()
{
    if (std::abs(std::remainder(processor.platterTurns.load() - drawnTurn, 1.0)) > 0.00005)
        repaint();
}
bool Platter::keyPressed(const juce::KeyPress &k)
{
    if (k.getKeyCode() == juce::KeyPress::leftKey)
    {
        processor.seek.store(std::max(0.0, processor.progress.load() - 0.01));
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::rightKey)
    {
        processor.seek.store(std::min(0.999, processor.progress.load() + 0.01));
        return true;
    }
    return false;
}

bool Platter::hitTest(int x, int y)
{
    const auto offset = juce::Point<float>(static_cast<float>(x), static_cast<float>(y)) - axis;
    const float radius = SkratchSkin::platterDiameter * 0.5f;
    return offset.x * offset.x + offset.y * offset.y <= radius * radius;
}
void Platter::paint(juce::Graphics &g)
{
    const bool measure = processor.latencyMetrics.enabled.load(std::memory_order_relaxed);
    const auto start = measure ? juce::Time::getHighResolutionTicks() : 0;
    const auto acknowledged = processor.appliedMouseSerial.load(std::memory_order_acquire);
    drawnTurn =
        previewSerial != 0 && acknowledged != previewSerial ? previewTurn : processor.platterTurns.load();
    const float rotation = static_cast<float>(drawnTurn * 2 * skratch::pi);
    SkratchSkin::drawPlatter(g, axis, rotation);
    if (hasKeyboardFocus(true))
    {
        g.setColour(teal.withAlpha(0.75f));
        g.drawEllipse(juce::Rectangle<float>(SkratchSkin::platterDiameter, SkratchSkin::platterDiameter)
                          .withCentre(axis),
                      1.0f);
    }
    if (measure)
    {
        const auto end = juce::Time::getHighResolutionTicks();
        processor.latencyMetrics.platterPaint.add(end - start);
        if (pendingPaintTick > 0)
        {
            processor.latencyMetrics.mouseToPaint.add(end - pendingPaintTick);
            pendingPaintTick = 0;
        }
    }
}

void Waveform::refresh()
{
    const auto pixel = [this](double position)
    { return std::clamp(juce::roundToInt(position * getWidth()), 0, getWidth() - 1); };
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const auto cursor = pixel(processor.progress.load()), cuePixel = pixel(processor.cue.load());
    if (lastCursor != cursor)
    {
        const int left = std::max(0, std::min(lastCursor, cursor) - 1);
        repaint(left, 0, std::max(lastCursor, cursor) - left + 2, getHeight());
        lastCursor = cursor;
    }
    if (lastCue != cuePixel)
    {
        repaint(lastCue, 0, 1, getHeight());
        repaint(cuePixel, 0, 1, getHeight());
        lastCue = cuePixel;
    }
    if (cachedRevision != displayView.revision)
        repaint();
}
void Waveform::paint(juce::Graphics &g)
{
    if (cachedRevision != displayView.revision || quiet.getWidth() != getWidth() * 2 ||
        quiet.getHeight() != getHeight() * 2)
    {
        cachedRevision = displayView.revision;
        const auto draw = [&](juce::Colour colour)
        {
            juce::Image layer(juce::Image::RGB, getWidth() * 2, getHeight() * 2, false);
            juce::Graphics canvas(layer);
            canvas.fillAll(groove);
            canvas.addTransform(juce::AffineTransform::scale(2.0f));
            const auto &peaks = displayView.peaks;
            const float cy = getHeight() * 0.5f;
            canvas.setColour(colour);
            for (int x = 0; x < getWidth(); x += 2)
            {
                const size_t i = static_cast<size_t>(x) * peaks.size() / getWidth();
                const float h = i < peaks.size() ? peaks[i] * (getHeight() - 3) * 0.48f : 0;
                canvas.fillRect(static_cast<float>(x), cy - std::max(1.0f, h), 1.0f, std::max(2.0f, 2 * h));
            }
            canvas.setColour(chrome.withAlpha(0.12f));
            for (int x = 0; x < getWidth(); x += 48)
                canvas.drawVerticalLine(x, 0, static_cast<float>(getHeight()));
            return layer;
        };
        quiet = draw(teal.withAlpha(0.45f));
        played = draw(teal);
    }
    const int cursor =
        std::clamp(juce::roundToInt(processor.progress.load() * getWidth()), 0, getWidth() - 1);
    g.drawImage(quiet, getLocalBounds().toFloat());
    {
        juce::Graphics::ScopedSaveState state(g);
        if (g.reduceClipRegion(0, 0, cursor, getHeight()))
            g.drawImage(played, getLocalBounds().toFloat());
    }
    g.setColour(accent);
    g.drawVerticalLine(static_cast<int>(processor.cue.load() * getWidth()), 0,
                       static_cast<float>(getHeight()));
    g.setColour(ink);
    g.drawVerticalLine(cursor, 0, static_cast<float>(getHeight()));
    if (hasKeyboardFocus(true))
    {
        g.setColour(teal);
        g.drawRect(getLocalBounds());
    }
}
bool Waveform::keyPressed(const juce::KeyPress &k)
{
    if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey)
    {
        processor.seek.store(juce::jlimit(0.0, 0.999,
                                          processor.progress.load() +
                                              (k.getKeyCode() == juce::KeyPress::leftKey ? -0.01 : 0.01)));
        return true;
    }
    return false;
}
void FaderCurve::paint(juce::Graphics &g)
{
    g.fillAll(groove);
    g.setColour(panel);
    for (int n = 1; n < 4; ++n)
    {
        g.drawVerticalLine(n * getWidth() / 4, 0, static_cast<float>(getHeight()));
        g.drawHorizontalLine(n * getHeight() / 4, 0, static_cast<float>(getWidth()));
    }
    juce::Path p;
    for (int n = 0; n <= 100; ++n)
    {
        float x = n / 100.0f;
        float y = processor.param("cutMode") > 0.5f
                      ? (x > processor.param("cutIn") ? 1.0f : 0.0f)
                      : std::sin(std::pow(x, processor.param("curve")) * 1.5707963f);
        const float py = (1 - y) * (getHeight() - 6) + 3;
        if (n == 0)
            p.startNewSubPath(0, py);
        else
            p.lineTo(x * getWidth(), py);
    }
    g.setColour(teal);
    g.strokePath(p, juce::PathStrokeType(1.5f));
    text(g, "CURVE / DRAG", {6, 4, getWidth() - 12, 14}, 9, muted);
    if (hasKeyboardFocus(true))
    {
        g.setColour(teal);
        g.drawRect(getLocalBounds());
    }
}
void FaderCurve::mouseDrag(const juce::MouseEvent &e)
{
    grabKeyboardFocus();
    processor.setParam("curve",
                       juce::jlimit(0.15f, 4.0f, 4.0f - 3.85f * e.position.y / std::max(1, getHeight())));
    processor.setParam("cutIn", juce::jlimit(0.001f, 0.4f, e.position.x / std::max(1, getWidth()) * 0.4f));
    repaint();
}
bool FaderCurve::keyPressed(const juce::KeyPress &k)
{
    if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey)
    {
        processor.setParam("curve",
                           juce::jlimit(0.15f, 4.0f,
                                        processor.param("curve") +
                                            (k.getKeyCode() == juce::KeyPress::upKey ? 0.1f : -0.1f)));
        repaint();
        return true;
    }
    return false;
}

void NeedleEditor::slider(juce::Slider &slider, const juce::String &id, const juce::String &title,
                          int hardware)
{
    slider.setName(title);
    slider.setTitle(title);
    slider.setComponentID(id);
    slider.getProperties().set(hardwareId, hardware);
    if (hardware != rotary)
        slider.setSliderStyle(hardware == deckHorizontal || hardware == mixerCross
                                  ? juce::Slider::LinearHorizontal
                                  : juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setWantsKeyboardFocus(true);
    if (auto *parameter = processor.state.getParameter(id))
        slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    slider.setTooltip(title + (hardware == rotary ? "  |  Shift: fine adjustment" : "") +
                      "  |  Double-click: reset");
    surface.addAndMakeVisible(slider);
    sliderAttachments.push_back(
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, id, slider));
    if (hardware == rotary || hardware == mixerVertical)
        slider.onValueChange = [this, hardware]
        {
            const auto area = hardware == rotary ? juce::Rectangle<int>(678, 207, 201, 20)
                                                 : juce::Rectangle<int>(675, 523, 219, 30);
            repaint(area.toFloat().transformedBy(designTransform).getSmallestIntegerContainer());
        };
}

NeedleEditor::NeedleEditor(NeedleProcessor &p)
    : AudioProcessorEditor(p), processor(p), displayView(p.view()), platter(p), waveform(p, displayView),
      curve(p)
{
    setLookAndFeel(&look);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(surface);
    for (auto *control : std::initializer_list<juce::Component *>{
             &platter, &waveform, &load,      &play,         &stop,        &cue,
             &setCue,  &cut,      &settings,  &midiShortcut, &padShortcut, &record,
             &loop,    &reverse,  &cutMode,   &deckReverse,  &rpm,         &tabs,
             &bank,    &fxPower,  &inputGate, &motorPower,   &morphPower})
        surface.addAndMakeVisible(control);
    for (auto *control : std::initializer_list<juce::Component *>{
             &curve, &mode, &sync, &quantize, &settingsLabel, &performance, &midiMode,
             &monitorToggle, &monitorParameter, &monitorReadout,
             p.isInstrument() ? static_cast<juce::Component *>(&embedSample) : &source})
        settingsPage.addAndMakeVisible(control);

    for (auto *control : {&play, &cue, &record})
        control->getProperties().set(physicalButtonId, true);
    for (auto *control : {&midiShortcut, &padShortcut})
        control->getProperties().set(physicalButtonId, false);
    for (auto *control : {&fxPower, &bank, &motorPower, &inputGate, &morphPower})
        control->getProperties().set(mixerButtonId, true);
    for (auto *control : {&play, &cue, &record})
        control->setColour(juce::TextButton::textColourOnId, ink);

    slider(pitch, "pitch", "Pitch", mixerVertical);
    slider(gain, "gain", "Master gain", mixerVertical);
    slider(sensitivity, "sensitivity", "Scratch sensitivity", mixerVertical);
    slider(response, "response", "Fader response", mixerVertical);
    slider(deckPitch, "pitch", "Deck pitch", deckHorizontal);
    slider(deckSensitivity, "sensitivity", "Deck scratch sensitivity", deckVertical);
    slider(cutIn, "cutIn", "Crossfader cut-in", rotary);
    slider(curveAmount, "curve", "Crossfader curve", rotary);
    slider(bpm, "bpm", "Internal BPM", rotary);
    slider(swing, "swing", "Pattern swing", rotary);
    slider(fader, "fader", "Crossfader", mixerCross);
    const auto bindKnob = [&](CustomRotaryKnob &knob, dj::Param parameter)
    {
        const auto &definition = dj::definitions[parameter];
        slider(knob, definition.id, definition.name, rotary);
        knob.textFromValueFunction = [parameter](double value)
        { return DjPanel::valueText(parameter, value); };
        knob.setPopupDisplayEnabled(true, true, &surface, 800);
        knob.onDragStart = [this, parameter] { processor.clearDjOverride(parameter); };
    };
    for (size_t i = 0; i < fxKnobs.size(); ++i)
    {
        bindKnob(fxKnobs[i], fxParams[i]);
        bindKnob(eqKnobs[i], eqParams[i]);
        bindKnob(inputKnobs[i], inputs[i].parameter);
        bindKnob(performanceKnobs[i], performanceSlots[i].parameter);
    }
    for (size_t i = 0; i < motorKnobs.size(); ++i)
        bindKnob(motorKnobs[i], motors[i].parameter);
    bindKnob(echoDivisionKnob, dj::echoDivision);
    updateMixerBank();
    bank.setComponentID("fx.bank");
    bank.setTooltip("Switch the top four controls between MAIN and FX. Right-clicking is not needed.");
    bank.onClick = [this]
    {
        processor.mixerFxBank.store(!fxBank, std::memory_order_relaxed);
        updateMixerBank();
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    fxPower.setComponentID("fx.power");
    fxPower.onClick = [this]
    { processor.setDjParam(dj::enabled, processor.djValue(dj::enabled) > .5f ? 0 : 1); };
    const auto bindPower = [this](juce::TextButton &button, dj::Param parameter, const char *id)
    {
        button.setComponentID(id);
        button.onClick = [this, parameter]
        { processor.setDjParam(parameter, processor.djValue(parameter) > .5f ? 0 : 1); };
    };
    bindPower(inputGate, dj::noiseOn, "mixer.gate");
    bindPower(motorPower, dj::motorOn, "mixer.motor");
    bindPower(morphPower, dj::morphOn, "mixer.morph");

    for (auto item :
         std::initializer_list<std::pair<juce::ToggleButton *, const char *>>{{&loop, "loop"},
                                                                              {&reverse, "reverse"},
                                                                              {&cutMode, "cutMode"},
                                                                              {&deckReverse, "deckReverse"},
                                                                              {&sync, "sync"},
                                                                              {&performance, "performance"},
                                                                              {&embedSample, "embedSample"}})
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            p.state, item.second, *item.first));
    mode.addItemList({"CLASSIC VINYL", "DIRECT SCRATCH", "TAPE SCRATCH", "DIGITAL SCRATCH"}, 1);
    mode.setTitle("Scratch engine mode");
    rpm.addItemList({"33 RPM", "45 RPM"}, 1);
    rpm.setTitle("Record RPM");
    quantize.addItemList({"Q: OFF", "Q: 1/16", "Q: 1/8", "Q: 1/4"}, 1);
    quantize.setTitle("Pattern trigger quantization");
    midiMode.addItemList({"SCRATCH PATTERNS", "ONE SHOT", "LIVE MIDI"}, 1);
    midiMode.setTitle("MIDI performance mode");
    source.addItemList({"FILE DECK", "LIVE INPUT"}, 1);
    source.setTitle("FX scratch source");
    performance.setTooltip("Faster speed and fader response with short anti-click ramps.");
    embedSample.setTooltip(
        "Include the active sample in project state when its original file is up to 16 MiB.");
    for (auto item : std::initializer_list<std::pair<juce::ComboBox *, const char *>>{{&mode, "mode"},
                                                                                      {&rpm, "rpm"},
                                                                                      {&quantize, "quantize"},
                                                                                      {&midiMode, "midiMode"},
                                                                                      {&source, "source"}})
        comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            p.state, item.second, *item.first));
    load.getProperties().set(commandIconId, true);
    load.onClick = [this] { chooseFile(); };
    play.onClick = [this] { processor.setParam("play", processor.param("play") > 0.5f ? 0 : 1); };
    stop.onClick = [this] { processor.stopDeck(); };
    cue.onClick = [this] { processor.returnToCue(); };
    setCue.onClick = [this] { processor.setCue(); };
    settings.getProperties().set(commandIconId, false);
    settings.onClick = [this] { tabs.setCurrentTabIndex(6); };
    midiShortcut.setComponentID("mixer.midi");
    padShortcut.setComponentID("mixer.pad");
    record.setComponentID("mixer.record");
    midiShortcut.onClick = padShortcut.onClick = [this] { tabs.setCurrentTabIndex(5); };
    record.onClick = [this]
    {
        if (processor.recording.load())
            processor.stopRecording();
        else
            processor.startRecording();
    };
    cut.onStateChange = [this] { processor.cutHeld.store(cut.isDown()); };
    const char *names[] = {"SAMPLE BROWSER", "MIDI SEQUENCER", "SCRATCH PATTERNS", "RECORDING", "PRESETS"};
    for (int i = 0; i < 5; ++i)
    {
        pages.push_back(std::make_unique<ToolPage>(p, i));
        tabs.addTab(names[i], xpPanel, pages.back().get(), false);
    }
    mapping = std::make_unique<MappingPanel>(p);
    mappingView.setViewedComponent(mapping.get(), false);
    mappingView.setScrollBarsShown(true, false);
    tabs.addTab("INPUT MAPPING", xpPanel, &mappingView, false);
    tabs.addTab("SETTINGS", xpPanel, &settingsPage, false);
    djPanel = std::make_unique<DjPanel>(p);
    djView.setViewedComponent(djPanel.get(), false);
    djView.setScrollBarsShown(true, false);
    tabs.addTab("DJ FX", xpPanel, &djView, false);
    settingsLabel.setText("SCRATCH ENGINE / CROSSFADE CURVE", juce::dontSendNotification);
    int monitorId = 1;
    for (auto *parameter : processor.getParameters())
        monitorParameter.addItem(parameter->getName(64), monitorId++);
    monitorParameter.setSelectedId(1, juce::dontSendNotification);
    monitorReadout.setFont(SkratchSkin::font(FontRole::label, 14));
    monitorToggle.onClick = [this]
    {
        const bool on = monitorToggle.getToggleState();
        processor.monitor.enabled.store(on, std::memory_order_relaxed);
        monitorParameter.setVisible(on);
        monitorReadout.setVisible(on);
        curve.setVisible(!on);
    };
    monitorParameter.setVisible(false);
    monitorReadout.setVisible(false);
    settingsLabel.setFont(SkratchSkin::font(FontRole::label, 17));
    tabs.setTabBarDepth(30);
    addKeyListener(this);
    for (auto *child : surface.getChildren())
        child->addKeyListener(this);
    load.setTooltip("Load WAV / MP3 / FLAC / AIFF / OGG. Audio files can also be dropped into this window.");
    cut.setTooltip("Momentary deck mute. Hold J, or hold this button.");
    curve.setDescription("Drag X: cut-in distance. Drag Y: curve. Arrow keys adjust the exponent.");
    setResizable(true, true);
    setResizeLimits(SkratchSkin::width * 3 / 4, SkratchSkin::height * 3 / 4,
                    SkratchSkin::width * 2, SkratchSkin::height * 2);
    if (auto *constraints = getConstrainer())
        constraints->setFixedAspectRatio(static_cast<double>(SkratchSkin::width) / SkratchSkin::height);
    setSize(SkratchSkin::width, SkratchSkin::height);
    startTimerHz(30);
}

NeedleEditor::~NeedleEditor()
{
    processor.monitor.enabled.store(false, std::memory_order_relaxed);
    stopTimer();
    platter.stop();
    processor.keySpeed.store(0);
    processor.cutHeld.store(false);
    tabs.clearTabs();
    mappingView.setViewedComponent(nullptr, false);
    djView.setViewedComponent(nullptr, false);
    setLookAndFeel(nullptr);
}

void NeedleEditor::paint(juce::Graphics &g)
{
    using namespace SkratchSkin;
    g.fillAll(face);
    g.addTransform(designTransform);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(chassis(), juce::Rectangle<float>(static_cast<float>(width), static_cast<float>(height)));
    const auto &view = displayView;
    const auto currentTime = processor.progress.load() * view.seconds;
    SkratchSkin::text(g, FontRole::data, view.name, {27, 77, 560, 16}, 12, ink);
    lcd(g, juce::String(currentTime, 2), {829, 77, 82, 16}, 13);
    lcd(g, juce::String(view.seconds, 2), {930, 77, 82, 16}, 13);
    SkratchSkin::text(g, FontRole::data, "/", {916, 77, 12, 16}, 11, muted);

    auto status = [&g](int x, const juce::String &label, const juce::String &value, bool active)
    {
        const auto led = juce::Rectangle<float>(static_cast<float>(x + 7), 23, 8, 8);
        juce::ColourGradient light(active ? juce::Colour(0xffd1ff92) : juce::Colour(0xffd6e2f4),
                                   led.getX(), led.getY(),
                                   active ? juce::Colour(0xff357e26) : juce::Colour(0xff657e9c),
                                   led.getRight(), led.getBottom(), false);
        g.setGradientFill(light);
        g.fillEllipse(led);
        g.setColour(active ? juce::Colour(0xff35752c) : juce::Colour(0xff627997));
        g.drawEllipse(led, .6f);
        text(g, label, {x + 19, 19, 82, 16}, 11, xpInk);
        text(g, value, {x + 8, 37, 89, 14}, 11, xpInk);
    };
    const bool audio = processor.engineActive.load();
    const int note = processor.lastMidiNote.load(), cc = processor.lastMidiCC.load();
    const bool pad = processor.padConnected.load();
    status(405, "AUDIO ENGINE", audio ? "READY" : "IDLE", audio);
    status(519, "MIDI",
           note >= 0 ? "NOTE " + juce::String(note) : (cc >= 0 ? "CC " + juce::String(cc) : "WAITING"),
           note >= 0 || cc >= 0);
    status(633, "GAMEPAD", pad ? "CONNECTED" : "OFFLINE", pad);

    g.setColour(groove);
    g.fillRect(756, 33, 126, 12);
    const int cells = juce::roundToInt(20 * std::clamp(displayMeter, 0.0f, 1.0f));
    for (int i = 0; i < 20; ++i)
    {
        g.setColour(i >= cells ? juce::Colour(0xff2b3746)
                              : (i == 19 && displayMeter > .98f ? accent : (i >= 15 ? juce::Colour(0xffffc24f) : teal)));
        g.fillRect(759 + i * 6, 35, 4, 8);
    }
    SkratchSkin::text(g, FontRole::label, "OUTPUT / PEAK", {756, 13, 126, 16}, 14, xpInk);
    SkratchSkin::text(g, FontRole::data, processor.isInstrument() ? SKRATCH_PUBLIC_VERSION " / INSTRUMENT"
                                                                : SKRATCH_PUBLIC_VERSION " / FX",
                      {756, 47, 126, 14}, 11, xpInk);

    const std::array<double, 4> knobValues{bpm.getValue(), cutIn.getValue(), curveAmount.getValue(),
                                           swing.getValue()};
    const std::array<double, 4> channelValues{gain.getValue(), pitch.getValue(), sensitivity.getValue(),
                                              response.getValue()};
    hardwareLegends(g);
    const auto printLabels = [&g](const auto &items, int offset, int width)
    {
        for (const auto &item : items)
        {
            // The upper motor caption sits below the baked panel seam.
            const auto labelOffset = offset + (item.parameter == dj::torque ? 3 : 0);
            const auto centre = mixerPoint(item.x, item.y).translated(0, static_cast<float>(labelOffset));
            const auto bounds = around(centre, width, 15);
            if (item.parameter == dj::duty)
            {
                legend(g, FontRole::label, "STUTTER", bounds, 16);
                legend(g, FontRole::label, "DUTY", bounds.translated(0, 12), 16);
            }
            else
                legend(g, FontRole::label, item.label, bounds, 16);
        }
    };
    printLabels(inputs, -15, 64);
    printLabels(motors, -18, 61);
    printLabels(performanceSlots, 21, 49);
    for (size_t i = 0; i < channelX.size(); ++i)
    {
        const auto axis = mixerPoint(channelX[i], topKnob);
        legend(g, FontRole::label, fxBank ? fxNames[i] : knobNames[i],
               around(axis.translated(0, -22), 48, 16), 16);
        legend(g, FontRole::label, eqNames[i], around(mixerPoint(channelX[i], eqLegend), 48, 17), 18);
        const auto knob = around(mixerPoint(channelX[i], topReadout), 46, 17);
        const auto slider = around(mixerPoint(channelX[i], channelReadout), 46, 17);
        if (fxBank && i == 0 && echoDivisionKnob.isVisible())
            SkratchSkin::text(g, FontRole::data,
                              DjPanel::valueText(dj::echoDivision, echoDivisionKnob.getValue()), knob, 14,
                              teal, juce::Justification::centred);
        else
            lcd(g,
                juce::String(fxBank ? fxKnobs[i].getValue() : knobValues[i],
                             (fxBank && (i == 0 || i == 3)) || (!fxBank && i == 0) ? 0 : 2),
                knob, 14);
        lcd(g, juce::String(channelValues[i], i < 2 ? 1 : 2), slider, 15);
    }
    const auto mixerDisplay = around(mixerPoint(758, 378), 61, 62);
    legend(g, FontRole::label, "BPM", mixerDisplay.withHeight(12), 13);
    lcd(g, juce::String(processor.bpmDisplay.load(), 1), mixerDisplay.translated(0, 11).withHeight(18), 16);
    legend(g, FontRole::label, "SPEED", mixerDisplay.translated(0, 30).withHeight(12), 13);
    lcd(g, juce::String(processor.actualSpeed.load(), 2), mixerDisplay.translated(0, 42).withHeight(18), 15);
    const bool hamster = processor.param("reverse") > 0.5f;
    legend(g, FontRole::label, hamster ? "DECK" : "INPUT", crossLegend(true), 14,
           silkscreen, juce::Justification::centredRight);
    legend(g, FontRole::label, hamster ? "INPUT" : "DECK", crossLegend(false), 14,
           silkscreen, juce::Justification::centredLeft);

    const juce::String state =
        processor.recording.load()
            ? "RECORDING / 200 Hz"
            : (processor.takePlaying.load() ? "TAKE / BEAT " + juce::String(processor.takePosition.load(), 2)
                                            : view.status);
    SkratchSkin::text(g, FontRole::data, state, {21, 833, 520, 17}, 12,
                      processor.recording.load() ? juce::Colour(0xff9c2820) : xpInk);
    SkratchSkin::text(g, FontRole::label, "A / D  VINYL     J  CUT     X  FADER     C  CUE",
                      {591, 833, 448, 17}, 14, xpInk, juce::Justification::centredRight);
}

void NeedleEditor::resized()
{
    using namespace SkratchSkin;
    const float scale =
        std::min(getWidth() / static_cast<float>(width), getHeight() / static_cast<float>(height));
    designTransform = juce::AffineTransform::scale(scale).translated((getWidth() - width * scale) * 0.5f,
                                                                     (getHeight() - height * scale) * 0.5f);
    surface.setBounds(0, 0, width, height);
    surface.setTransform(designTransform);
    const auto axis = deckPoint(deckAxis.x, deckAxis.y);
    const auto discBounds = juce::Rectangle<float>(platterDiameter, platterDiameter)
                                .withCentre(axis)
                                .getSmallestIntegerContainer();
    platter.setBounds(discBounds);
    platter.setAxis(axis - discBounds.getPosition().toFloat());
    waveform.setBounds(27, 95, width - 54, 17);
    settings.setBounds(910, 7, 128, 25);
    load.setBounds(910, 36, 128, 25);

    play.setBounds(around(deckPoint(168, 1103), 51, 51));
    cue.setBounds(around(deckPoint(975, 1132), 46, 46));
    deckPitch.setBounds(sliderRail(deckPoint(710, 126), deckPoint(1040, 126), false, 26, 12));
    deckSensitivity.setBounds(sliderRail(deckPoint(1124, 865), deckPoint(1124, 1078), true, 25, 12));
    const std::array<juce::Slider *, 4> knobs{&bpm, &cutIn, &curveAmount, &swing};
    const std::array<juce::Slider *, 4> sliders{&gain, &pitch, &sensitivity, &response};
    for (size_t i = 0; i < channelX.size(); ++i)
    {
        knobs[i]->setBounds(around(mixerPoint(channelX[i], topKnob), 32, 32));
        fxKnobs[i].setBounds(knobs[i]->getBounds());
        eqKnobs[i].setBounds(around(mixerPoint(channelX[i], eqKnob), 30, 30));
        inputKnobs[i].setBounds(around(mixerPoint(inputs[i].x, inputs[i].y), 26, 26));
        performanceKnobs[i].setBounds(
            around(mixerPoint(performanceSlots[i].x, performanceSlots[i].y), 30, 30));
        sliders[i]->setBounds(
            sliderRail(mixerPoint(channelX[i], railTop), mixerPoint(channelX[i], railBottom), true, 38, 12));
    }
    fader.setBounds(sliderRail(mixerPoint(crossStart, crossRow), mixerPoint(crossEnd, crossRow), false, 24, 12));
    echoDivisionKnob.setBounds(fxKnobs.front().getBounds());
    for (size_t i = 0; i < motorKnobs.size(); ++i)
        motorKnobs[i].setBounds(around(mixerPoint(motors[i].x, motors[i].y), 26, 26));
    constexpr float shortcutRow = 185;
    constexpr int shortcutWidth = 23, shortcutHeight = 27;
    midiShortcut.setBounds(around(mixerPoint(83, shortcutRow), shortcutWidth, shortcutHeight));
    padShortcut.setBounds(around(mixerPoint(151, shortcutRow), shortcutWidth, shortcutHeight));
    record.setBounds(around(mixerPoint(803, 252), 26, 26));

    constexpr int leftButtonWidth = 62, rightButtonWidth = 65;
    inputGate.setBounds(sideButton(inputX, bankRow, leftButtonWidth));
    morphPower.setBounds(sideButton(inputX, bankRow + buttonPitch, leftButtonWidth));
    motorPower.setBounds(sideButton(lowerMotorX, 367, rightButtonWidth));
    fxPower.setBounds(sideButton(lowerMotorX, bankRow, rightButtonWidth));
    bank.setBounds(sideButton(lowerMotorX, bankRow + buttonPitch, rightButtonWidth));

    stop.setBounds(32, 622, 75, 25);
    setCue.setBounds(114, 622, 88, 25);
    rpm.setBounds(210, 622, 91, 25);
    deckReverse.setBounds(314, 622, 99, 25);
    loop.setBounds(423, 622, 78, 25);
    reverse.setBounds(594, 622, 100, 25);
    cutMode.setBounds(702, 622, 105, 25);
    cut.setBounds(827, 622, 198, 25);

    tabs.setBounds(20, 662, width - 40, 166);
    settingsLabel.setBounds(14, 6, 400, 23);
    mode.setBounds(16, 35, 196, 31);
    quantize.setBounds(224, 35, 127, 31);
    sync.setBounds(365, 35, 140, 31);
    midiMode.setBounds(16, 72, 164, 28);
    source.setBounds(192, 72, 128, 28);
    embedSample.setBounds(192, 72, 135, 28);
    performance.setBounds(338, 72, 160, 28);
    curve.setBounds(523, 12, 442, 105);
    monitorToggle.setBounds(16, 106, 310, 24);
    monitorParameter.setBounds(523, 12, 420, 27);
    monitorReadout.setBounds(523, 44, 442, 78);
    if (mapping)
        mapping->setSize(width - 62, 1016);
    if (djPanel)
        djPanel->setSize(width - 62, 80 + static_cast<int>((dj::count + 3) / 4) * 48 + 8);
}

void NeedleEditor::chooseFile()
{
    platter.stop();
    chooser = std::make_unique<juce::FileChooser>("Load audio files", juce::File(),
                                                  "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg");
    juce::Component::SafePointer<NeedleEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::canSelectMultipleItems,
                         [safe](const juce::FileChooser &fc)
                         {
                             if (safe)
                             {
                                 juce::StringArray files;
                                 for (const auto &f : fc.getResults())
                                     files.add(f.getFullPathName());
                                 safe->processor.addSamples(files);
                                 safe->grabKeyboardFocus();
                             }
                         });
}
void NeedleEditor::filesDropped(const juce::StringArray &files, int, int)
{
    processor.addSamples(files);
    grabKeyboardFocus();
}
bool NeedleEditor::keyPressed(const juce::KeyPress &k, juce::Component *source)
{
    if (dynamic_cast<juce::TextEditor *>(source))
        return false;
    const int c = k.getKeyCode();
    if (c == 'A' || c == 'D' || c == 'J')
    {
        updateKeys();
        return true;
    }
    if (c == ' ' && dynamic_cast<juce::Button *>(source))
        return false;
    if (c != ' ' && c != 'C' && c != 'V' && c != 'X' && c != 'H' && c != 'R' &&
        c != juce::KeyPress::escapeKey)
        return false;
    if (!latched.insert(c).second)
        return true;
    if (c == ' ')
        processor.setParam("play", processor.param("play") > 0.5f ? 0 : 1);
    if (c == 'C')
        processor.returnToCue();
    if (c == 'V')
        processor.setCue();
    if (c == 'X')
        processor.setParam("fader", processor.param("fader") > 0.5f ? 0 : 1);
    if (c == 'H')
        processor.setParam("reverse", processor.param("reverse") > 0.5f ? 0 : 1);
    if (c == 'R')
    {
        if (processor.recording.load())
            processor.stopRecording();
        else
            processor.startRecording();
    }
    if (c == juce::KeyPress::escapeKey)
    {
        processor.stopDeck();
        processor.playRecording(false);
    }
    return true;
}
void NeedleEditor::updateKeys()
{
    if (!hasKeyboardFocus(true))
    {
        processor.keySpeed.store(0);
        processor.cutHeld.store(cut.isDown());
        latched.clear();
        return;
    }
    const bool a = juce::KeyPress::isKeyCurrentlyDown('A'), d = juce::KeyPress::isKeyCurrentlyDown('D');
    processor.keySpeed.store((d ? 1.7f : 0) - (a ? 1.7f : 0));
    processor.cutHeld.store(cut.isDown() || juce::KeyPress::isKeyCurrentlyDown('J'));
    for (auto i = latched.begin(); i != latched.end();)
        if (!juce::KeyPress::isKeyCurrentlyDown(*i))
            i = latched.erase(i);
        else
            ++i;
}
bool NeedleEditor::keyStateChanged(bool, juce::Component *)
{
    updateKeys();
    return false;
}
void NeedleEditor::timerCallback()
{
    if (!isShowing())
    {
        if (getTimerInterval() < 120)
            startTimer(120);
        processor.keySpeed.store(0);
        platter.stop();
        return;
    }
    updateKeys();
    if (!hasKeyboardFocus(true) && !juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        platter.stop();
    const bool moving = processor.mouseHeld.load() || std::abs(processor.actualSpeed.load()) > 0.00001f;
    const int frameInterval = moving ? 16 : 32;
    if (getTimerInterval() != frameInterval)
        startTimer(frameInterval);
    play.setButtonText(processor.param("play") > 0.5f ? "PAUSE" : "PLAY");
    play.setToggleState(processor.param("play") > 0.5f, juce::dontSendNotification);
    record.setToggleState(processor.recording.load(), juce::dontSendNotification);
    displayMeter = std::max(processor.meter.load(), displayMeter * 0.86f);
    if (!fader.isMouseButtonDown())
    {
        const auto value = processor.effectiveFader.load();
        if (std::abs(fader.getValue() - value) > 0.0001)
            fader.setValue(value, juce::dontSendNotification);
    }
    const std::array<juce::Slider *, 8> midiSliders{&pitch, &gain,        &sensitivity, &response,
                                                    &cutIn, &curveAmount, &deckPitch,   &deckSensitivity};
    constexpr std::array<size_t, 8> midiIndices{0, 1, 2, 3, 4, 5, 0, 2};
    for (size_t i = 0; i < midiSliders.size(); ++i)
    {
        auto &slider = *midiSliders[i];
        const auto index = midiIndices[i];
        const auto published = processor.effectiveLive[index].load(std::memory_order_relaxed);
        const auto value =
            std::isfinite(published) ? published : processor.param(NeedleProcessor::liveIds[index]);
        if (!slider.isMouseButtonDown() && std::abs(slider.getValue() - value) > 0.0001)
            slider.setValue(value, juce::dontSendNotification);
    }
    platter.refreshFrame();
    waveform.refresh();
    refreshMixerControls();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastPanelRefresh >= (processor.param("performance") > 0.5f ? 150 : 100))
    {
        lastPanelRefresh = now;
        if (monitorReadout.isShowing())
        {
            const int index = monitorParameter.getSelectedId() - 1;
            if (auto *p = dynamic_cast<juce::AudioProcessorParameterWithID *>(processor.getParameters()[index]))
            {
                float value = processor.param(p->paramID);
                for (size_t i = 0; i < processor.effectiveLive.size(); ++i)
                    if (p->paramID == NeedleProcessor::liveIds[i])
                    {
                        const auto live = processor.effectiveLive[i].load(std::memory_order_relaxed);
                        if (std::isfinite(live))
                            value = live;
                    }
                const auto &m = processor.monitor;
                juce::String status;
                for (size_t i = 0; i < dj::count; ++i)
                    if (p->paramID == dj::definitions[i].id)
                    {
                        value = processor.djValue(static_cast<dj::Param>(i));
                        int module = -1;
                        if (i >= dj::low && i <= dj::highKill) module = 0;
                        if (i >= dj::filter && i <= dj::filterMix) module = 1;
                        if (i >= dj::echoOn && i <= dj::echoOut) module = 2;
                        if (i >= dj::crushOn && i <= dj::crushGain) module = 3;
                        if (i >= dj::driveOn && i <= dj::driveGain) module = 4;
                        if (i >= dj::gateOn && i <= dj::gateFire) module = 5;
                        if (i == dj::limiter) module = 6;
                        if (module >= 0)
                            status = (m.activeModules.load() & (1u << module)) != 0 ? " / active" : " / bypassed";
                    }
                monitorReadout.setText(p->paramID + "  " + juce::String(value, 3) + status +
                    "\nInput RMS " + juce::String(m.inputRms.load(), 5) +
                    "  Output RMS " + juce::String(m.outputRms.load(), 5) +
                    "\nRack " + (m.rack.load() ? "active" : "bypassed") +
                    "  Strip " + (m.strip.load() ? "active" : "bypassed"), juce::dontSendNotification);
            }
        }
        auto view = processor.view(displayView.revision);
        if (view.revision == displayView.revision)
            view.peaks.swap(displayView.peaks);
        displayView = std::move(view);
        for (auto &page : pages)
            if (page->isShowing())
                page->refresh();
        for (const auto area : {juce::Rectangle<int>(19, 4, 869, 58),
                                {20, 73, 1020, 21},
                                {678, 207, 201, 20},
                                {675, 523, 219, 56},
                                {887, 250, 69, 63},
                                {20, 831, 1020, 25}})
            repaint(area.toFloat().transformedBy(designTransform).getSmallestIntegerContainer());
    }
}

void NeedleEditor::updateMixerBank()
{
    const bool stored = processor.mixerFxBank.load(std::memory_order_relaxed);
    const bool bankChanged = stored != fxBank;
    if (bankChanged)
    {
        fxBank = stored;
        bank.setButtonText(fxBank ? "BANK / FX" : "BANK / MAIN");
        repaint(juce::Rectangle<float>(677, 163, 210, 67)
                    .transformedBy(designTransform)
                    .getSmallestIntegerContainer());
    }
    const bool synced = fxBank && processor.djValue(dj::echoSync) > .5f;
    const bool echoVisibilityChanged = echoDivisionKnob.isVisible() != synced;
    echoDivisionKnob.setVisible(synced);
    for (auto *knob : {&bpm, &cutIn, &curveAmount, &swing})
        knob->setVisible(!fxBank);
    for (size_t i = 0; i < fxKnobs.size(); ++i)
        fxKnobs[i].setVisible(fxBank && (i != 0 || !synced));
    if (echoVisibilityChanged)
        repaint(juce::Rectangle<float>(675, 167, 61, 56)
                    .transformedBy(designTransform)
                    .getSmallestIntegerContainer());
}

void NeedleEditor::refreshMixerControls()
{
    updateMixerBank();
    const auto indicator =
        [this](juce::TextButton &button, dj::Param parameter, const char *on, const char *off)
    {
        const bool enabled = processor.djValue(parameter) > .5f;
        button.setButtonText(enabled ? on : off);
        button.setToggleState(enabled, juce::dontSendNotification);
    };
    indicator(fxPower, dj::enabled, "FX ON", "FX OFF");
    indicator(inputGate, dj::noiseOn, "GATE ON", "GATE OFF");
    indicator(motorPower, dj::motorOn, "MOTOR ON", "MOTOR OFF");
    indicator(morphPower, dj::morphOn, "MORPH ON", "MORPH OFF");
    const auto refresh = [this](CustomRotaryKnob &knob, dj::Param parameter)
    {
        const auto value = processor.djValue(parameter);
        if (knob.isVisible() && !knob.isMouseButtonDown() && std::abs(knob.getValue() - value) > .0001)
            knob.setValue(value, juce::dontSendNotification);
    };
    for (size_t i = 0; i < fxKnobs.size(); ++i)
    {
        refresh(fxKnobs[i], fxParams[i]);
        refresh(eqKnobs[i], eqParams[i]);
        refresh(inputKnobs[i], inputs[i].parameter);
        refresh(performanceKnobs[i], performanceSlots[i].parameter);
    }
    for (size_t i = 0; i < motorKnobs.size(); ++i)
        refresh(motorKnobs[i], motors[i].parameter);
    refresh(echoDivisionKnob, dj::echoDivision);
}
