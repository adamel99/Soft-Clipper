# VIVE — Intelligent Clipper

A saturation/clipping plugin built with JUCE, featuring alias-free oversampled drive, five distinct saturation characters, a soft-knee ceiling clipper, and a real-time input/output waveform visualizer.

## Features

- **Input Gain** — pre-processing gain (-12 to +12 dB)
- **Drive** — alias-free saturation amount, processed at 4x oversampling
- **Saturation Modes** — Off, Clean, Tube, Tape, Solid State, and Asymmetric; Off disables the saturation character while the final ceiling remains active
- **Ceiling** — soft-knee clipping followed by a hard final output cap, so the signal sent to the host does not exceed the selected level
- **Mix** — dry/wet blend for parallel saturation
- **Output Gain** — post-processing gain
- **Genre Presets** — quick-start settings for Hip-Hop, EDM, Rock/Metal, Pop/R&B, Jazz/Acoustic, Lo-Fi, and Voice/Podcast
- **Live Visualizer** — dual input/output waveform display with peak/RMS traces, peak-hold indicators, gain reduction and overage metering

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
│   └── VisualizerComponent.cpp/h # Waveform visualizer and metering
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
| Saturation Mode | Off / Clean / Tube / Tape / Solid State / Asymmetric | Clean | Harmonic character of the drive stage; Off disables saturation shaping |
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
5. Saturation and soft-knee ceiling shaping at 4x oversampling
6. Downsample back to the host rate and align the dry path for Mix
7. Dry/wet mix
8. Hard ceiling applied to the mixed signal
9. Output gain applied last
10. Input/output signal pushed to the visualizer

## Saturation Modes

- **Off** — bypasses saturation shaping while retaining input/output gain and the final ceiling
- **Clean** — subtle tanh-based softening with a gentle cubic correction term
- **Tube** — classic tanh saturation, warm and rounded
- **Tape** — asymptotic soft clip (`x / (1 + |x|)`), smooth compression-like character
- **Solid State** — square-root-based soft clip, tighter and more transistor-like
- **Asymmetric** — different curves for positive/negative excursions, adding even-order harmonics

## Visualizer Notes

The `VisualizerComponent` renders a dual peak/RMS waveform display for input (blue) and output (pink) signals, with peak-hold ticks, a gain-reduction sidebar, and an overage meter showing how hard the signal is hitting the ceiling. Audio data streams to the UI thread via a lock-free FIFO fed from `pushSamples()` in the audio callback.
