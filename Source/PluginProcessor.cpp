#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static const StringArray typeNames { "HighPass", "LowShelf", "Bell", "Notch", "LowPass" };

AudioProcessorValueTreeState::ParameterLayout NeneEQAudioProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "Bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "SoloMode", 1 }, "Solo Mode", false));
    layout.add (std::make_unique<AudioParameterInt>  (ParameterID { "ActiveSoloBand", 1 }, "Active Solo Band", 0, 4, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "OutputGain", 1 }, "Output Gain",
                                                       NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f));

    const float defaultFreqs[numBands] = { 100.0f, 250.0f, 1000.0f, 4000.0f, 10000.0f };

    for (int i = 0; i < numBands; ++i)
    {
        const String n = "Band" + String (i + 1);
        const String d = "Band " + String (i + 1) + " ";

        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { n + "_Enable", 1 }, d + "Enable", true));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { n + "_Type", 1 }, d + "Type", typeNames, 2));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { n + "_Freq", 1 }, d + "Freq",
                                                            NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.3f), defaultFreqs[i]));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { n + "_Gain", 1 }, d + "Gain",
                                                            NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { n + "_Q", 1 }, d + "Q",
                                                            NormalisableRange<float> (0.1f, 40.0f, 0.05f, 0.4f), 1.0f));
    }
    return layout;
}

NeneEQAudioProcessor::NeneEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", createLayout())
{
    pBypass   = apvts.getRawParameterValue ("Bypass");
    pSoloMode = apvts.getRawParameterValue ("SoloMode");
    pSoloBand = apvts.getRawParameterValue ("ActiveSoloBand");
    pOutGain  = apvts.getRawParameterValue ("OutputGain");

    for (int i = 0; i < numBands; ++i)
    {
        const String n = "Band" + String (i + 1);
        bands[(size_t) i].enable = apvts.getRawParameterValue (n + "_Enable");
        bands[(size_t) i].type   = apvts.getRawParameterValue (n + "_Type");
        bands[(size_t) i].freq   = apvts.getRawParameterValue (n + "_Freq");
        bands[(size_t) i].gain   = apvts.getRawParameterValue (n + "_Gain");
        bands[(size_t) i].q      = apvts.getRawParameterValue (n + "_Q");
    }
}

bool NeneEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void NeneEQAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;

    for (auto& ch : filters)
        for (auto& f : ch)
            f.reset();
    for (auto& f : soloFilter)
        f.reset();

    for (auto& c : cache)
        c = BandCache();
    soloCache = BandCache();

    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTargetValue (Decibels::decibelsToGain (pOutGain->load()));
}

dsp::IIR::Coefficients<float>::Ptr NeneEQAudioProcessor::makeBandCoeffs (int type, double sampleRate,
                                                                          float freq, float q, float gainDb)
{
    using Coeffs = dsp::IIR::Coefficients<float>;
    freq = jlimit (20.0f, (float) (sampleRate * 0.49), freq);
    q = jmax (0.1f, q);
    const float g = Decibels::decibelsToGain (gainDb);

    switch (type)
    {
        case 0:  return Coeffs::makeHighPass (sampleRate, freq, q);
        case 1:  return Coeffs::makeLowShelf (sampleRate, freq, q, g);
        case 3:  return Coeffs::makeNotch (sampleRate, freq, q);
        case 4:  return Coeffs::makeLowPass (sampleRate, freq, q);
        case 2:
        default: return Coeffs::makePeakFilter (sampleRate, freq, q, g);
    }
}

void NeneEQAudioProcessor::updateFilters()
{
    using Coeffs = dsp::IIR::Coefficients<float>;
    const float maxFreq = (float) (sr * 0.49);

    for (int b = 0; b < numBands; ++b)
    {
        auto& p = bands[(size_t) b];
        auto& c = cache[(size_t) b];

        const float type = p.type->load();
        const float freq = jlimit (20.0f, maxFreq, p.freq->load());
        const float gain = p.gain->load();
        const float q    = jmax (0.1f, p.q->load());

        if (type == c.type && freq == c.freq && gain == c.gain && q == c.q)
            continue;

        c.type = type; c.freq = freq; c.gain = gain; c.q = q;
        auto coeffs = makeBandCoeffs ((int) type, sr, freq, q, gain);

        for (int ch = 0; ch < 2; ++ch)
            filters[ch][b].coefficients = coeffs;
    }

    const int sb = jlimit (0, numBands - 1, (int) pSoloBand->load());
    const float sf = jlimit (20.0f, maxFreq, bands[(size_t) sb].freq->load());
    const float sq = jmax (0.1f, bands[(size_t) sb].q->load());

    if (sf != soloCache.freq || sq != soloCache.q)
    {
        soloCache.freq = sf; soloCache.q = sq;
        auto coeffs = Coeffs::makeBandPass (sr, sf, sq);
        for (int ch = 0; ch < 2; ++ch)
            soloFilter[ch].coefficients = coeffs;
    }
}

