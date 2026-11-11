#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool result, const char *message)
{
    if (!result)
        throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}

juce::Component *find(juce::Component &parent, const juce::String &id)
{
    if (parent.getComponentID() == id)
        return &parent;
    for (auto *child : parent.getChildren())
        if (auto *result = find(*child, id))
            return result;
    return nullptr;
}

juce::MouseEvent event(juce::Component &c, juce::Point<float> current, juce::Point<float> start,
                       int modifiers, bool dragged, int clicks = 1)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            current,
            juce::ModifierKeys(modifiers),
            1,
            0,
            0,
            0,
            0,
            &c,
            &c,
            now,
            start,
            now,
            clicks,
            dragged};
}

void checkAxis(const juce::Image &sprite)
{
    const juce::Point<float> expected{194.25f, 192.75f};
    for (int degrees = -360; degrees <= 360; degrees += 15)
    {
        juce::Image frame(juce::Image::ARGB, 388, 388, true);
        {
            juce::Graphics g(frame);
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            SkratchSkin::rotate(g, sprite, expected, 341,
                                juce::degreesToRadians(static_cast<float>(degrees)));
        }
        int left = 388, top = 388, right = -1, bottom = -1;
        const juce::Image::BitmapData pixels(frame, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
                if (pixels.getPixelColour(x, y).getAlpha() >= 128)
                {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
        if (std::abs((left + right + 1) * 0.5f - expected.x) > 1.25f ||
            std::abs((top + bottom + 1) * 0.5f - expected.y) > 1.25f ||
            std::abs((right - left) - (bottom - top)) > 2)
            throw std::runtime_error("rotating sprite bounds drifted");
    }
}

void save(const juce::Image &image, const juce::File &file)
{
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat png;
    if (!stream.openedOk() || !png.writeImageToStream(image, stream))
        throw std::runtime_error("could not write skin preview");
}
} // namespace

void runSkinTests(NeedleProcessor &processor, NeedleEditor &editor, const juce::File &directory)
{
    editor.setVisible(true);
    const auto &art = SkratchSkin::art();
    check(art.deck.isValid() && art.mixer.isValid() && art.logo.isValid() &&
              art.knob.isValid() && art.cap.isValid() && art.crossCap.isValid(),
          "user art decodes from resources");
    for (const auto centre : {juce::Point<int>{79, 65}, {1174, 65}, {79, 1183}, {1176, 1183}})
        check(art.deck.getPixelAt(centre.x, centre.y).getAlpha() >= 250,
              "former corner bolt positions are solid metal");
    for (const auto centre : {juce::Point<int>{970, 303}, {1000, 323}, {1030, 342}, {1082, 352}, {1060, 385}})
    {
        const auto steel = art.deck.getPixelAt(centre.x, centre.y);
        check(steel.getAlpha() == 255 && steel.getBrightness() > .25f,
              "headshell opening reveals underlying deck metal");
    }
    const std::array<const char *, 5> families{"Black Ops One", "Teko", "Rajdhani", "IBM Plex Mono", "DSEG"};
    for (size_t i = 0; i < families.size(); ++i)
        check(SkratchSkin::font(static_cast<SkratchSkin::FontRole>(i), 20)
                  .getTypefaceName()
                  .contains(families[i]),
              "embedded typeface family is active");
    checkAxis(art.metal);
    checkAxis(art.vinyl);
    check(true, "metal and vinyl remain centred through forward and reverse revolutions");
    for (int degrees = -360; degrees <= 360; degrees += 30)
    {
        juce::Image frame(juce::Image::ARGB, 388, 388, true);
        const juce::Point<float> axis{194.25f, 192.75f};
        {
            juce::Graphics g(frame);
            SkratchSkin::drawPlatter(g, axis, juce::degreesToRadians(static_cast<float>(degrees)));
        }
        for (int sample = 0; sample < 64; ++sample)
        {
            const auto a = sample * 2.0 * skratch::pi / 64;
            const int x = juce::roundToInt(axis.x + std::cos(a) * 150);
            const int y = juce::roundToInt(axis.y + std::sin(a) * 150);
            if (frame.getPixelAt(x, y).getAlpha() < 250)
                throw std::runtime_error("transparent gap at the metal/vinyl seam");
        }
    }
    check(art.deckPitchCap.getPixelAt(0, 0).getAlpha() == 0 && art.deckCap.getPixelAt(0, 0).getAlpha() == 0,
          "cap silhouettes are transparent at corners and joined platter seam stays opaque");

    auto *knob = dynamic_cast<CustomRotaryKnob *>(find(editor, "cutIn"));
    auto *fader = dynamic_cast<juce::Slider *>(find(editor, "fader"));
    auto *gain = dynamic_cast<juce::Slider *>(find(editor, "gain"));
    auto *platter = dynamic_cast<Platter *>(find(editor, "vinyl"));
    check(knob && fader && gain && platter, "native controls found in the editor");
    check(fader->isHorizontal() && gain->isVertical() &&
              gain->getPositionOfValue(gain->getMaximum()) < gain->getPositionOfValue(gain->getMinimum()),
          "physical rails are constrained to their horizontal or vertical axis");
    const auto faderMin = fader->getX() + fader->getPositionOfValue(fader->getMinimum());
    const auto faderMax = fader->getX() + fader->getPositionOfValue(fader->getMaximum());
    check(std::abs(faderMin - SkratchSkin::mixerPoint(349, 992).x) < 2 &&
              std::abs(faderMax - SkratchSkin::mixerPoint(519, 992).x) < 2,
          "crossfader travel matches the atlas");

    constexpr int button = juce::ModifierKeys::leftButtonModifier;
    const auto middle = platter->getLocalBounds().toFloat().getCentre();
    const auto right = middle.translated(50, 0), down = middle.translated(0, 50);
    platter->mouseDown(event(*platter, right, right, button, false));
    platter->mouseDrag(event(*platter, down, right, button, true));
    check(processor.mouseHeld.load() && processor.mouseSpeed.load() > 0,
          "clockwise drag reaches scratch engine");
    platter->mouseDrag(event(*platter, right, right, button, true));
    check(processor.mouseSpeed.load() < 0, "counterclockwise drag reaches scratch engine");
    platter->mouseUp(event(*platter, right, right, 0, true));
    check(!processor.mouseHeld.load() && processor.mouseSpeed.load() == 0, "mouse release clears scratch");
    check(platter->hitTest(170, 170) && !platter->hitTest(0, 0), "scratch hit area is circular");

    const auto knobStart = knob->getLocalBounds().toFloat().getCentre();
    const auto knobEnd = knobStart.translated(0, -30);
    auto drag = [&](int mods)
    {
        processor.setParam("cutIn", 0.08f);
        knob->mouseDown(event(*knob, knobStart, knobStart, mods, false));
        knob->mouseDrag(event(*knob, knobEnd, knobStart, mods, true));
        knob->mouseUp(event(*knob, knobEnd, knobStart, 0, true));
        return processor.param("cutIn") - 0.08f;
    };
    const auto normal = drag(button);
    const auto fine = drag(button | juce::ModifierKeys::shiftModifier);
    check(normal > fine && fine > 0, "Shift reduces drag sensitivity without losing APVTS updates");
    knob->mouseDoubleClick(event(*knob, knobStart, knobStart, button, false, 2));
    const auto *parameter = processor.state.getParameter("cutIn");
    check(std::abs(processor.param("cutIn") - parameter->convertFrom0to1(parameter->getDefaultValue())) <
              0.001f,
          "rotary double-click restores the parameter default");
    auto *input = dynamic_cast<CustomRotaryKnob *>(find(editor, "dj.inputGain"));
    check(input != nullptr, "new input control is a physical rotary");
    const auto centre = input->getLocalBounds().toFloat().getCentre();
    const auto dragTo = centre.translated(0, -21);
    input->mouseDown(event(*input, centre, centre, button | juce::ModifierKeys::shiftModifier, false));
    input->mouseDrag(event(*input, dragTo, centre, button | juce::ModifierKeys::shiftModifier, true));
    input->mouseUp(event(*input, dragTo, centre, 0, true));
    check(processor.djValue(dj::inputGain) > 0, "new rotary fine drag reaches its APVTS parameter");
    input->mouseDoubleClick(event(*input, centre, centre, button, false, 2));
    check(std::abs(processor.djValue(dj::inputGain)) < .0001f,
          "new rotary double-click resets the actual parameter");

    gain->setValue(-10, juce::sendNotificationSync);
    fader->setValue(0.25, juce::sendNotificationSync);
    check(std::abs(processor.param("gain") + 10) < 0.001f &&
              std::abs(processor.param("fader") - 0.25f) < 0.001f,
          "faders reach the original APVTS");
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    processor.setParam("gain", -25);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(std::abs(gain->getValue() + 10) < 0.001f, "attached control reflects restored plugin state");
    processor.setParam("gain", -6);
    processor.setParam("fader", 1);

    const auto *constrainer = editor.getConstrainer();
    check(constrainer && constrainer->getMinimumHeight() == SkratchSkin::height * 3 / 4 &&
              constrainer->getMaximumHeight() == SkratchSkin::height * 2 &&
              std::abs(constrainer->getFixedAspectRatio() -
                       static_cast<double>(SkratchSkin::width) / SkratchSkin::height) < .00001,
          "editor bounds constrainer matches the compact canvas");
    const auto mixerControl = [&](const char *id)
    {
        auto *control = find(editor, id);
        check(control != nullptr, id);
        return control;
    };
    const auto *gate = mixerControl("mixer.gate");
    const auto *morph = mixerControl("mixer.morph");
    const auto *motor = mixerControl("mixer.motor");
    const auto *power = mixerControl("fx.power");
    const auto *bank = mixerControl("fx.bank");
    const auto *midi = mixerControl("mixer.midi");
    const auto *pad = mixerControl("mixer.pad");
    const auto sameColumn = [](const auto *a, const auto *b)
    {
        return a->getX() == b->getX() && a->getWidth() == b->getWidth() && a->getHeight() == b->getHeight();
    };
    check(sameColumn(gate, morph) && sameColumn(motor, power) && sameColumn(power, bank) &&
              gate->getY() == power->getY() && morph->getY() == bank->getY(),
          "mixer side buttons share column widths and lower row baselines");
    check(midi->getY() == pad->getY() && midi->getWidth() == pad->getWidth() && midi->getHeight() == pad->getHeight(),
          "MIDI and PAD share a horizontal row and button dimensions");
    for (const auto *id : {"pitch", "sensitivity", "response"})
    {
        const auto *rail = mixerControl(id);
        check(rail->getY() == gain->getY() && rail->getHeight() == gain->getHeight(),
              "all four vertical fader rails have equal travel and baselines");
    }
    const auto *record = mixerControl("mixer.record");
    for (const int percent : {100, 125, 150, 200})
    {
        editor.setSize(SkratchSkin::width * percent / 100, SkratchSkin::height * percent / 100);
        const auto hit = editor.getLocalPoint(knob, knob->getLocalBounds().getCentre());
        check(editor.getComponentAt(hit) == knob, "scaled hit test matches the visible knob");
        for (const auto *control : {gate, morph, motor, power, bank, midi, pad, record})
        {
            const auto centre = editor.getLocalPoint(control, control->getLocalBounds().getCentre());
            check(editor.getComponentAt(centre) == control, "scaled hit test matches the mixer button");
        }
        save(editor.createComponentSnapshot(editor.getLocalBounds()),
             directory.getChildFile("scale-" + juce::String(percent) + ".png"));
    }
    editor.setSize(SkratchSkin::width, SkratchSkin::height);
    processor.setParam("pitch", -50);
    processor.setParam("sensitivity", 3);
    save(editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f),
         directory.getChildFile("rails-min.png"));
    processor.setParam("pitch", 50);
    processor.setParam("sensitivity", .25f);
    save(editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f),
         directory.getChildFile("rails-max.png"));
    processor.setParam("pitch", 0);
    processor.setParam("sensitivity", 1);
}
