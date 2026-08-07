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

#include "Skin.h"

#include "../dsp/Utils.h"

namespace collapse::skin
{

Palette paletteFor (float heat, int state)
{
    // One hue per state, ordered the way the states are: cold, violet, amber, red. The
    // panel tells you which voicing is loaded before you have read the selector.
    struct Entry { juce::uint32 accent, accentDim, hot; };

    static const Entry entries[] =
    {
        { 0xff5ad4ff, 0xff1d4a60, 0xff8fe6ff },   // Drift    - cold
        { 0xffa98cff, 0xff342a63, 0xffd0b4ff },   // Phase    - violet
        { 0xffffb14f, 0xff5c3e18, 0xffffd79a },   // Decay    - amber
        { 0xffff6a44, 0xff5e2418, 0xffffb08c },   // Collapse - red
    };

    constexpr auto count = (int) (sizeof (entries) / sizeof (entries[0]));
    const auto& e = entries[juce::jlimit (0, count - 1, state)];

    Palette p;
    p.heat      = juce::jlimit (0.0f, 1.0f, heat);
    p.accent    = juce::Colour (e.accent);
    p.accentDim = juce::Colour (e.accentDim);
    p.hot       = juce::Colour (e.hot);

    // Everything gets a little warmer and brighter as the thing is driven. Subtle enough
    // that nobody would name it, obvious enough that the panel feels connected to the
    // sound rather than painted behind it.
    p.accent = p.accent.brighter (0.16f * p.heat);
    p.text   = colour::text.interpolatedWith (p.accent, 0.10f * p.heat);

    return p;
}

juce::Font labelFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain))
               .withHorizontalScale (0.94f);
}

juce::Font displayFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                          juce::Font::plain))
               .withHorizontalScale (0.90f);
}

void drawTracked (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                  juce::Rectangle<float> area, juce::Justification justification,
                  juce::Colour c, float tracking)
{
    if (text.isEmpty())
        return;

    juce::Array<float> widths;
    auto total = 0.0f;

    for (int i = 0; i < text.length(); ++i)
    {
        const auto w = juce::GlyphArrangement::getStringWidth (font, text.substring (i, i + 1));
        widths.add (w);
        total += w + tracking;
    }

    total -= tracking;

    auto x = area.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - total;

    const auto baseline = area.getCentreY() + font.getAscent() * 0.5f - font.getDescent() * 0.35f;

    g.setFont (font);
    g.setColour (c);

    for (int i = 0; i < text.length(); ++i)
    {
        g.drawSingleLineText (text.substring (i, i + 1), juce::roundToInt (x),
                              juce::roundToInt (baseline));
        x += widths[i] + tracking;
    }
}

void drawEngraved (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                   juce::Rectangle<float> area, juce::Justification justification,
                   juce::Colour ink, float tracking)
{
    drawTracked (g, text, font, area.translated (0.0f, 1.0f), justification,
                 juce::Colours::black.withAlpha (0.65f), tracking);
    drawTracked (g, text, font, area, justification, ink, tracking);
}

void glowEllipse (juce::Graphics& g, juce::Point<float> centre, float radius,
                  juce::Colour c, float intensity, int layers)
{
    for (int i = layers; i >= 1; --i)
    {
        const auto t = (float) i / (float) layers;
        const auto r = radius * (1.0f + t * 1.9f);
        const auto a = intensity * (1.0f - t) * (1.0f - t) * 0.55f;

        if (a <= 0.002f)
            continue;

        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, a)));
        g.fillEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
    }
}