void NeneEQAudioProcessor::pushAnalyser (const AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chs = buffer.getNumChannels();
    if (chs == 0 || n == 0)
        return;

    int w = scopeWrite.load();
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int ch = 0; ch < chs; ++ch)
            s += buffer.getSample (ch, i);
        scopeBuffer[(size_t) w] = s / (float) chs;
        w = (w + 1) % scopeSize;
    }
    scopeWrite.store (w);

    float peak = 0.0f;
    for (int ch = 0; ch < chs; ++ch)
        peak = jmax (peak, buffer.getMagnitude (ch, 0, n));
    outputLevel.store (jmax (outputLevel.load(), peak));
}

void NeneEQAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int numCh = jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (pBypass->load() > 0.5f)
    {
        pushAnalyser (buffer);
        return;
    }

    updateFilters();

    const bool solo = pSoloMode->load() > 0.5f;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        if (solo)
        {
            for (int i = 0; i < numSamples; ++i)
                data[i] = soloFilter[ch].processSample (data[i]);
        }
        else
        {
            for (int b = 0; b < numBands; ++b)
            {
                if (bands[(size_t) b].enable->load() < 0.5f)
                    continue;
                for (int i = 0; i < numSamples; ++i)
                    data[i] = filters[ch][b].processSample (data[i]);
            }
        }
    }

    outGain.setTargetValue (Decibels::decibelsToGain (pOutGain->load()));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    pushAnalyser (buffer);
}

void NeneEQAudioProcessor::setParamValue (const String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

StringArray NeneEQAudioProcessor::getPresetNames()
{
    return { "Default", "Vocal Clarity", "Warm Low End", "Bright Air", "Mud Cleanup", "Telephone" };
}

void NeneEQAudioProcessor::applyPreset (int index)
{
    struct B { int type; float freq, gain, q; };
    static const B presets[6][5] =
    {
        // Default
        { {2, 100.0f, 0.0f, 1.0f}, {2, 250.0f, 0.0f, 1.0f}, {2, 1000.0f, 0.0f, 1.0f}, {2, 4000.0f, 0.0f, 1.0f}, {2, 10000.0f, 0.0f, 1.0f} },
        // Vocal Clarity
        { {0, 80.0f, 0.0f, 0.7f}, {2, 250.0f, -2.5f, 1.2f}, {2, 3000.0f, 2.5f, 1.0f}, {2, 5000.0f, 1.5f, 1.0f}, {2, 12000.0f, 2.0f, 0.7f} },
        // Warm Low End
        { {1, 100.0f, 3.0f, 0.7f}, {2, 250.0f, 1.5f, 1.0f}, {2, 1000.0f, 0.0f, 1.0f}, {2, 4000.0f, -1.0f, 1.0f}, {4, 16000.0f, 0.0f, 0.7f} },
        // Bright Air
        { {0, 30.0f, 0.0f, 0.7f}, {2, 250.0f, -1.0f, 1.0f}, {2, 2500.0f, 1.5f, 0.8f}, {2, 6000.0f, 2.0f, 1.0f}, {2, 12000.0f, 3.0f, 0.7f} },
        // Mud Cleanup
        { {0, 40.0f, 0.0f, 0.7f}, {2, 200.0f, -3.0f, 1.5f}, {2, 400.0f, -2.0f, 2.0f}, {2, 3500.0f, 1.0f, 1.0f}, {2, 10000.0f, 1.5f, 0.7f} },
        // Telephone
        { {0, 400.0f, 0.0f, 0.9f}, {2, 1000.0f, 4.0f, 1.0f}, {2, 2500.0f, 3.0f, 1.0f}, {4, 3400.0f, 0.0f, 0.9f}, {2, 10000.0f, 0.0f, 1.0f} }
    };

    index = jlimit (0, 5, index);

    for (int b = 0; b < numBands; ++b)
    {
        const String n = "Band" + String (b + 1);
        const auto& p = presets[index][b];
        setParamValue (n + "_Enable", 1.0f);
        setParamValue (n + "_Type", (float) p.type);
        setParamValue (n + "_Freq", p.freq);
        setParamValue (n + "_Gain", p.gain);
        setParamValue (n + "_Q", p.q);
    }
    setParamValue ("OutputGain", 0.0f);
}

void NeneEQAudioProcessor::switchSlot (int newSlot)
{
    newSlot = jlimit (0, 1, newSlot);
    if (newSlot == currentSlot)
        return;

    slots[currentSlot] = apvts.copyState();
    currentSlot = newSlot;
    if (slots[newSlot].isValid())
        apvts.replaceState (slots[newSlot].createCopy());
}

AudioProcessorEditor* NeneEQAudioProcessor::createEditor()
{
    return new NeneEQAudioProcessorEditor (*this);
}

void NeneEQAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NeneEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeneEQAudioProcessor();
}
