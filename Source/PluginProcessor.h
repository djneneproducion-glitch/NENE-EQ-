#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

class NeneEQAudioProcessor : public juce::AudioProcessor
{
public:
    NeneEQAudioProcessor();
    ~NeneEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NENE PRODUCE EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- utilidades usadas por la interfaz ----
    static juce::dsp::IIR::Coefficients<float>::Ptr makeBandCoeffs (int type, double sampleRate,
                                                                     float freq, float q, float gainDb);
    double getCurrentSampleRate() const { return sr; }
    void setParamValue (const juce::String& id, float value);
    void applyPreset (int index);
    static juce::StringArray getPresetNames();
    void switchSlot (int newSlot);
    int getCurrentSlot() const { return currentSlot; }

    static constexpr int scopeSize = 2048;
    std::array<float, scopeSize> scopeBuffer {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<float> outputLevel { 0.0f };

    juce::UndoManager undoManager;          // debe ir antes de apvts
    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int numBands = 5;
    using Filter = juce::dsp::IIR::Filter<float>;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateFilters();
    void pushAnalyser (const juce::AudioBuffer<float>& buffer);

    struct BandPtrs
    {
        std::atomic<float>* enable = nullptr;
        std::atomic<float>* type   = nullptr;
        std::atomic<float>* freq   = nullptr;
        std::atomic<float>* gain   = nullptr;
        std::atomic<float>* q      = nullptr;
    };

    struct BandCache
    {
        float type = -1.0f, freq = -1.0f, gain = -999.0f, q = -1.0f;
    };

    std::array<BandPtrs, numBands> bands;
    std::array<BandCache, numBands> cache;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pSoloMode = nullptr;
    std::atomic<float>* pSoloBand = nullptr;
    std::atomic<float>* pOutGain = nullptr;

    Filter filters[2][numBands];
    Filter soloFilter[2];
    BandCache soloCache;

    juce::SmoothedValue<float> outGain;
    double sr = 44100.0;

    juce::ValueTree slots[2];
    int currentSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeneEQAudioProcessor)
};
