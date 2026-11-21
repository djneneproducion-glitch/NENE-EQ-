#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

using namespace juce;

//==============================================================================
// LookAndFeel
//==============================================================================
NeneLookAndFeel::NeneLookAndFeel()
{
    setColour (TextButton::buttonColourId, Colour (0xff1d1530));
    setColour (TextButton::buttonOnColourId, Colour (0xffd61fa0));
    setColour (TextButton::textColourOffId, Colours::white.withAlpha (0.75f));
    setColour (TextButton::textColourOnId, Colours::white);
    setColour (ComboBox::backgroundColourId, Colour (0xff0d0916));
    setColour (ComboBox::textColourId, Colours::white);
    setColour (ComboBox::outlineColourId, Colour (0x00000000));
    setColour (ComboBox::arrowColourId, Colours::white);
    setColour (PopupMenu::backgroundColourId, Colour (0xff140d22));
    setColour (PopupMenu::textColourId, Colours::white);
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xffd61fa0));
}

void NeneLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                        float pos, float startAngle, float endAngle, Slider& s)
{
    auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float r = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto col = s.findColour (Slider::rotarySliderFillColourId);

    g.setColour (Colour (0xff15101f));
    g.fillEllipse (c.x - r * 0.78f, c.y - r * 0.78f, r * 1.56f, r * 1.56f);

    Path track;
    track.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (Colour (0x30ffffff));
    g.strokePath (track, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    Path val;
    val.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, startAngle, angle, true);
    g.setColour (col);
    g.strokePath (val, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    Path pointer;
    pointer.addRoundedRectangle (-1.5f, -r * 0.62f, 3.0f, r * 0.38f, 1.5f);
    g.setColour (Colours::white);
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (c.x, c.y));
}

//==============================================================================
// EQDisplay
//==============================================================================
Colour EQDisplay::bandColour (int i)
{
    static const uint32 c[5] = { 0xffb44bff, 0xffff3b5c, 0xff3b82ff, 0xff22d36b, 0xfff59e0b };
    return Colour (c[jlimit (0, 4, i)]);
}

EQDisplay::EQDisplay (NeneEQAudioProcessor& p) : proc (p)
{
    spec.fill (-120.0f);
    startTimerHz (30);
}

EQDisplay::Band EQDisplay::getBand (int i) const
{
    const String n = "Band" + String (i + 1);
    auto& v = proc.apvts;
    Band b;
    b.enabled = v.getRawParameterValue (n + "_Enable")->load() > 0.5f;
    b.type    = (int) v.getRawParameterValue (n + "_Type")->load();
    b.freq    = v.getRawParameterValue (n + "_Freq")->load();
    b.gain    = v.getRawParameterValue (n + "_Gain")->load();
    b.q       = v.getRawParameterValue (n + "_Q")->load();
    return b;
}

Rectangle<float> EQDisplay::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (26.0f).withTrimmedRight (30.0f);
}

float EQDisplay::freqToX (float f) const
{
    auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}

float EQDisplay::xToFreq (float x) const
{
    auto a = plotArea();
    const float n = jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth());
    return 20.0f * std::pow (1000.0f, n);
}

float EQDisplay::dbToY (float db) const
{
    auto a = plotArea();
    return a.getCentreY() - (db / (rangeDb * 1.15f)) * (a.getHeight() * 0.5f);
}

float EQDisplay::yToDb (float y) const
{
    auto a = plotArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * (rangeDb * 1.15f);
}

Point<float> EQDisplay::nodePos (int i) const
{
    auto b = getBand (i);
    return { freqToX (b.freq), dbToY (hasGain (b.type) ? b.gain : 0.0f) };
}

