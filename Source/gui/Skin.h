/*
    The Collapse of The Wave Function - bass drive, blend and cabinet
    Copyright (C) 2026 doctorspider42

    This program is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later version.

    It links against JUCE 8 under the AGPLv3, so every distributed binary additionally
    carries AGPLv3 section 13. See LICENSE and LICENSE.AGPLv3.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
    PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/

/*  Palette, type and the handful of drawing primitives every widget shares. Keeping them
    here means the whole panel can be re-tuned by editing a dozen numbers rather than
    hunting through paint methods.

    The panel is nearly black on purpose. Everything that glows is a signal - the wave,
    the value arcs, the lit state - so there has to be something for it to glow against,
    and a mid-grey chassis would swallow all of it.                                  */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace collapse::skin
{

namespace colour
{
    // The vacuum. Three stops rather than two, because a straight two-stop gradient over
    // this much height bands visibly on an 8-bit display.
    inline const juce::Colour voidTop   { 0xff080b14 };
    inline const juce::Colour voidMid   { 0xff05070f };
    inline const juce::Colour voidDeep  { 0xff020307 };

    inline const juce::Colour panelFill { 0x2a0e1420 };
    inline const juce::Colour panelEdge { 0x24a8c4ff };
    inline const juce::Colour recess    { 0xff010205 };

    inline const juce::Colour text      { 0xffd4dff2 };
    inline const juce::Colour textDim   { 0xff76829e };
    inline const juce::Colour textFaint { 0xff424b63 };

    inline const juce::Colour cold      { 0xff5ad4ff };   // the coherent end of everything
}

/** Every colour the panel derives from the current State and Drive.

    State owns the hue, and the ordering is the ordering of the four voicings: Drift is
    cold, Phase is violet, Decay is amber, Collapse is the red end. Drive is kept separate
    as `heat`, and only controls how much energy the panel shows - glow radius, bloom,
    how far the wave has fallen apart - so the two never fight over the same pixels. */
struct Palette
{
    juce::Colour accent    { 0xff5ad4ff };
    juce::Colour accentDim { 0xff214a5e };
    juce::Colour hot       { 0xffff8a4a };   // where the collapsed packets end up
    juce::Colour text      { colour::text };
    juce::Colour textDim   { colour::textDim };

    /** 0 at the coherent end, 1 at full drive. */
    float heat = 0.0f;
};

Palette paletteFor (float heat, int state);

/** juce::String(const char*) decodes as Latin-1, which turns any UTF-8 glyph in a source
    literal into mojibake. Everything non-ASCII goes through here. */
inline juce::String u8 (const char* utf8) { return juce::String::fromUTF8 (utf8); }

juce::Font labelFont (float height, bool bold = false);
juce::Font displayFont (float height);

/** Panel lettering is always tracked out - JUCE has no tracking parameter, so the glyphs
    are measured and placed one at a time. */
void drawTracked (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                  juce::Rectangle<float> area, juce::Justification justification,
                  juce::Colour c, float tracking);

/** Drawn once in shadow one pixel down, then in ink on top: reads as cut into the panel
    rather than printed on it. */
void drawEngraved (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                   juce::Rectangle<float> area, juce::Justification justification,
                   juce::Colour ink, float tracking);

/** Additive-looking bloom: several passes of the same shape, widening and fading. Cheaper
    and softer than a real blur, and it survives any scaling. */
void glowEllipse (juce::Graphics& g, juce::Point<float> centre, float radius,
                  juce::Colour c, float intensity, int layers = 5);

void glowPath (juce::Graphics& g, const juce::Path& path, juce::Colour c,
               float baseWidth, float intensity, int layers = 4);

/** Section frame: faint fill, a one-pixel edge that catches the accent along the top, and
    a soft inner shadow so it reads as recessed into the panel. */
void drawSectionFrame (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour accent);

void drawSectionTitle (juce::Graphics& g, const juce::String& title,
                       juce::Rectangle<float> area, juce::Colour accent);

/** A 128x128 tile of very low-contrast noise, generated once. Nothing physical is
    perfectly even, and a few per cent of grain over flat fills is most of what stops a
    dark panel looking like a dark rectangle. */
const juce::Image& noiseTile();

/** The look-and-feel set on the editor itself, for the two things JUCE draws that no widget
    here owns: the resize grip in the bottom-right corner, and the preset menu.

    Both default to a light grey that belongs to a different plug-in entirely - the menu
    especially, which opens as a bright rectangle in the middle of a panel that is
    otherwise nearly black. */
class PanelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();

    void setAccent (juce::Colour c) noexcept;

    void drawCornerResizer (juce::Graphics&, int w, int h,
                            bool isMouseOver, bool isMouseDragging) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    juce::Font getPopupMenuFont() override;

private:
    juce::Colour accent { colour::cold };
};

} // namespace collapse::skin
