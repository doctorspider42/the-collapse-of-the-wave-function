# Third-party components

## JUCE 8

- Version: 8.0.15 (pinned as the submodule `libs/JUCE`)
- Licence: **AGPLv3**, as used here — see `libs/JUCE/LICENSE.md`
- Home: <https://juce.com>

Because JUCE is linked under the AGPLv3, every binary distributed from this project is a
combined work carrying AGPLv3 section 13 in addition to the GPLv3 terms of the code in
`Source/` and `tools/`. Both texts ship with every release: `LICENSE` (GPLv3) and
`LICENSE.AGPLv3`.

JUCE itself is not redistributed in the archives — the JUCE EULA forbids that. It is
pinned as a submodule so the exact revision behind any binary is recorded in this
repository's history, and every release archive names both SHAs in `BUILD-INFO.txt`.

## Cabinet impulse responses

- Source: **Jester's Brutal Pack 1.0** by Bastian Karschewski (Jester Dyne Productions)
- Licence: **CC0 1.0 Universal** — public domain dedication
- Home: <https://www.jester-dyne-productions.com/brutal-ir-pack/>

Four of the pack's fifteen measurements are compiled into `Source/dsp/CabinetIr.h` by
`tools/make_cabinet_ir.py`. CC0 places no conditions on redistribution, so they carry no
obligation of their own into the binary; the attribution here is courtesy, not licence
compliance.

The cabinet measured is a modified oversized Behringer BG412S 4×12 — a guitar cabinet.
This is a bass plug-in, and a guitar 4×12 has essentially nothing below 80 Hz, so the
plug-in does not pretend otherwise: `CabSim` uses the measurements for the midrange and
top, which is what a capture is genuinely good for, and synthesises the bottom octave with
a modelled ported low end. See the comment at the top of `Source/dsp/CabSim.h`.

The speakers, as named in the pack's handbook:

| Voicing | Speaker | Captures used |
|---|---|---|
| **Cone** | Celestion Vintage 30 | `1_Cookie_Monster` (SM57), `9_Devils_Cunnilingus` (PDMIC75) |
| **Steel** | Eminence DV-77 | `2_Darth_Genocider` (SM57), `10_October_32th` (SM57, off-axis) |

## Shared code

`Source/dsp/Biquad.h`, `Envelopes.h`, `DelayLine.h` and `Oversampler.h` are shared verbatim
with **Voxy Drive**, a sibling plug-in by the same author under the same licence. They are
duplicated rather than factored into a library because two projects do not need one.

## VST3 SDK

The VST3 SDK subset bundled with JUCE 8.0.15 lives under
`libs/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/` and is
distributed by Steinberg under the **MIT licence** (© 2025 Steinberg Media Technologies
GmbH). Check `LICENSE.txt` in that directory for the copy actually vendored here.

> VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other
> countries.

No Steinberg logo is used anywhere in this project. Using the VST logo requires a separate
usage agreement with Steinberg, which this project does not hold.

## Fonts

No fonts are shipped. The panel asks the operating system for its default sans-serif face,
so it renders with whatever the host machine already has installed.