int EQDisplay::hitNode (Point<float> p) const
{
    int best = -1;
    float bestD = 14.0f;
    for (int i = 0; i < 5; ++i)
    {
        const float d = p.getDistanceFrom (nodePos (i));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void EQDisplay::setParamRaw (const String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void EQDisplay::timerCallback()
{
    const int w = proc.scopeWrite.load();
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = proc.scopeBuffer[(size_t) ((w + i) % fftSize)];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (size_t k = 0; k < spec.size(); ++k)
    {
        const float db = Decibels::gainToDecibels (fftData[k] / ((float) fftSize * 0.25f), -120.0f);
        spec[k] = spec[k] * 0.7f + db * 0.3f;
    }

    const float l = proc.outputLevel.exchange (0.0f);
    meter = jmax (l, meter * 0.88f);

    repaint();
}

//------------------------------------------------------------------------------
void EQDisplay::drawGrid (Graphics& g) const
{
    const auto area = plotArea();

    static const float fl[] = { 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
    for (float f : fl)
    {
        const bool major = (f == 100.0f || f == 1000.0f || f == 10000.0f);
        g.setColour (Colours::white.withAlpha (major ? 0.10f : 0.04f));
        g.drawVerticalLine ((int) freqToX (f), area.getY(), area.getBottom());
    }

    const float step = rangeDb <= 3.5f ? 1.0f : (rangeDb <= 6.5f ? 2.0f : (rangeDb <= 12.5f ? 3.0f : 6.0f));
    const int n = (int) std::floor (rangeDb / step + 0.001f);

    g.setFont (12.0f);
    for (int k = -n; k <= n; ++k)
    {
        const float d = (float) k * step;
        const float y = dbToY (d);
        g.setColour (Colours::white.withAlpha (k == 0 ? 0.55f : 0.05f));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());

        String txt = (k > 0 ? "+" : "") + String ((int) d);
        g.setColour (Colours::white.withAlpha (0.8f));
        g.drawText (txt, (int) area.getX() + 8, (int) y - 8, 40, 16, Justification::left);
    }
}

void EQDisplay::drawSpectrum (Graphics& g) const
{
    if (! showSpectrum)
        return;

    const auto area = plotArea();
    const float sr = (float) proc.getCurrentSampleRate();
    const float h = area.getHeight() * 0.55f;

    Path sp;
    bool started = false;
    float lastX = area.getX();

    for (int k = 1; k < fftSize / 2; ++k)
    {
        const float f = (float) k * sr / (float) fftSize;
        if (f < 20.0f || f > 20000.0f)
            continue;

        const float x = freqToX (f);
        const float t = jlimit (0.0f, 1.0f, (spec[(size_t) k] + 100.0f) / 80.0f);
        const float y = area.getBottom() - t * h;

        if (! started)
        {
            sp.startNewSubPath (x, area.getBottom());
            started = true;
        }
        sp.lineTo (x, y);
        lastX = x;
    }

    if (started)
    {
        sp.lineTo (lastX, area.getBottom());
        sp.closeSubPath();
        g.setGradientFill (ColourGradient (Colour (0xaa7b2ff7), 0.0f, area.getBottom() - h,
                                           Colour (0x107b2ff7), 0.0f, area.getBottom(), false));
        g.fillPath (sp);
    }
}

void EQDisplay::drawCurves (Graphics& g) const
{
    const auto area = plotArea();
    const double sr = proc.getCurrentSampleRate();
    constexpr int N = 360;

    dsp::IIR::Coefficients<float>::Ptr coeffs[5];
    Band bs[5];
    for (int b = 0; b < 5; ++b)
    {
        bs[b] = getBand (b);
        coeffs[b] = NeneEQAudioProcessor::makeBandCoeffs (bs[b].type, sr, bs[b].freq, bs[b].q, bs[b].gain);
    }

    float bandDb[5][N];
    float total[N];
    float xs[N];

    for (int i = 0; i < N; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / (float) (N - 1));
        xs[i] = freqToX (f);
        total[i] = 0.0f;
        for (int b = 0; b < 5; ++b)
        {
            float db = 0.0f;
            if (bs[b].enabled)
                db = Decibels::gainToDecibels ((float) coeffs[b]->getMagnitudeForFrequency ((double) f, sr), -100.0f);
            bandDb[b][i] = db;
            total[i] += db;
        }
    }

    const float zeroY = dbToY (0.0f);
    auto clampY = [&] (float y) { return jlimit (area.getY() - 2.0f, area.getBottom() + 2.0f, y); };

    // relleno por banda
    for (int b = 0; b < 5; ++b)
    {
        if (! bs[b].enabled)
            continue;
        Path p;
        p.startNewSubPath (xs[0], zeroY);
        for (int i = 0; i < N; ++i)
            p.lineTo (xs[i], clampY (dbToY (bandDb[b][i])));
        p.lineTo (xs[N - 1], zeroY);
        p.closeSubPath();
        g.setColour (bandColour (b).withAlpha (0.13f));
        g.fillPath (p);
    }

    // curva total
    Path line, fill;
    fill.startNewSubPath (xs[0], zeroY);
    for (int i = 0; i < N; ++i)
    {
        const float y = clampY (dbToY (total[i]));
        if (i == 0) line.startNewSubPath (xs[i], y); else line.lineTo (xs[i], y);
        fill.lineTo (xs[i], y);
    }
    fill.lineTo (xs[N - 1], zeroY);
    fill.closeSubPath();

    g.setColour (Colour (0x30c026d3));
    g.fillPath (fill);
    g.setColour (Colour (0x40ff2fb0));
    g.strokePath (line, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xffff3fb8));
    g.strokePath (line, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
}

void EQDisplay::drawNodes (Graphics& g) const
{
    for (int i = 0; i < 5; ++i)
    {
        const auto b = getBand (i);
        const auto p = nodePos (i);
        const float r = (i == selected) ? 10.0f : 8.0f;
        const Colour c = b.enabled ? bandColour (i) : Colours::grey;

        if (i == selected)
        {
            g.setColour (c.withAlpha (0.3f));
            g.fillEllipse (p.x - r - 5.0f, p.y - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f);
        }
        g.setColour (c);
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        g.setColour (Colour (0xff0a0714));
        g.fillEllipse (p.x - r * 0.55f, p.y - r * 0.55f, r * 1.1f, r * 1.1f);
        g.setColour (Colours::white);
        g.fillEllipse (p.x - r * 0.25f, p.y - r * 0.25f, r * 0.5f, r * 0.5f);
    }
}

void EQDisplay::drawMeter (Graphics& g) const
{
    const auto area = plotArea();
    Rectangle<float> r (area.getRight() + 8.0f, area.getY() + 8.0f, 12.0f, area.getHeight() - 16.0f);

    g.setColour (Colour (0xff0d0916));
    g.fillRoundedRectangle (r, 3.0f);

    const float db = Decibels::gainToDecibels (meter, -100.0f);
    const float t = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    ColourGradient grad (Colour (0xffff2d2d), r.getX(), r.getY(), Colour (0xff22d36b), r.getX(), r.getBottom(), false);
    grad.addColour (0.2, Colour (0xfffacc15));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r.withTop (r.getBottom() - t * r.getHeight()), 3.0f);
}

void EQDisplay::paint (Graphics& g)
{
    const auto area = plotArea();

    g.setColour (Colour (0xff070510));
    g.fillRoundedRectangle (area, 10.0f);

    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area.toNearestInt());
        drawGrid (g);
        drawSpectrum (g);
        drawCurves (g);
    }

    drawNodes (g);
    drawMeter (g);

    // etiquetas de frecuencia
    static const float lf[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    static const char* lt[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    g.setFont (13.0f);
    g.setColour (Colours::white.withAlpha (0.85f));
    for (int i = 0; i < 10; ++i)
    {
        float x = freqToX (lf[i]);
        if (i == 0) x += 10.0f;
        if (i == 9) x -= 10.0f;
        g.drawText (lt[i], (int) (x - 20.0f), (int) area.getBottom() + 5, 40, 16, Justification::centred);
    }
}

//------------------------------------------------------------------------------
void EQDisplay::mouseDown (const MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h >= 0)
    {
        selected = h;
        dragging = true;
        const String n = "Band" + String (h + 1);
        if (auto* p = proc.apvts.getParameter (n + "_Freq")) p->beginChangeGesture();
        if (auto* p = proc.apvts.getParameter (n + "_Gain")) p->beginChangeGesture();
    }
    else
    {
        dragging = false;
    }
    repaint();
}

