#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

//==============================================================================
namespace Palette
{
    const juce::Colour bg        { 0xff0c0e13 };
    const juce::Colour panel     { 0xff151922 };
    const juce::Colour panelEdge { 0xff232938 };
    const juce::Colour text      { 0xffe8ecf4 };
    const juce::Colour dim       { 0xff7d8699 };
    const juce::Colour track     { 0xff262c3a };

    const juce::Colour dynamics  { 0xffff8a3d };
    const juce::Colour eq        { 0xffffd23d };
    const juce::Colour comp      { 0xff4dd2ff };
    const juce::Colour limiter   { 0xffff4d6d };
    const juce::Colour enhancer  { 0xffb07bff };
    const juce::Colour denoise   { 0xff3dffa8 };
    const juce::Colour autoCol   { 0xff7cf3ff };
}

//==============================================================================
class ClarityLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ClarityLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
};

//==============================================================================
class MacroKnob : public juce::Component
{
public:
    MacroKnob (juce::String name, juce::String subtitle, juce::Colour colour, bool big = false);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Shows an outer ring with the value AUTO is actually using (pass < 0 to hide). */
    void setEffective (float v01);

    std::function<juce::String (double)> formatValue = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    juce::Slider slider;

private:
    juce::String name, subtitle;
    juce::Colour colour;
    bool big;
    float effective = -1.0f;
};

//==============================================================================
class Visualizer : public juce::Component
{
public:
    explicit Visualizer (ClarityStripAudioProcessor&);

    void update();  // pull new audio + repaint (called from the editor timer)
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, numPoints = 240;
    enum class Mode { Spectrum, Scope, Waveform };

    void computeSpectrum (const float* src, std::array<float, numPoints>& out);
    void drawGrid (juce::Graphics&, juce::Rectangle<float>);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawScope (juce::Graphics&, juce::Rectangle<float>);
    void drawWaveform (juce::Graphics&, juce::Rectangle<float>);
    float xForFreq (float f, juce::Rectangle<float> r) const;
    float yForDb (float db, juce::Rectangle<float> r) const;

    ClarityStripAudioProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize> inBuf {}, outBuf {};
    std::array<float, numPoints> inSpec {}, outSpec {}, peakSpec {};
    std::array<float, 4096> scopeIn {}, scopeOut {};
    std::vector<float> wfMax, wfMin;   // scrolling waveform history (output)
    std::vector<float> wfInMax;
    Mode mode = Mode::Spectrum;
};

//==============================================================================
class Meters : public juce::Component
{
public:
    void setLevels (float inDb, float outDb, float grDb, float nrDb);
    void paint (juce::Graphics&) override;

private:
    void drawBar (juce::Graphics&, juce::Rectangle<float>, const juce::String& label, float norm,
                  juce::Colour, bool fromTop, const juce::String& value, float holdNorm = -1.0f);
    float in = -100, out = -100, gr = 0, nr = 0, inHold = -100, outHold = -100;
    int inHoldAge = 0, outHoldAge = 0;
};

//==============================================================================
class ClarityStripAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClarityStripAudioProcessorEditor (ClarityStripAudioProcessor&);
    ~ClarityStripAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyLearned (const float* values);

    ClarityStripAudioProcessor& proc;
    ClarityLookAndFeel lnf;

    Visualizer visualizer;
    Meters meters;

    std::array<std::unique_ptr<MacroKnob>, clarity::NumMacros> knobs;
    MacroKnob autoKnob   { "AUTO CLEAN", "Analyze + adapt", Palette::autoCol, true };
    MacroKnob outputKnob { "OUTPUT", "Gain", Palette::text };
    juce::TextButton analyzeButton { "ANALYZE  5s" };
    juce::String status { "Play audio, then press ANALYZE, or turn AUTO CLEAN up to adapt automatically." };

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::array<std::unique_ptr<Attachment>, clarity::NumMacros> attachments;
    std::unique_ptr<Attachment> autoAttachment, outputAttachment;

    juce::Rectangle<int> knobPanel, autoPanel, outPanel, headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClarityStripAudioProcessorEditor)
};
