#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/ClarityDSP.h"

//==============================================================================
// Lock-free ring of the most recent samples, read by the visualizer.
struct ScopeFifo
{
    static constexpr int kSize = 8192; // power of two
    std::array<std::atomic<float>, kSize> data {};
    std::atomic<int> writePos { 0 };

    void push (const float* x, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            data[(size_t) w].store (x[i], std::memory_order_relaxed);
            w = (w + 1) & (kSize - 1);
        }
        writePos.store (w, std::memory_order_release);
    }

    void readLatest (float* dst, int n) const noexcept
    {
        const int w = writePos.load (std::memory_order_acquire);
        const int start = (w - n + kSize) & (kSize - 1);
        for (int i = 0; i < n; ++i)
            dst[i] = data[(size_t) ((start + i) & (kSize - 1))].load (std::memory_order_relaxed);
    }
};

//==============================================================================
namespace ParamIDs
{
    inline constexpr const char* dynamics   = "dynamics";
    inline constexpr const char* eq         = "eq";
    inline constexpr const char* compressor = "compressor";
    inline constexpr const char* limiter    = "limiter";
    inline constexpr const char* enhancer   = "enhancer";
    inline constexpr const char* denoise    = "denoise";
    inline constexpr const char* autoAmt    = "auto";
    inline constexpr const char* output     = "output";

    // same order as clarity::Macro
    inline const std::array<const char*, clarity::NumMacros> macros { dynamics, eq, compressor, limiter, enhancer, denoise };
}

//==============================================================================
class ClarityStripAudioProcessor : public juce::AudioProcessor
{
public:
    ClarityStripAudioProcessor();
    ~ClarityStripAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    clarity::Engine engine;
    ScopeFifo inputScope, outputScope;

private:
    std::array<std::atomic<float>*, clarity::NumMacros> macroParams {};
    std::atomic<float>* autoParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::vector<float> monoTmp;

    void pushMono (ScopeFifo&, const juce::AudioBuffer<float>&, int numCh, int n);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClarityStripAudioProcessor)
};