void EQDisplay::mouseDrag (const MouseEvent& e)
{
    if (! dragging || selected < 0)
        return;

    const auto b = getBand (selected);
    const String n = "Band" + String (selected + 1);

    setParamRaw (n + "_Freq", xToFreq (e.position.x));
    if (hasGain (b.type))
        setParamRaw (n + "_Gain", jlimit (-24.0f, 24.0f, yToDb (e.position.y)));
}

void EQDisplay::mouseUp (const MouseEvent&)
{
    if (dragging && selected >= 0)
    {
        const String n = "Band" + String (selected + 1);
        if (auto* p = proc.apvts.getParameter (n + "_Freq")) p->endChangeGesture();
        if (auto* p = proc.apvts.getParameter (n + "_Gain")) p->endChangeGesture();
    }
    dragging = false;
}

void EQDisplay::mouseDoubleClick (const MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h < 0)
        return;

    const auto b = getBand (h);
    proc.setParamValue ("Band" + String (h + 1) + "_Enable", b.enabled ? 0.0f : 1.0f);
}

void EQDisplay::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    int h = hitNode (e.position);
    if (h < 0)
        h = selected;
    if (h < 0)
        return;

    const auto b = getBand (h);
    const float q = jlimit (0.1f, 40.0f, b.q * std::exp (w.deltaY * 1.5f));
    proc.setParamValue ("Band" + String (h + 1) + "_Q", q);
}

