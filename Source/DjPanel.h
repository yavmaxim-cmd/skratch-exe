#pragma once
#include "PluginProcessor.h"

class DjPanel final : public juce::Component, private juce::Timer
{
  public:
    explicit DjPanel(NeedleProcessor &);
    void resized() override;
    void paint(juce::Graphics &) override;
    static juce::String valueText(dj::Param, double);

  private:
    struct Field
    {
        juce::Label title;
        juce::Slider slider;
        juce::TextButton learn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void timerCallback() override;
    NeedleProcessor &processor;
    juce::ToggleButton setCue{"SET CUE"};
    std::array<juce::TextButton, 8> cues;
    std::array<juce::TextButton, 4> actions;
    std::array<Field, dj::count> fields;
};
