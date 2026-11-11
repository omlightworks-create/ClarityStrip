# ClarityStrip — audio cleanup plugin for DaVinci Resolve (Fairlight)

ClarityStrip is a VST3 (Windows and macOS) and AU (macOS) effect. Resolve loads it on the
**Fairlight** page and on the **Edit** page (Effects → Audio FX).

```
 Input → NOISE REMOVER → EQ → DYNAMICS → COMPRESSOR → ENHANCER → LIMITER → OUTPUT
                      ▲                                               │
                      └──────── AUTO CLEAN analyzer ◄─────────────────┘
```

## The controls

| Knob | What one knob does (0 → 100 %) |
|---|---|
| **DYNAMICS** | Downward expander that follows the measured noise floor, so the gaps between words stay quiet. Adds transient punch. |
| **EQ** | Rumble high-pass (20 → 90 Hz), mud cut at 280 Hz (up to −5 dB), presence lift at 3.3 kHz (up to +4.5 dB). |
| **COMPRESSOR** | Soft-knee, stereo-linked compressor. Threshold −12 → −30 dB, ratio 1:1 → 6:1, automatic make-up gain. |
| **LIMITER** | Look-ahead brickwall limiter. Drives the signal up to +9 dB into a **−1 dBFS** ceiling. |
| **ENHANCER** | High-band harmonic exciter, an "air" shelf at 11 kHz (up to +5 dB) and low warmth at 120 Hz. |
| **NOISE REMOVER** | STFT spectral denoiser that learns the noise by itself (hiss, hum, fans, room tone, traffic). Up to about 30 dB of reduction. |
| **AUTO CLEAN** | Analyzes the audio continuously. It measures the noise floor, level, dynamic range, mud-to-presence balance, air and crest factor. At 0 % the manual knobs run as set. At 100 % every module runs at the analyzer's suggested value. Anything in between blends the two. A white outer ring on each knob shows the value actually in use. |
| **ANALYZE 5s** | Listens to 5 s of playing audio and **moves every knob for you**. Fine-tune by ear afterwards. |
| **OUTPUT** | ±12 dB output trim. |

**Visualizer screen.** Click the screen to switch views:
- **SPECTRUM**: input (grey) and cleaned output (colour) on a log scale, 20 Hz–20 kHz, with peak hold and a +3 dB/oct display tilt.
- **SCOPE**: triggered oscilloscope with the latency-aligned input as a ghost trace.
- **WAVEFORM**: scrolling history of the output, with the input behind it.

**Meters:** IN and OUT peak (with hold), GR (compressor + limiter gain reduction) and NR (noise reduction).

Latency is about 1,100 samples (about 23 ms at 48 kHz). The plugin reports it to the host, so Resolve keeps the
audio in sync automatically.

## Getting the plugin binary

### Option A: let GitHub build it (no tools needed)
1. Create a new GitHub repository and upload this whole folder, including `.github/`.
2. Open the **Actions** tab. The *Build ClarityStrip* workflow runs on its own. You can also start it with "Run workflow".
3. When it finishes (about 10 min), download **ClarityStrip-Windows** or **ClarityStrip-macOS** from the run's *Artifacts*.

### Option B: build it yourself
You need CMake 3.22+ and a C++17 compiler (Visual Studio 2022 on Windows, Xcode on macOS).
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release        # downloads JUCE 8 automatically
cmake --build build --config Release
```
The output is in `build/ClarityStrip_artefacts/Release/` (VST3, AU, Standalone). On Windows, run the commands from
"x64 Native Tools Command Prompt for VS 2022".

## Installing in DaVinci Resolve
1. Copy the plugin:
   - **Windows:** `ClarityStrip.vst3` → `C:\Program Files\Common Files\VST3\`
   - **macOS:** `ClarityStrip.vst3` → `/Library/Audio/Plug-Ins/VST3/` and `ClarityStrip.component` → `/Library/Audio/Plug-Ins/Components/`.
     The build is unsigned. If macOS blocks it, run
     `sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/ClarityStrip.vst3`.
2. In Resolve, open **DaVinci Resolve → Preferences → System → Audio Plugins**. Make sure the VST folder is listed
   and ticked, then restart Resolve.
3. Use the plugin:
   - **Fairlight page:** in the Mixer, click **+** in a track's Effects slot → VST3 / AU → *OM Audio → ClarityStrip*.
   - **Edit page:** Effects → **Audio FX** → drag *ClarityStrip* onto a clip or track.

## Quick workflow
1. Play the clip and press **ANALYZE 5s**. The knobs move to suit that audio.
2. Raise **AUTO CLEAN** if the material changes over the clip (different speakers, locations, music beds).
3. Adjust by ear. For dialogue, raise NOISE REMOVER until the background is gone, then stop before the voice
   sounds "watery". Leave LIMITER around 40–60 % for YouTube or social loudness.

## Project layout
```
Source/DSP/ClarityDSP.h      all DSP (pure C++, no JUCE): denoiser, EQ, dynamics, compressor, enhancer, limiter, analyzer
Source/PluginProcessor.*     JUCE wrapper, parameters, state save/restore, latency reporting
Source/PluginEditor.*        UI: knobs, AUTO ring, visualizer (spectrum/scope/waveform), meters
tests/offline_test.cpp       offline DSP tests (null test, denoise depth, limiter ceiling, analyzer)
```
Run the DSP tests on their own with:
`g++ -std=c++17 -O2 -ISource tests/offline_test.cpp -o dsptest && ./dsptest`

JUCE is used under its GPLv3 / commercial licence. Distributing the binary closed-source requires a JUCE licence.
