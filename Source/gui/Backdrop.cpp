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

#include "Backdrop.h"

#include "../dsp/Utils.h"

namespace collapse::gui
{

Backdrop::Backdrop()
{
    // Opaque, so JUCE never bothers painting anything underneath - but that promise means
    // paint() has to cover every pixel of the bounds, corners included.
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

void Backdrop::setSections (std::vector<Section> newSections)
{
    sections = std::move (newSections);
    cache = {};
    repaint();
}

void Backdrop::setPalette (const skin::Palette& newPalette)
{
    palette = newPalette;
    cache = {};
    repaint();
}

void Backdrop::setTitle (juce::String newTitle)
{
    title = std::move (newTitle);
    cache = {};
    repaint();
}

void Backdrop::setFooter (juce::String left, juce::String right)
{
    footerLeft  = std::move (left);
    footerRight = std::move (right);
    cache = {};
    repaint();
}

/** Two coherent sources and the fringes they make. It is the picture the plug-in is named
    after, so it is the wallpaper - drawn at an alpha low enough that you notice it only
    once you have been looking at the panel for a while, which is exactly the right amount
    of wallpaper. Circles rather than a computed intensity field: a few dozen stroked arcs
    cost nothing, and per-pixel arithmetic over 840x660 would not be free even once. */
void Backdrop::paintInterference (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    const auto slitGap = bounds.getWidth() * 0.19f;
    const auto originY = bounds.getY() - bounds.getHeight() * 0.28f;

    const juce::Point<float> sources[] =
    {
        { bounds.getCentreX() - slitGap, originY },
        { bounds.getCentreX() + slitGap, originY },
    };

    const auto wavelength = bounds.getWidth() * 0.041f;
    const auto reach = juce::jmax (bounds.getWidth(), bounds.getHeight()) * 1.75f;
    const auto rings = (int) (reach / wavelength);

    for (const auto& s : sources)
    {
        for (int i = 1; i <= rings; ++i)
        {
            const auto r = (float) i * wavelength;

            // Amplitude falls with distance, so the pattern is dense and faint at the
            // edges rather than a uniform target.
            const auto fade = 1.0f - (float) i / (float) rings;
            const auto alpha = 0.085f * fade * fade;

            if (alpha < 0.0020f)
                continue;

            g.setColour (palette.accent.withAlpha (alpha));
            g.drawEllipse (s.x - r, s.y - r, r * 2.0f, r * 2.0f, 1.0f);
        }
    }
}

void Backdrop::paintScene (juce::Graphics& g) const
{
    const auto bounds = getLocalBounds().toFloat();

    // ---- the vacuum ---------------------------------------------------------
    // Three stops: a straight two-stop gradient over this height bands visibly on an
    // 8-bit display, and banding is the one artefact a nearly black panel cannot hide.
    juce::ColourGradient space (skin::colour::voidTop, bounds.getCentreX(), 0.0f,
                                skin::colour::voidDeep, bounds.getCentreX(), bounds.getBottom(),
                                false);
    space.addColour (0.46, skin::colour::voidMid);
    g.setGradientFill (space);
    g.fillRect (bounds);

    paintInterference (g, bounds);

    // The fringes belong to the top of the panel. Left to run the full height, the outer
    // arcs are so wide that they cross the bottom corners as near-straight diagonals and
    // read as a scratch on the artwork rather than as part of a pattern. Veiling them out
    // with the colour the background is already heading towards costs one gradient.
    {
        juce::ColourGradient veil (skin::colour::voidDeep.withAlpha (0.0f),
                                   bounds.getCentreX(), bounds.getHeight() * 0.12f,
                                   skin::colour::voidDeep, bounds.getCentreX(),
                                   bounds.getHeight() * 0.74f, false);
        g.setGradientFill (veil);
        g.fillRect (bounds);
    }

    // ---- a wash of the state's colour from above ----------------------------
    {
        const auto r = bounds.getWidth() * 0.78f;
        juce::ColourGradient wash (palette.accent.withAlpha (0.085f),
                                   bounds.getCentreX(), bounds.getY() - r * 0.25f,
                                   palette.accent.withAlpha (0.0f),
                                   bounds.getCentreX(), bounds.getY() + r * 0.75f, true);
        g.setGradientFill (wash);
        g.fillRect (bounds);
    }

    // ---- grain --------------------------------------------------------------
    g.setTiledImageFill (skin::noiseTile(), 0, 0, 0.030f);
    g.fillRect (bounds);

    // ---- vignette -----------------------------------------------------------
    {
        const auto r = juce::jmax (bounds.getWidth(), bounds.getHeight()) * 0.80f;
        juce::ColourGradient v (juce::Colours::transparentBlack,
                                bounds.getCentreX(), bounds.getCentreY(),
                                juce::Colours::black.withAlpha (0.76f),
                                bounds.getCentreX() + r, bounds.getCentreY(), true);
        v.addColour (0.55, juce::Colours::black.withAlpha (0.05f));
        g.setGradientFill (v);
        g.fillRect (bounds);
    }

    // ---- the title ----------------------------------------------------------
    if (title.isNotEmpty())
    {
        auto header = juce::Rectangle<float> (24.0f, 14.0f, bounds.getWidth() * 0.62f, 34.0f);

        skin::drawEngraved (g, title, skin::labelFont (13.0f, true), header,
                            juce::Justification::centredLeft,
                            palette.text.withAlpha (0.94f), 3.6f);

        // A hairline under it, fading out towards the controls on the right.
        auto rule = header.withTop (header.getBottom() - 1.0f).withHeight (1.0f)
                          .withWidth (bounds.getWidth() * 0.44f);
        juce::ColourGradient grad (palette.accent.withAlpha (0.34f), rule.getX(), 0.0f,
                                   palette.accent.withAlpha (0.0f), rule.getRight(), 0.0f,
                                   false);
        g.setGradientFill (grad);
        g.fillRect (rule);
    }

    // ---- section frames -----------------------------------------------------
    for (const auto& s : sections)
    {
        const auto accent = s.tint.isTransparent() ? palette.accent : s.tint;

        skin::drawSectionFrame (g, s.bounds, accent);

        if (s.title.isNotEmpty())
        {
            auto label = s.bounds.withHeight (20.0f).translated (0.0f, 7.0f)
                                 .reduced (13.0f, 0.0f);
            skin::drawSectionTitle (g, s.title, label, accent);
        }
    }

    // ---- footer -------------------------------------------------------------
    if (footerLeft.isNotEmpty() || footerRight.isNotEmpty())
    {
        auto footer = bounds.withTop (bounds.getBottom() - 26.0f).reduced (24.0f, 0.0f);

        skin::drawTracked (g, footerLeft, skin::labelFont (8.5f), footer,
                           juce::Justification::centredLeft,
                           skin::colour::textFaint, 1.4f);
        skin::drawTracked (g, footerRight, skin::labelFont (8.5f), footer,
                           juce::Justification::centredRight,
                           skin::colour::textFaint, 1.4f);
    }
}

void Backdrop::paint (juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // setBufferedToImage would cache at logical size and go soft the moment the editor is
    // scaled up, so the cache is built at the real device resolution instead.
    const auto scale = juce::jlimit (1.0f, 3.0f,
                                     (float) g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto w = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale));
    const auto h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));

    if (cache.isNull() || cache.getWidth() != w || cache.getHeight() != h)
    {
        cache = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics ig (cache);
        ig.addTransform (juce::AffineTransform::scale (scale));
        paintScene (ig);
    }

    g.drawImage (cache, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

} // namespace collapse::gui
