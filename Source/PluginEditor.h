#pragma once
#include "PluginProcessor.h"
#include "SkinAssets.h"
#include "DjPanel.h"
#include <set>
namespace NeedleTheme
{
inline const juce::Colour face{0xff230c12}, panel{0xff362429}, ink{0xfff4eee7}, muted{0xffc4b4b0},
    accent{0xffe85b55}, teal{0xffc6e976}, groove{0xff130d10}, chrome{0xffbcb7b3};
inline const juce::Colour xpPanel{0xffece9d8}, xpWhite{0xfffaf9f3}, xpInk{0xff183653},
    xpBorder{0xff7298c9}, xpBlue{0xff245edb}, xpFocus{0xffd48b12};
juce::Font font(float, bool = false);
void drawXpTitle(juce::Graphics &, juce::Rectangle<float>, bool active = true);
void text(juce::Graphics &, const juce::String &, juce::Rectangle<int>, float = 12, juce::Colour = ink,
          int = juce::Justification::centredLeft);
} // namespace NeedleTheme
class NeedleLook final : public juce::LookAndFeel_V4
{
  public:
    NeedleLook();
    void drawButtonBackground(juce::Graphics &, juce::Button &, const juce::Colour &, bool, bool) override;
    void drawButtonText(juce::Graphics &, juce::TextButton &, bool, bool) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
    juce::Font getLabelFont(juce::Label &) override;
    juce::Font getPopupMenuFont() override;
    void drawTabButton(juce::TabBarButton &, juce::Graphics &, bool, bool) override;
    int getTabButtonBestWidth(juce::TabBarButton &, int) override;
    void drawComboBox(juce::Graphics &, int, int, bool, int, int, int, int, juce::ComboBox &) override;
    void drawToggleButton(juce::Graphics &, juce::ToggleButton &, bool, bool) override;
    void drawTickBox(juce::Graphics &, juce::Component &, float, float, float, float,
                     bool, bool, bool, bool) override;
    void drawDocumentWindowTitleBar(juce::DocumentWindow &, juce::Graphics &, int, int, int, int,
                                    const juce::Image *, bool) override;
    int getSliderThumbRadius(juce::Slider &) override;
    void drawRotarySlider(juce::Graphics &, int, int, int, int, float, float, float, juce::Slider &) override;
    void drawLinearSlider(juce::Graphics &, int, int, int, int, float, float, float,
                          juce::Slider::SliderStyle, juce::Slider &) override;
};
class CustomRotaryKnob final : public juce::Slider
{
  public:
    CustomRotaryKnob();
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;

  private:
    juce::Point<float> pointer, adjustedPointer;
};
class Platter final : public juce::Component
{
  public:
    explicit Platter(NeedleProcessor &);
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    void stop();
    void refreshFrame();
    bool keyPressed(const juce::KeyPress &) override;
    bool hitTest(int, int) override;
    void setAxis(juce::Point<float> localAxis)
    {
        axis = localAxis;
    }

  private:
    float angleAt(juce::Point<float>) const;
    NeedleProcessor &processor;
    juce::Point<float> axis;
    float previousAngle = 0;
    double lastDrag = 0, drawnTurn = 2, previewTurn = 0;
    unsigned previewSerial = 0;
    int64_t pendingPaintTick = 0;
};
class Waveform final : public juce::Component
{
  public:
    Waveform(NeedleProcessor &p, const NeedleProcessor::View &view) : processor(p), displayView(view)
    {
        setTitle("Waveform / seek");
        setWantsKeyboardFocus(true);
    }
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &e) override
    {
        move(e);
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        move(e);
    }
    bool keyPressed(const juce::KeyPress &) override;
    void refresh();

  private:
    void move(const juce::MouseEvent &e)
    {
        processor.seek.store(juce::jlimit(0.0, 0.999999, static_cast<double>(e.x) / getWidth()));
    }
    NeedleProcessor &processor;
    const NeedleProcessor::View &displayView;
    juce::Image quiet, played;
    unsigned cachedRevision = ~0u;
    int lastCursor = -1, lastCue = -1;
};
class MotionEditor final : public juce::Component
{
  public:
    MotionEditor(NeedleProcessor &, bool);
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    bool keyPressed(const juce::KeyPress &) override;

  private:
    void edit(juce::Point<float>);
    NeedleProcessor &processor;
    bool pattern;
    int lane = 0, node = 0;
};
class PianoRoll final : public juce::Component
{
  public:
    explicit PianoRoll(NeedleProcessor &);
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    bool keyPressed(const juce::KeyPress &) override;

  private:
    void toggle(int, int, bool);
    NeedleProcessor &processor;
    int dragEvent = -1, selectedStep = 0, selectedPitch = 0;
};
class FaderCurve final : public juce::Component
{
  public:
    explicit FaderCurve(NeedleProcessor &p) : processor(p)
    {
        setWantsKeyboardFocus(true);
        setTitle("Fader curve editor");
    }
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &e) override
    {
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent &) override;
    bool keyPressed(const juce::KeyPress &) override;

  private:
    NeedleProcessor &processor;
};
class ToolPage final : public juce::Component, private juce::ListBoxModel
{
  public:
    ToolPage(NeedleProcessor &, int);
    void resized() override;
    void paint(juce::Graphics &) override;
    void refresh();

