#pragma once
#include "Performance.h"
#include <juce_core/juce_core.h>

// Poll on the processor's service thread, never on the audio callback.
class GamepadBackend
{
  public:
    struct Device
    {
        int id;
        juce::String name;
        bool standard;
    };
    std::vector<Device> devices();
    skratch::PadState poll(int id);
};
