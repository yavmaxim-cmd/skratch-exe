#include "Gamepad.h"
#if JUCE_WINDOWS
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <mmsystem.h>
#endif

std::vector<GamepadBackend::Device> GamepadBackend::devices()
{
    std::vector<Device> found;
#if JUCE_WINDOWS
    for (int i = 0; i < 4; ++i)
    {
        XINPUT_STATE state{};
        if (XInputGetState(i, &state) == ERROR_SUCCESS)
            found.push_back({i, "XInput / Xbox slot " + juce::String(i + 1), true});
    }
    for (UINT i = 0; i < joyGetNumDevs(); ++i)
    {
        JOYCAPSW caps{};
        JOYINFOEX state{};
        state.dwSize = sizeof(state);
        state.dwFlags = JOY_RETURNALL;
        if (joyGetDevCapsW(i, &caps, sizeof(caps)) == JOYERR_NOERROR &&
            joyGetPosEx(i, &state) == JOYERR_NOERROR)
            found.push_back({static_cast<int>(100 + i),
                             juce::String(caps.szPname) + " [HID/WinMM " + juce::String(i) + "] - calibrate",
                             false});
    }
#endif
    return found;
}

skratch::PadState GamepadBackend::poll(int id)
{
    skratch::PadState p;
    p.id = id;
#if JUCE_WINDOWS
    if (id >= 0 && id < 4)
    {
        XINPUT_STATE s{};
        if (XInputGetState(id, &s) != ERROR_SUCCESS)
            return p;
        p.connected = true;
        p.standard = true;
        p.axisCount = 6;
        p.buttonCount = 12;
        p.availableAxes = 0x3f;
        const auto stick = [](SHORT v) { return std::clamp(v / (v < 0 ? 32768.0f : 32767.0f), -1.0f, 1.0f); };
        p.axes = {stick(s.Gamepad.sThumbLX),
                  -stick(s.Gamepad.sThumbLY),
                  stick(s.Gamepad.sThumbRX),
                  -stick(s.Gamepad.sThumbRY),
                  s.Gamepad.bLeftTrigger / 127.5f - 1,
                  s.Gamepad.bRightTrigger / 127.5f - 1,
                  0,
                  0};
        const WORD masks[] = {XINPUT_GAMEPAD_A,
                              XINPUT_GAMEPAD_B,
                              XINPUT_GAMEPAD_X,
                              XINPUT_GAMEPAD_Y,
                              XINPUT_GAMEPAD_LEFT_SHOULDER,
                              XINPUT_GAMEPAD_RIGHT_SHOULDER,
                              XINPUT_GAMEPAD_DPAD_LEFT,
                              XINPUT_GAMEPAD_DPAD_RIGHT,
                              XINPUT_GAMEPAD_DPAD_UP,
                              XINPUT_GAMEPAD_DPAD_DOWN,
                              XINPUT_GAMEPAD_START,
                              XINPUT_GAMEPAD_BACK};
        for (int i = 0; i < 12; ++i)
            p.buttons[i] = (s.Gamepad.wButtons & masks[i]) != 0;
    }
    else if (id >= 100)
    {
        const UINT device = static_cast<UINT>(id - 100);
        JOYCAPSW caps{};
        JOYINFOEX s{};
        s.dwSize = sizeof(s);
        s.dwFlags = JOY_RETURNALL;
        if (joyGetDevCapsW(device, &caps, sizeof(caps)) != JOYERR_NOERROR ||
            joyGetPosEx(device, &s) != JOYERR_NOERROR)
            return p;
        p.connected = true;
        p.axisCount = 6;
        p.buttonCount = std::min(32, static_cast<int>(caps.wNumButtons));
        p.availableAxes = 3u | ((caps.wCaps & JOYCAPS_HASZ) ? 4u : 0u) |
                          ((caps.wCaps & JOYCAPS_HASR) ? 8u : 0u) | ((caps.wCaps & JOYCAPS_HASU) ? 16u : 0u) |
                          ((caps.wCaps & JOYCAPS_HASV) ? 32u : 0u);
        const DWORD vals[] = {s.dwXpos, s.dwYpos, s.dwZpos, s.dwRpos, s.dwUpos, s.dwVpos};
        const UINT mins[] = {caps.wXmin, caps.wYmin, caps.wZmin, caps.wRmin, caps.wUmin, caps.wVmin};
        const UINT maxs[] = {caps.wXmax, caps.wYmax, caps.wZmax, caps.wRmax, caps.wUmax, caps.wVmax};
        for (int i = 0; i < 6; ++i)
            p.axes[i] = maxs[i] > mins[i]
                            ? 2.0f * (vals[i] - mins[i]) / static_cast<float>(maxs[i] - mins[i]) - 1.0f
                            : 0;
        for (int i = 0; i < p.buttonCount; ++i)
            p.buttons[i] = (s.dwButtons & (DWORD(1) << i)) != 0;
        // POV is exposed in diagnostic slots if the driver has room.
        if (p.buttonCount <= 28 && s.dwPOV != JOY_POVCENTERED)
        {
            p.buttons[28] = s.dwPOV > 22500 || s.dwPOV < 4500;
            p.buttons[29] = s.dwPOV > 4500 && s.dwPOV < 13500;
            p.buttons[30] = s.dwPOV > 13500 && s.dwPOV < 22500;
            p.buttons[31] = s.dwPOV > 22500 && s.dwPOV < 31500;
            p.buttonCount = 32;
        }
    }
#endif
    return p;
}
