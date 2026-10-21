#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cdaw::Palette
{
    // Dark theme. All UI colours come from here so the theme stays consistent.
    inline const juce::Colour windowBackground  { 0xff1b1d21 };
    inline const juce::Colour panelBackground   { 0xff23262b };
    inline const juce::Colour panelRaised       { 0xff2c3036 };
    inline const juce::Colour panelOutline      { 0xff3a3f47 };
    inline const juce::Colour laneBackground    { 0xff202328 };
    inline const juce::Colour laneAlternate     { 0xff1d2024 };
    inline const juce::Colour gridBar           { 0xff3d424a };
    inline const juce::Colour gridBeat          { 0xff2e3238 };
    inline const juce::Colour gridSub           { 0xff272a2f };
    inline const juce::Colour text              { 0xffe6e8eb };
    inline const juce::Colour textDim           { 0xff9aa1ab };
    inline const juce::Colour textDisabled      { 0xff60666f };
    inline const juce::Colour accent            { 0xff4fa3ff };
    inline const juce::Colour accentDim         { 0xff2c5d93 };
    inline const juce::Colour selection         { 0x554fa3ff };
    inline const juce::Colour playhead          { 0xffff5a4f };
    inline const juce::Colour loopRange         { 0x334fa3ff };
    inline const juce::Colour punchRange        { 0x33ff5a4f };
    inline const juce::Colour record            { 0xffe5383b };
    inline const juce::Colour play              { 0xff3ccf6e };
    inline const juce::Colour mute              { 0xfff2b134 };
    inline const juce::Colour solo              { 0xff3ccf6e };
    inline const juce::Colour monitor           { 0xff4fa3ff };
    inline const juce::Colour meterGreen        { 0xff3ccf6e };
    inline const juce::Colour meterYellow       { 0xfff2d034 };
    inline const juce::Colour meterRed          { 0xffe5383b };
    inline const juce::Colour automation        { 0xffffb84f };
    inline const juce::Colour marker            { 0xffd77cff };
    inline const juce::Colour tempo             { 0xff4fd6c8 };

    // Default track colours, cycled for new tracks.
    inline const juce::Array<juce::Colour> trackColours {
        juce::Colour (0xff4fa3ff), juce::Colour (0xff3ccf6e), juce::Colour (0xfff2b134),
        juce::Colour (0xffe5383b), juce::Colour (0xffd77cff), juce::Colour (0xff4fd6c8),
        juce::Colour (0xffff8a4f), juce::Colour (0xff9c8cff), juce::Colour (0xff8bd34f),
        juce::Colour (0xffff6fae)
    };

    inline juce::Colour trackColour (int index)
    {
        return trackColours[juce::jmax (0, index) % trackColours.size()];
    }
}
