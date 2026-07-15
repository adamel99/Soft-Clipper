# VIVE — Intelligent Clipper

A saturation/clipping plugin built with JUCE, featuring alias-free oversampled drive, five distinct saturation characters, a soft-knee ceiling clipper, and a real-time input/output waveform visualizer with a reactive cat mascot that responds to your gain staging.

## Features

- **Input Gain** — pre-processing gain (-12 to +12 dB)
- **Drive** — alias-free saturation amount, processed at 4x oversampling
- **5 Saturation Modes** — Clean, Tube, Tape, Solid State, and Asymmetric, each with a distinct harmonic character and matched makeup gain
- **Ceiling** — soft-knee clipper with a wide, gradual knee starting ~2 dB below the ceiling, followed by a hard safety clip
- **Mix** — dry/wet blend for parallel saturation
- **Output Gain** — post-processing gain
- **Genre Presets** — quick-start settings for Hip-Hop, EDM, Rock/Metal, Pop/R&B, Jazz/Acoustic, Lo-Fi, and Voice/Podcast
- **Live Visualizer** — dual input/output waveform display with peak/RMS traces, peak-hold indicators, gain reduction and overage metering
- **Cat Mascot** — an animated cat that idles, perks up, gets nervous, or panics based on your output level and clipping activity

## Screenshots

*(Add screenshots of the plugin UI here)*

## Requirements

- **JUCE** (developed with Projucer — see the `.jucer` file for module configuration)
- **Xcode** (macOS build target)
- macOS 10.13+ (adjust based on your deployment target in Projucer)

## Building

This project is developed using **Projucer** and **Xcode**.

1. Open the `.jucer` file in Projucer
2. Ensure your JUCE modules path is correctly set in Projucer's global paths
3. Click **Save and Open in IDE** to regenerate the Xcode project and open it
4. Build in Xcode (`Cmd + B`) — this compiles the **VST3** and **AU** (Audio Unit) plugin formats, depending on your Projucer export settings

> **Note:** `JuceLibraryCode/` and the `Builds/` output directories are regenerated automatically by Projucer/Xcode and are not tracked in version control (see `.gitignore`).

## Project Structure

```
Vive/
├── Vive.jucer                   # Projucer project file
├── Source/
│   ├── PluginProcessor.cpp/h    # DSP: gain staging, oversampled saturation, ceiling clip
│   ├── PluginEditor.cpp/h       # Main plugin UI, knob layout, saturation mode selector
│   ├── Params.h                 # Parameter IDs, ranges, genre presets
│   ├── CustomLookAndFeel.cpp/h  # Rotary knob, combo box, and popup menu styling
│   └── VisualizerComponent.cpp/h # Waveform visualizer + animated cat mascot
├── Builds/                       # (generated, ignored)
└── JuceLibraryCode/               # (generated, ignored)
```

## Parameters

| Parameter | Range | Default | Description |
|---|---|---|---|
| Input Gain | -12 – +12 dB | 0 dB | Pre-saturation gain |
| Drive | 0–100% | 0% | Saturation intensity |
| Ceiling | -15 – 0 dB | 0 dB | Soft-knee clip threshold |
| Mix | 0–100% | 100% | Dry/wet blend |
| Output Gain | -12 – 0 dB | 0 dB | Post-processing gain |
| Saturation Mode | Clean / Tube / Tape / Solid State / Asymmetric | Clean | Harmonic character of the drive stage |
| Genre | See presets below | *(none selected)* | One-click starting point per genre |

### Genre Presets

| Genre | Input Gain | Drive | Ceiling | Output Gain | Mix | Sat Mode |
|---|---|---|---|---|---|---|
| Hip-Hop | +2 dB | 65% | -0.3 dB | 0 dB | 100% | Tube |
| EDM | +3 dB | 50% | -0.3 dB | 0 dB | 100% | Solid State |
| Rock/Metal | +1 dB | 80% | -0.5 dB | 0 dB | 100% | Tape |
| Pop/R&B | 0 dB | 30% | -0.3 dB | -0.5 dB | 100% | Clean |
| Jazz/Acoustic | 0 dB | 10% | -1.0 dB | -1.0 dB | 80% | Clean |
| Lo-Fi | +2 dB | 90% | -0.3 dB | 0 dB | 100% | Asymmetric |
| Voice/Podcast | 0 dB | 40% | -1.0 dB | -1.0 dB | 100% | Clean |

## Signal Flow

1. Input gain applied
2. Signal captured for dry mix and input metering
3. 4x oversampling engaged
4. Drive/saturation applied (mode-dependent curve + makeup gain)
5. Soft-knee ceiling clip (tanh-shaped knee starting ~2 dB below ceiling)
6. Downsampled back to base rate, followed by a hard safety clip at the ceiling
7. Dry/wet mix
8. Output gain applied
9. Input/output signal pushed to the visualizer

## Saturation Modes

- **Clean** — subtle tanh-based softening with a gentle cubic correction term
- **Tube** — classic tanh saturation, warm and rounded
- **Tape** — asymptotic soft clip (`x / (1 + |x|)`), smooth compression-like character
- **Solid State** — square-root-based soft clip, tighter and more transistor-like
- **Asymmetric** — different curves for positive/negative excursions, adding even-order harmonics

## Visualizer Notes

The `VisualizerComponent` renders a dual peak/RMS waveform display for input (blue) and output (pink) signals, with peak-hold ticks, a gain-reduction sidebar, and an overage meter showing how hard the signal is hitting the ceiling. Audio data streams to the UI thread via a lock-free FIFO fed from `pushSamples()` in the audio callback.

The cat mascot's mood — Idle, Active, Nervous, Peak, or Clipping — is driven directly by the current output peak level and clipping state, with distinct ear position, pupil dilation, tail motion, and mouth shape for each state.
