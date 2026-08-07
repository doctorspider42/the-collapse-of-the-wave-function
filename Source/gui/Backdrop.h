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

/*  Everything that never moves: the vacuum, the interference pattern behind it, the
    section frames and all the lettering. All of it is expensive to draw and none of it
    changes between frames, so it is rendered once into an image at the display's real
    pixel density and blitted after that.

    That matters more than it sounds. The wave visualiser repaints thirty times a second
    and is not opaque, so without the cache every one of those frames would re-run a few
    hundred gradient and path operations underneath it.                               */

#pragma once

#include "Skin.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace collapse::gui
{

struct Section
{
    juce::Rectangle<float> bounds;
    juce::String title;

    /** A frame that follows a colour of its own rather than the State's. The coherent path
        is drawn cold everywhere else on the panel, and a frame around it in the drive
        path's accent would undo that in one stroke. Transparent means "use the palette". */
    juce::Colour tint {};
};

class Backdrop final : public juce::Component
{
public:
    Backdrop();

    void setSections (std::vector<Section> newSections);

    /** The accent follows the State selector, so the cache has to be thrown away when it
        changes. That happens on a click, not on a frame, so redrawing the scene is free
        in the only sense that matters. */
    void setPalette (const skin::Palette& newPalette);

    /** The title is painted here rather than by the editor. The backdrop is opaque and
        covers the whole panel, so anything the editor draws underneath it is drawn
        underneath it - which is exactly as visible as it sounds. */
    void setTitle (juce::String newTitle);

    void setFooter (juce::String left, juce::String right);

    void paint (juce::Graphics&) override;

private:
    void paintScene (juce::Graphics&) const;
    void paintInterference (juce::Graphics&, juce::Rectangle<float> bounds) const;

    std::vector<Section> sections;
    skin::Palette palette;
    juce::String title;
    juce::String footerLeft, footerRight;
    juce::Image cache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Backdrop)
};

} // namespace collapse::gui