//==============================================================================
// BandPanel
//==============================================================================
BandPanel::BandPanel (NeneEQAudioProcessor& p, int index)
    : proc (p), idx (index), prefix ("Band" + String (index + 1)), col (EQDisplay::bandColour (index))
{
    enableBtn.setButtonText (String (idx + 1));
    enableBtn.setColour (TextButton::buttonColourId, Colour (0xff2a2238));
    enableBtn.setColour (TextButton::buttonOnColourId, col);
    addAndMakeVisible (enableBtn);
    enableAtt = std::make_unique<ButtonAtt> (proc.apvts, prefix + "_Enable", enableBtn);

    static const char* names[5] = { "HP", "LS", "Bell", "Ntch", "LP" };
    for (int t = 0; t < 5; ++t)
    {
        typeBtn[t].setButtonText (names[t]);
        typeBtn[t].setClickingTogglesState (true);
        typeBtn[t].setRadioGroupId (1000 + idx);
        typeBtn[t].setColour (TextButton::buttonOnColourId, col);
        typeBtn[t].onClick = [this, t]
        {
            proc.setParamValue (prefix + "_Type", (float) t);
        };
        addAndMakeVisible (typeBtn[t]);
    }

    auto setup = [this] (Slider& s, Colour c)
    {
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::TextBoxBelow, false, 66, 16);
        s.setTextBoxIsEditable (false);
        s.setColour (Slider::rotarySliderFillColourId, c);
        s.setColour (Slider::textBoxTextColourId, Colours::white);
        s.setColour (Slider::textBoxOutlineColourId, Colour (0x00000000));
        s.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        addAndMakeVisible (s);
    };
    setup (freq, col);
    setup (gain, Colour (0xff22d3ee));
    setup (q, Colour (0xfffacc15));

    freqAtt = std::make_unique<SliderAtt> (proc.apvts, prefix + "_Freq", freq);
    gainAtt = std::make_unique<SliderAtt> (proc.apvts, prefix + "_Gain", gain);
    qAtt    = std::make_unique<SliderAtt> (proc.apvts, prefix + "_Q", q);

    freq.textFromValueFunction = [] (double v)
    {
        return v >= 1000.0 ? String (v / 1000.0, 2) + " kHz" : String (v, 1) + " Hz";
    };
    gain.textFromValueFunction = [] (double v)
    {
        return String (v >= 0.0 ? "+" : "") + String (v, 2) + " dB";
    };
    q.textFromValueFunction = [] (double v) { return String (v, 2); };
    freq.updateText();
    gain.updateText();
    q.updateText();

    startTimerHz (15);
    timerCallback();
}

void BandPanel::timerCallback()
{
    const int t = jlimit (0, 4, (int) proc.apvts.getRawParameterValue (prefix + "_Type")->load());
    typeBtn[t].setToggleState (true, dontSendNotification);

    const bool hasGain = (t == 1 || t == 2);
    gain.setEnabled (hasGain);
    gain.setAlpha (hasGain ? 1.0f : 0.35f);

    const bool en = proc.apvts.getRawParameterValue (prefix + "_Enable")->load() > 0.5f;
    freq.setAlpha (en ? 1.0f : 0.5f);
    q.setAlpha (en ? 1.0f : 0.5f);
}

void BandPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colour (0xff0d0916));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col.withAlpha (0.35f));
    g.drawRoundedRectangle (r, 10.0f, 1.2f);

    g.setFont (12.0f);
    g.setColour (Colours::white.withAlpha (0.7f));
    static const char* labels[3] = { "FREQ", "GAIN", "Q" };
    for (int i = 0; i < 3; ++i)
        g.drawText (labels[i], cols[(size_t) i].withHeight (16), Justification::centred);
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (24);
    enableBtn.setBounds (top.removeFromLeft (24));
    top.removeFromLeft (6);
    const int bw = top.getWidth() / 5;
    for (int t = 0; t < 5; ++t)
        typeBtn[t].setBounds (top.removeFromLeft (bw).reduced (1, 0));

    r.removeFromTop (8);
    const int cw = r.getWidth() / 3;

    auto place = [] (Slider& s, Rectangle<int> c)
    {
        c.removeFromTop (16);
        s.setBounds (c);
    };

    cols[0] = r.removeFromLeft (cw);
    cols[1] = r.removeFromLeft (cw);
    cols[2] = r;
    place (freq, cols[0]);
    place (gain, cols[1]);
    place (q, cols[2]);
}