void glowPath (juce::Graphics& g, const juce::Path& path, juce::Colour c,
               float baseWidth, float intensity, int layers)
{
    for (int i = layers; i >= 1; --i)
    {
        const auto t = (float) i / (float) layers;
        const auto w = baseWidth * (1.0f + t * 3.2f);
        const auto a = intensity * (1.0f - t) * 0.5f;

        if (a <= 0.002f)
            continue;

        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, a)));
        g.strokePath (path, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, intensity)));
    g.strokePath (path, juce::PathStrokeType (baseWidth, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void drawSectionFrame (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour accent)
{
    constexpr float corner = 9.0f;

    juce::ColourGradient fill (juce::Colour (0x2c101827), area.getX(), area.getY(),
                               juce::Colour (0x12060a12), area.getX(), area.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (area, corner);

    // Grain, so the fill is not a flat digital wash.
    g.setTiledImageFill (noiseTile(), 0, 0, 0.028f);
    g.fillRoundedRectangle (area, corner);

    // The accent only touches the top edge: light falls from above everywhere on this
    // panel, and the frames have to agree with that.
    juce::ColourGradient edge (accent.withAlpha (0.34f), area.getCentreX(), area.getY(),
                               accent.withAlpha (0.05f), area.getCentreX(), area.getBottom(),
                               false);
    g.setGradientFill (edge);
    g.drawRoundedRectangle (area, corner, 1.1f);

    g.setColour (juce::Colours::black.withAlpha (0.40f));
    g.drawRoundedRectangle (area.reduced (1.3f), corner - 1.3f, 1.0f);
}

void drawSectionTitle (juce::Graphics& g, const juce::String& title,
                       juce::Rectangle<float> area, juce::Colour accent)
{
    const auto font = labelFont (10.0f, true);

    drawEngraved (g, title, font, area, juce::Justification::centredLeft,
                  accent.withAlpha (0.88f), 2.7f);

    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, title)
                             + 2.7f * (float) title.length();
    auto rule = area.withTrimmedLeft (textWidth + 10.0f).withHeight (1.0f)
                    .withY (area.getCentreY());

    if (rule.getWidth() > 4.0f)
    {
        juce::ColourGradient grad (accent.withAlpha (0.26f), rule.getX(), 0.0f,
                                   accent.withAlpha (0.0f), rule.getRight(), 0.0f, false);
        g.setGradientFill (grad);
        g.fillRect (rule);
    }
}

const juce::Image& noiseTile()
{
    static const juce::Image tile = []
    {
        juce::Image image (juce::Image::ARGB, 128, 128, true);
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        dsp::Xorshift rng (0x51ade42au);

        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
            {
                const auto v = (juce::uint8) (108 + (int) (rng.nextFloat() * 92.0f));
                data.setPixelColour (x, y, juce::Colour (v, v, (juce::uint8) (v + 8)));
            }

        return image;
    }();

    return tile;
}

PanelLookAndFeel::PanelLookAndFeel()
{
    setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
    setAccent (colour::cold);
}

void PanelLookAndFeel::setAccent (juce::Colour c) noexcept
{
    accent = c;

    setColour (juce::PopupMenu::backgroundColourId,          juce::Colour (0xff0a0f19));
    setColour (juce::PopupMenu::textColourId,                colour::text);
    setColour (juce::PopupMenu::headerTextColourId,          c);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId,     c.brighter (0.5f));
}

/** Three ticks rather than JUCE's default staircase of lines, and in the panel's own
    colours: the stock one is drawn in near-white and is the brightest thing on the whole
    editor, which for a control nobody uses twice is the wrong amount of attention. */
void PanelLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h,
                                          bool isMouseOver, bool isMouseDragging)
{
    const auto size = (float) juce::jmin (w, h);
    const auto lit = isMouseOver || isMouseDragging;

    for (int i = 1; i <= 3; ++i)
    {
        const auto inset = size * (0.16f + 0.20f * (float) i);

        g.setColour (accent.withAlpha (lit ? 0.55f : 0.22f));
        g.drawLine ((float) w - inset, (float) h - size * 0.14f,
                    (float) w - size * 0.14f, (float) h - inset, 1.2f);
    }
}

void PanelLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);

    g.setColour (juce::Colour (0xf20a0f19));
    g.fillRoundedRectangle (bounds, 6.0f);

    g.setTiledImageFill (noiseTile(), 0, 0, 0.025f);
    g.fillRoundedRectangle (bounds, 6.0f);

    g.setColour (accent.withAlpha (0.30f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}

juce::Font PanelLookAndFeel::getPopupMenuFont()
{
    return labelFont (13.0f);
}

} // namespace collapse::skin
