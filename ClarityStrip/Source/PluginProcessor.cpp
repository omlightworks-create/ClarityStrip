#include "PluginProcessor.h"
#include "PluginEditor.h"

ClarityStripAudioProcessor::ClarityStripAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ClarityStrip", createLayout())
{
    for (int i = 0; i < clarity::NumMacros; ++i)
        macroParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::macros[(size_t) i]);
    autoParam   = apvts.getRawParameterValue (ParamIDs::autoAmt);
    outputParam = apvts.getRawParameterValue (ParamIDs::output);
}

juce::AudioProcessorValueTreeState::ParameterLayout ClarityStripAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
                                                      AudioParameterFloatAttributes().withLabel ("%"));
    };

    layout.add (pct (ParamIDs::dynamics,   "Dynamics",      0.0f));
    layout.add (pct (ParamIDs::eq,         "EQ",            0.0f));
    layout.add (pct (ParamIDs::compressor, "Compressor",    0.0f));
    layout.add (pct (ParamIDs::limiter,    "Limiter",       0.0f));
    layout.add (pct (ParamIDs::enhancer,   "Enhancer",      0.0f));
    layout.add (pct (ParamIDs::denoise,    "Noise Remover", 0.0f));
    layout.add (pct (ParamIDs::autoAmt,    "Auto Clean",    0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, 1 }, "Output",
                                                       NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

bool ClarityStripAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void ClarityStripAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = std::max (1, getTotalNumOutputChannels());
    engine.prepare (sampleRate, std::max (32, samplesPerBlock), numCh);
    monoTmp.assign ((size_t) std::max (32, samplesPerBlock), 0.0f);
    setLatencySamples (engine.getLatency()); // Resolve/Fairlight compensates this delay
}

void ClarityStripAudioProcessor::pushMono (ScopeFifo& fifo, const juce::AudioBuffer<float>& buf, int numCh, int n)
{
    int done = 0;
    while (done < n)
    {
        const int k = std::min ((int) monoTmp.size(), n - done);
        for (int i = 0; i < k; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) s += buf.getSample (ch, done + i);
            monoTmp[(size_t) i] = s / (float) numCh;
        }
        fifo.push (monoTmp.data(), k);
        done += k;
    }
}

void ClarityStripAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);

    const int numCh = std::min (numIn, buffer.getNumChannels());
    if (numCh == 0 || n == 0) return;

    pushMono (inputScope, buffer, numCh, n);

    clarity::Params p;
    for (int i = 0; i < clarity::NumMacros; ++i) p.macro[(size_t) i] = macroParams[(size_t) i]->load() * 0.01f;
    p.autoAmount = autoParam->load() * 0.01f;
    p.outputDb   = outputParam->load();

    engine.process (buffer.getArrayOfWritePointers(), numCh, n, p);

    pushMono (outputScope, buffer, numCh, n);
}

void ClarityStripAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ClarityStripAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ClarityStripAudioProcessor::createEditor()
{
    return new ClarityStripAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClarityStripAudioProcessor();
}