  private:
    void fileAction(const juce::String &, const juce::String &, bool,
                    std::function<juce::Result(const juce::File &)>);
    juce::TextButton &button(const juce::String &, std::function<void()>);
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics &, int, int, bool) override;
    void listBoxItemClicked(int, const juce::MouseEvent &) override;
    NeedleProcessor &processor;
    int page;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    juce::ListBox browser;
    juce::ComboBox selection;
    juce::ToggleButton loop{"LOOP TAKE"};
    juce::Slider duration;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> durationAttachment;
    std::unique_ptr<MotionEditor> motion;
    std::unique_ptr<PianoRoll> piano;
    juce::Viewport pianoView;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String feedback;
};
class MappingPanel final : public juce::Component, private juce::Timer
{
  public:
    explicit MappingPanel(NeedleProcessor &);
    ~MappingPanel() override
    {
        stopTimer();
    }
    void resized() override;
    void paint(juce::Graphics &) override;

  private:
    void timerCallback() override;
    void refreshDevices();
    void apply();
    void number(const juce::String &, double, double, double, std::function<void(double)>);
    struct Field
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
    };
    NeedleProcessor &processor;
    std::vector<Field> fields;
    juce::ComboBox device;
    juce::TextButton rescan{"RESCAN"}, center{"CAL: CENTER"}, minimum{"CAL: MIN"}, maximum{"CAL: MAX"};
    juce::ToggleButton enabled{"ENABLE GAMEPAD"}, invertP{"INVERT PLATTER"}, invertF{"INVERT FADER"},
        binary{"BINARY CUT"};
    juce::TextButton learnSpeed{"LEARN SPEED CC"}, learnFader{"LEARN FADER CC"},
        learnPosition{"LEARN POSITION CC"};
    juce::TextButton panic{"RELEASE MIDI"};
    std::array<juce::TextButton, 6> learnLive;
    std::vector<GamepadBackend::Device> devices;
    int lastDevice = -999;
};
class NeedleEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           public juce::KeyListener,
                           public juce::FileDragAndDropTarget
{
  public:
    explicit NeedleEditor(NeedleProcessor &);
    ~NeedleEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress &, juce::Component *) override;
    bool keyPressed(const juce::KeyPress &k) override
    {
        return keyPressed(k, this);
    }
    bool keyStateChanged(bool, juce::Component *) override;
    bool isInterestedInFileDrag(const juce::StringArray &) override
    {
        return true;
    }
    void filesDropped(const juce::StringArray &, int, int) override;
    void mouseDown(const juce::MouseEvent &) override
    {
        grabKeyboardFocus();
    }
    void selectPanel(int index)
    {
        tabs.setCurrentTabIndex(index);
    }

  private:
    void timerCallback() override;
    void chooseFile();
    void updateKeys();
    void updateMixerBank();
    void refreshMixerControls();
    void slider(juce::Slider &, const juce::String &, const juce::String &, int hardware);
    NeedleProcessor &processor;
    NeedleProcessor::View displayView;
    NeedleLook look;
    Platter platter;
    Waveform waveform;
    FaderCurve curve;
    juce::TextButton load{"LOAD AUDIO"}, play{"PLAY"}, stop{"STOP"}, cue{"CUE"}, setCue{"SET CUE"},
        cut{"HOLD CUT / J"}, settings{"SETTINGS"}, midiShortcut{"MIDI"}, padShortcut{"PAD"}, record{"REC"};
    juce::TextButton bank{"BANK / MAIN"}, fxPower{"FX OFF"};
    juce::TextButton inputGate{"GATE OFF"}, motorPower{"MOTOR OFF"}, morphPower{"MORPH OFF"};
    juce::ToggleButton loop{"LOOP"}, reverse{"HAMSTER"}, cutMode{"CUT / MIX"}, deckReverse{"REVERSE"},
        sync{"HOST SYNC"}, performance{"LOW LATENCY"}, embedSample{"EMBED SAMPLE"};
    juce::Slider fader, pitch, gain, sensitivity, response, deckPitch, deckSensitivity;
    CustomRotaryKnob cutIn, curveAmount, bpm, swing;
    std::array<CustomRotaryKnob, 4> fxKnobs, eqKnobs;
    std::array<CustomRotaryKnob, 4> inputKnobs, performanceKnobs;
    std::array<CustomRotaryKnob, 5> motorKnobs;
    CustomRotaryKnob echoDivisionKnob;
    bool fxBank = false;
    juce::ComboBox mode, rpm, quantize, midiMode, source;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    std::vector<std::unique_ptr<ToolPage>> pages;
    juce::Viewport mappingView, djView;
    std::unique_ptr<MappingPanel> mapping;
    std::unique_ptr<DjPanel> djPanel;
    juce::Component surface, settingsPage;
    juce::Label settingsLabel, monitorReadout;
    juce::ToggleButton monitorToggle{"DSP DEBUG / CONTROL MONITOR"};
    juce::ComboBox monitorParameter;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltip{this, 600};
    std::set<int> latched;
    juce::AffineTransform designTransform;
    float displayMeter = 0;
    double lastPanelRefresh = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeedleEditor)
};
