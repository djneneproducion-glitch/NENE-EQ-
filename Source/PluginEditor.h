#pragma once
#include "PluginProcessor.h"
#include <array>
#include <memory>
#include <vector>

//==============================================================================
class NeneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeneLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};

//==============================================================================
class EQDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EQDisplay (NeneEQAudioProcessor& p);
    ~EQDisplay() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setRangeDb (float r) { rangeDb = r; repaint(); }
    void setShowSpectrum (bool b) { showSpectrum = b; repaint(); }
    int getSelectedBand() const { return selected; }

    static juce::Colour bandColour (int i);

private:
    struct Band { bool enabled; int type; float freq, gain, q; };

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;

    void timerCallback() override;
    Band getBand (int i) const;
    static bool hasGain (int type) { return type == 1 || type == 2; }

    juce::Rectangle<float> plotArea() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    juce::Point<float> nodePos (int i) const;
    int hitNode (juce::Point<float> p) const;
    void setParamRaw (const juce::String& id, float v);

    void drawGrid (juce::Graphics&) const;
    void drawSpectrum (juce::Graphics&) const;
    void drawCurves (juce::Graphics&) const;
    void drawNodes (juce::Graphics&) const;
    void drawMeter (juce::Graphics&) const;

    NeneEQAudioProcessor& proc;
    float rangeDb = 12.0f;
    bool showSpectrum = true;
    int selected = 0;
    bool dragging = false;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> spec {};
    float meter = 0.0f;
};

//==============================================================================
class BandPanel : public juce::Component, private juce::Timer
{
public:
    BandPanel (NeneEQAudioProcessor& p, int index);
    ~BandPanel() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    NeneEQAudioProcessor& proc;
    int idx;
    juce::String prefix;
    juce::Colour col;

    juce::TextButton enableBtn;
    juce::TextButton typeBtn[5];
    juce::Slider freq, gain, q;
    std::array<juce::Rectangle<int>, 3> cols;

    std::unique_ptr<ButtonAtt> enableAtt;
    std::unique_ptr<SliderAtt> freqAtt, gainAtt, qAtt;
};

//==============================================================================
class NeneEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeneEQAudioProcessorEditor (NeneEQAudioProcessor&);
    ~NeneEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    NeneLookAndFeel laf;
    NeneEQAudioProcessor& proc;

    EQDisplay display;
    std::array<std::unique_ptr<BandPanel>, 5> panels;

    juce::ComboBox presetBox, rangeBox, analyzerBox;
    juce::TextButton aBtn, bBtn, prevBtn, nextBtn, undoBtn, powerBtn, stereoBtn, soloBtn;
    juce::Slider outKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outAtt;

    juce::Rectangle<int> outRect, outLabelRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeneEQAudioProcessorEditor)
};