//==============================================================================
// Editor principal
//==============================================================================
NeneEQAudioProcessorEditor::NeneEQAudioProcessorEditor (NeneEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (display);
    for (int i = 0; i < 5; ++i)
    {
        panels[(size_t) i] = std::make_unique<BandPanel> (proc, i);
        addAndMakeVisible (*panels[(size_t) i]);
    }

    // Presets
    presetBox.addItemList (NeneEQAudioProcessor::getPresetNames(), 1);
    presetBox.setSelectedItemIndex (0, dontSendNotification);
    presetBox.onChange = [this] { proc.applyPreset (presetBox.getSelectedItemIndex()); };
    addAndMakeVisible (presetBox);

    prevBtn.setButtonText ("<");
    prevBtn.onClick = [this]
    {
        const int n = presetBox.getNumItems();
        presetBox.setSelectedItemIndex ((presetBox.getSelectedItemIndex() - 1 + n) % n);
    };
    nextBtn.setButtonText (">");
    nextBtn.onClick = [this]
    {
        const int n = presetBox.getNumItems();
        presetBox.setSelectedItemIndex ((presetBox.getSelectedItemIndex() + 1) % n);
    };
    addAndMakeVisible (prevBtn);
    addAndMakeVisible (nextBtn);

    // A / B
    aBtn.setButtonText ("A");
    bBtn.setButtonText ("B");
    for (auto* b : { &aBtn, &bBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (300);
        addAndMakeVisible (*b);
    }
    aBtn.setToggleState (true, dontSendNotification);
    aBtn.onClick = [this] { proc.switchSlot (0); };
    bBtn.onClick = [this] { proc.switchSlot (1); };

    // Rango dB
    rangeBox.addItem ("3 dB", 1);
    rangeBox.addItem ("6 dB", 2);
    rangeBox.addItem ("12 dB", 3);
    rangeBox.addItem ("24 dB", 4);
    rangeBox.setSelectedId (3, dontSendNotification);
    rangeBox.onChange = [this]
    {
        static const float v[4] = { 3.0f, 6.0f, 12.0f, 24.0f };
        display.setRangeDb (v[jlimit (0, 3, rangeBox.getSelectedId() - 1)]);
    };
    addAndMakeVisible (rangeBox);

    // Undo
    undoBtn.setButtonText ("Undo");
    undoBtn.onClick = [this] { proc.undoManager.undo(); };
    addAndMakeVisible (undoBtn);

    // Power
    powerBtn.setButtonText ("ON");
    powerBtn.setClickingTogglesState (true);
    powerBtn.setColour (TextButton::buttonOnColourId, Colour (0xff22e56b));
    powerBtn.setColour (TextButton::buttonColourId, Colour (0xff3a1530));
    powerBtn.setColour (TextButton::textColourOnId, Colour (0xff04210f));
    powerBtn.setToggleState (true, dontSendNotification);
    powerBtn.onClick = [this]
    {
        proc.setParamValue ("Bypass", powerBtn.getToggleState() ? 0.0f : 1.0f);
    };
    addAndMakeVisible (powerBtn);

    // Salida
    outKnob.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    outKnob.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 16);
    outKnob.setTextBoxIsEditable (false);
    outKnob.setColour (Slider::rotarySliderFillColourId, Colour (0xfffacc15));
    outKnob.setColour (Slider::textBoxTextColourId, Colours::white);
    outKnob.setColour (Slider::textBoxOutlineColourId, Colour (0x00000000));
    outKnob.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (outKnob);
    outAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "OutputGain", outKnob);
    outKnob.textFromValueFunction = [] (double v) { return String (v, 1) + " dB"; };
    outKnob.updateText();

    analyzerBox.addItem ("Analyzer On", 1);
    analyzerBox.addItem ("Analyzer Off", 2);
    analyzerBox.setSelectedId (1, dontSendNotification);
    analyzerBox.onChange = [this] { display.setShowSpectrum (analyzerBox.getSelectedId() == 1); };
    addAndMakeVisible (analyzerBox);

    // Stereo / Solo
    stereoBtn.setButtonText ("Stereo");
    soloBtn.setButtonText ("Solo");
    for (auto* b : { &stereoBtn, &soloBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (400);
        b->setColour (TextButton::buttonOnColourId, Colour (0xffd61fa0));
        addAndMakeVisible (*b);
    }
    stereoBtn.setToggleState (true, dontSendNotification);
    stereoBtn.onClick = [this] { proc.setParamValue ("SoloMode", 0.0f); };
    soloBtn.onClick = [this]
    {
        proc.setParamValue ("ActiveSoloBand", (float) display.getSelectedBand());
        proc.setParamValue ("SoloMode", 1.0f);
    };

    setSize (1200, 700);
    startTimerHz (15);
}

NeneEQAudioProcessorEditor::~NeneEQAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void NeneEQAudioProcessorEditor::timerCallback()
{
    powerBtn.setToggleState (proc.apvts.getRawParameterValue ("Bypass")->load() < 0.5f, dontSendNotification);

    const bool solo = proc.apvts.getRawParameterValue ("SoloMode")->load() > 0.5f;
    soloBtn.setToggleState (solo, dontSendNotification);
    stereoBtn.setToggleState (! solo, dontSendNotification);

    aBtn.setToggleState (proc.getCurrentSlot() == 0, dontSendNotification);
    bBtn.setToggleState (proc.getCurrentSlot() == 1, dontSendNotification);
}

void NeneEQAudioProcessorEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (Colour (0xff6d28ff), 0.0f, 0.0f,
                                       Colour (0xffff1d9b), (float) getWidth(), (float) getHeight() * 0.6f, false));
    g.fillAll();

    g.setColour (Colours::white);
    g.setFont (22.0f);
    g.drawText ("NENE PRODUCE EQ", 18, 10, 320, 36, Justification::centredLeft);

    g.setColour (Colour (0xff0d0916));
    g.fillRoundedRectangle (outRect.toFloat(), 10.0f);
    g.setColour (Colour (0x55fffff0));
    g.drawRoundedRectangle (outRect.toFloat().reduced (1.0f), 10.0f, 1.0f);

    g.setColour (Colours::white.withAlpha (0.7f));
    g.setFont (12.0f);
    g.drawText ("OUTPUT", outLabelRect, Justification::centred);
}

void NeneEQAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (46);
    powerBtn.setBounds (top.removeFromRight (46).reduced (0, 6));   top.removeFromRight (8);
    undoBtn.setBounds (top.removeFromRight (56).reduced (0, 6));    top.removeFromRight (8);
    rangeBox.setBounds (top.removeFromRight (90).reduced (0, 6));   top.removeFromRight (8);
    nextBtn.setBounds (top.removeFromRight (30).reduced (0, 6));    top.removeFromRight (4);
    prevBtn.setBounds (top.removeFromRight (30).reduced (0, 6));    top.removeFromRight (8);
    bBtn.setBounds (top.removeFromRight (34).reduced (0, 6));       top.removeFromRight (2);
    aBtn.setBounds (top.removeFromRight (34).reduced (0, 6));       top.removeFromRight (8);
    presetBox.setBounds (top.removeFromRight (200).reduced (0, 6));

    r.removeFromTop (6);
    display.setBounds (r.removeFromTop (420));
    r.removeFromTop (8);

    auto bottom = r;
    outRect = bottom.removeFromRight (160);
    bottom.removeFromRight (6);

    const int pw = (bottom.getWidth() - 4 * 6) / 5;
    for (int i = 0; i < 5; ++i)
    {
        panels[(size_t) i]->setBounds (bottom.removeFromLeft (pw));
        bottom.removeFromLeft (6);
    }

    auto o = outRect.reduced (8);
    auto knobArea = o.removeFromTop (118);
    outLabelRect = knobArea.removeFromTop (16);
    outKnob.setBounds (knobArea);
    o.removeFromTop (6);
    analyzerBox.setBounds (o.removeFromTop (24));
    o.removeFromTop (8);
    auto sb = o.removeFromTop (28);
    stereoBtn.setBounds (sb.removeFromLeft (sb.getWidth() / 2).reduced (2, 0));
    soloBtn.setBounds (sb.reduced (2, 0));
}
