#include "PluginEditor.h"

using namespace juce;

static Font uiFont (float h, bool bold = false)
{
    return Font (FontOptions (h, bold ? Font::bold : Font::plain));
}

//==============================================================================
// LookAndFeel
//==============================================================================
ClarityLookAndFeel::ClarityLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, Palette::bg);
    setColour (TextButton::buttonColourId, Palette::panelEdge);
    setColour (TextButton::textColourOffId, Palette::text);
}

void ClarityLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                           float start, float end, Slider& s)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size  = jmin (bounds.getWidth(), bounds.getHeight());
    const float thick = jmax (3.0f, size * 0.055f);
    const float r     = size * 0.5f - thick - 6.0f;
    if (r < 8.0f) return;

    const auto c      = bounds.getCentre();
    const auto col    = s.findColour (Slider::rotarySliderFillColourId);
    const float angle = start + pos * (end - start);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const PathStrokeType stroke (thick, PathStrokeType::curved, PathStrokeType::rounded);

    // track
    Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour (Palette::track);
    g.strokePath (track, stroke);

    // value arc (+ soft glow)
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.01f)
    {
        Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, jmin (from, angle), jmax (from, angle), true);
        g.setColour (col.withAlpha (0.18f));
        g.strokePath (val, PathStrokeType (thick * 2.8f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (val, stroke);
    }

    // AUTO "effective value" ring
    const float eff = (float) (double) s.getProperties().getWithDefault ("effective", -1.0);
    if (eff >= 0.0f)
    {
        const float rr = r + thick * 0.5f + 4.0f;
        const float ea = start + eff * (end - start);
        Path e;
        e.addCentredArc (c.x, c.y, rr, rr, 0.0f, start, jmax (start + 0.001f, ea), true);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.strokePath (e, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        const Point<float> dot (c.x + rr * std::sin (ea), c.y - rr * std::cos (ea));
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (dot));
    }

    // knob body
    const float kr = r - thick - 4.0f;
    g.setGradientFill (ColourGradient (Colour (0xff323a4c), c.x, c.y - kr, Colour (0xff10131a), c.x, c.y + kr, false));
    g.fillEllipse (Rectangle<float> (kr * 2.0f, kr * 2.0f).withCentre (c));
    g.setColour (Colour (0xff434c62));
    g.drawEllipse (Rectangle<float> (kr * 2.0f, kr * 2.0f).withCentre (c), 1.0f);

    // pointer
    const Point<float> dir (std::sin (angle), -std::cos (angle));
    g.setColour (col);
    g.drawLine (Line<float> (c + dir * (kr * 0.35f), c + dir * (kr * 0.85f)), jmax (2.0f, kr * 0.09f));
}

void ClarityLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const auto base = Palette::autoCol;
    g.setGradientFill (ColourGradient (base.withAlpha (down ? 0.45f : (hi ? 0.32f : 0.22f)), r.getX(), r.getY(),
                                       base.withAlpha (0.08f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (base.withAlpha (hi ? 0.9f : 0.6f));
    g.drawRoundedRectangle (r, 7.0f, 1.2f);
}

void ClarityLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setColour (Palette::text);
    g.setFont (uiFont (13.0f, true));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds(), Justification::centred, 1);
}

//==============================================================================
// MacroKnob
//==============================================================================
MacroKnob::MacroKnob (String n, String sub, Colour c, bool b)
    : name (std::move (n)), subtitle (std::move (sub)), colour (c), big (b)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setColour (Slider::rotarySliderFillColourId, colour);
    slider.setMouseDragSensitivity (big ? 260 : 200);
    slider.setVelocityBasedMode (false);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void MacroKnob::setEffective (float v)
{
    if (std::abs (v - effective) < 0.002f) return;
    effective = v;
    slider.getProperties().set ("effective", (double) v);
    slider.repaint();
}

void MacroKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (big ? 40 : 36);
    r.removeFromBottom (22);
    slider.setBounds (r);
}

void MacroKnob::paint (Graphics& g)
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (big ? 40 : 36);

    g.setColour (colour);
    g.setFont (uiFont (big ? 15.0f : 12.5f, true));
    g.drawFittedText (name, top.removeFromTop (big ? 22 : 19), Justification::centred, 1, 0.6f);
    g.setColour (Palette::dim);
    g.setFont (uiFont (10.5f));
    g.drawFittedText (subtitle, top, Justification::centredTop, 1, 0.6f);

    String value = formatValue (slider.getValue());
    if (effective >= 0.0f && ! slider.getProperties().getWithDefault ("bipolar", false))
        value << "  >  " << roundToInt (effective * 100.0f) << "%";
    g.setColour (Palette::text);
    g.setFont (uiFont (12.5f, true));
    g.drawFittedText (value, r.removeFromBottom (22), Justification::centred, 1, 0.6f);
}

//==============================================================================
// Visualizer
//==============================================================================
Visualizer::Visualizer (ClarityStripAudioProcessor& p) : proc (p)
{
    inSpec.fill (-120.0f); outSpec.fill (-120.0f); peakSpec.fill (-120.0f);
    wfMax.assign (400, 0.0f); wfMin.assign (400, 0.0f); wfInMax.assign (400, 0.0f);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void Visualizer::mouseDown (const MouseEvent&)
{
    mode = mode == Mode::Spectrum ? Mode::Scope : (mode == Mode::Scope ? Mode::Waveform : Mode::Spectrum);
    repaint();
}

void Visualizer::computeSpectrum (const float* src, std::array<float, numPoints>& out)
{
    std::copy (src, src + fftSize, fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float sr = (float) (proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0);
    const float norm = 4.0f / (float) fftSize; // Hann coherent gain + single-sided spectrum
    const int maxBin = fftSize / 2 - 1;

    for (int p = 0; p < numPoints; ++p)
    {
        const float f0 = 20.0f * std::pow (1000.0f, (float) p / numPoints);
        const float f1 = 20.0f * std::pow (1000.0f, (float) (p + 1) / numPoints);
        const float b0 = f0 / sr * fftSize, b1 = f1 / sr * fftSize;
        float m = 0.0f;
        if (b1 - b0 < 1.0f)
        {
            const float bc = jlimit (1.0f, (float) maxBin - 1.0f, 0.5f * (b0 + b1));
            const int i = (int) bc;
            const float fr = bc - (float) i;
            m = fftData[(size_t) i] * (1.0f - fr) + fftData[(size_t) i + 1] * fr;
        }
        else
        {
            const int i0 = jlimit (1, maxBin, (int) b0), i1 = jlimit (i0 + 1, maxBin + 1, (int) std::ceil (b1));
            for (int i = i0; i < i1; ++i) m = jmax (m, fftData[(size_t) i]);
        }
        const float fc = std::sqrt (f0 * f1);
        const float tilt = 3.0f * std::log2 (fc / 1000.0f); // +3 dB/oct display tilt (pink noise looks flat)
        out[(size_t) p] = Decibels::gainToDecibels (m * norm, -120.0f) + tilt;
    }
}

void Visualizer::update()
{
    if (! isShowing()) return;

    proc.inputScope.readLatest (inBuf.data(), fftSize);
    proc.outputScope.readLatest (outBuf.data(), fftSize);

    std::array<float, numPoints> tmp {};
    computeSpectrum (inBuf.data(), tmp);
    for (int i = 0; i < numPoints; ++i)
    {
        auto& s = inSpec[(size_t) i];
        s = tmp[(size_t) i] > s ? 0.5f * s + 0.5f * tmp[(size_t) i] : 0.85f * s + 0.15f * tmp[(size_t) i];
    }
    computeSpectrum (outBuf.data(), tmp);
    for (int i = 0; i < numPoints; ++i)
    {
        auto& s = outSpec[(size_t) i];
        s = tmp[(size_t) i] > s ? 0.5f * s + 0.5f * tmp[(size_t) i] : 0.85f * s + 0.15f * tmp[(size_t) i];
        peakSpec[(size_t) i] = jmax (s, peakSpec[(size_t) i] - 0.35f);
    }

    proc.inputScope.readLatest (scopeIn.data(), (int) scopeIn.size());
    proc.outputScope.readLatest (scopeOut.data(), (int) scopeOut.size());

    const int spf = jlimit (64, (int) scopeOut.size(), (int) (proc.getSampleRate() / 30.0));
    float mx = 0.0f, mn = 0.0f, imx = 0.0f;
    for (int i = (int) scopeOut.size() - spf; i < (int) scopeOut.size(); ++i)
    {
        mx = jmax (mx, scopeOut[(size_t) i]); mn = jmin (mn, scopeOut[(size_t) i]);
        imx = jmax (imx, std::abs (scopeIn[(size_t) i]));
    }
    wfMax.erase (wfMax.begin()); wfMax.push_back (mx);
    wfMin.erase (wfMin.begin()); wfMin.push_back (mn);
    wfInMax.erase (wfInMax.begin()); wfInMax.push_back (imx);

    repaint();
}

float Visualizer::xForFreq (float f, Rectangle<float> r) const
{
    return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}

float Visualizer::yForDb (float db, Rectangle<float> r) const
{
    return jmap (jlimit (-90.0f, 6.0f, db), -90.0f, 6.0f, r.getBottom(), r.getY());
}

void Visualizer::drawGrid (Graphics& g, Rectangle<float> plot)
{
    g.setFont (uiFont (10.0f));
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        const float x = xForFreq (f, plot);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (Palette::dim);
        const String label = f >= 1000.0f ? String (f / 1000.0f, 0) + "k" : String ((int) f);
        g.drawText (label, Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 12), Justification::centred);
    }
    for (int db = 0; db >= -84; db -= 12)
    {
        const float y = yForDb ((float) db, plot);
        g.setColour (Colours::white.withAlpha (db == 0 ? 0.12f : 0.05f));
        g.drawHorizontalLine (roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (Palette::dim);
        g.drawText (String (db), Rectangle<float> (plot.getX() - 30, y - 6, 26, 12), Justification::centredRight);
    }
}

void Visualizer::drawSpectrum (Graphics& g, Rectangle<float> plot)
{
    drawGrid (g, plot);

    auto makePath = [&] (const std::array<float, numPoints>& spec, bool closed)
    {
        Path p;
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * ((float) i + 0.5f) / numPoints;
            const float y = yForDb (spec[(size_t) i], plot);
            if (i == 0) { if (closed) { p.startNewSubPath (plot.getX(), plot.getBottom()); p.lineTo (plot.getX(), y); } else p.startNewSubPath (plot.getX(), y); }
            p.lineTo (x, y);
        }
        if (closed) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
        return p;
    };

    // input (before processing): grey ghost
    g.setColour (Colours::white.withAlpha (0.07f));
    g.fillPath (makePath (inSpec, true));
    g.setColour (Colours::white.withAlpha (0.28f));
    g.strokePath (makePath (inSpec, false), PathStrokeType (1.0f));

    // output (after processing): coloured
    g.setGradientFill (ColourGradient (Palette::autoCol.withAlpha (0.35f), 0, plot.getY(), Palette::autoCol.withAlpha (0.02f), 0, plot.getBottom(), false));
    g.fillPath (makePath (outSpec, true));
    ColourGradient lineGrad (Palette::denoise, plot.getX(), 0, Palette::enhancer, plot.getRight(), 0, false);
    lineGrad.addColour (0.5, Palette::comp);
    g.setGradientFill (lineGrad);
    g.strokePath (makePath (outSpec, false), PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (Colours::white.withAlpha (0.35f));
    g.strokePath (makePath (peakSpec, false), PathStrokeType (0.8f));

    // legend
    auto lg = Rectangle<float> (plot.getRight() - 190, plot.getY() + 4, 186, 14);
    g.setFont (uiFont (10.5f));
    g.setColour (Colours::white.withAlpha (0.45f));
    g.fillRect (lg.getX(), lg.getCentreY() - 1, 14.0f, 2.0f);
    g.drawText ("Input", lg.withTrimmedLeft (18), Justification::centredLeft);
    g.setColour (Palette::comp);
    g.fillRect (lg.getX() + 70, lg.getCentreY() - 1, 14.0f, 2.0f);
    g.drawText ("Output (cleaned)", lg.withTrimmedLeft (88), Justification::centredLeft);
}

void Visualizer::drawScope (Graphics& g, Rectangle<float> plot)
{
    const int len = 1024, total = (int) scopeOut.size();
    const int lat = jmax (0, proc.getLatencySamples());
    const int minI = jmin (total - len - 1, lat + 1);
    int start = total - len - 1;
    for (int i = total - len - 1; i > minI; --i)
        if (scopeOut[(size_t) i - 1] < 0.0f && scopeOut[(size_t) i] >= 0.0f) { start = i; break; }

    const float mid = plot.getCentreY(), half = plot.getHeight() * 0.48f;
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (roundToInt (mid), plot.getX(), plot.getRight());
    for (float db : { -6.0f, -20.0f })
    {
        const float a = Decibels::decibelsToGain (db) * half;
        g.drawHorizontalLine (roundToInt (mid - a), plot.getX(), plot.getRight());
        g.drawHorizontalLine (roundToInt (mid + a), plot.getX(), plot.getRight());
    }

    auto trace = [&] (const std::array<float, 4096>& src, int offset)
    {
        Path p;
        for (int i = 0; i < len; ++i)
        {
            const int idx = jlimit (0, total - 1, start + i - offset);
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (len - 1);
            const float y = mid - jlimit (-1.0f, 1.0f, src[(size_t) idx]) * half;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    if (start - lat >= 0)
    {
        g.setColour (Colours::white.withAlpha (0.25f));
        g.strokePath (trace (scopeIn, lat), PathStrokeType (1.0f));
    }
    g.setColour (Palette::autoCol);
    g.strokePath (trace (scopeOut, 0), PathStrokeType (1.6f));
}

void Visualizer::drawWaveform (Graphics& g, Rectangle<float> plot)
{
    const float mid = plot.getCentreY(), half = plot.getHeight() * 0.48f;
    const int n = (int) wfMax.size();
    const float bw = plot.getWidth() / (float) n;

    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (roundToInt (mid), plot.getX(), plot.getRight());

    for (int i = 0; i < n; ++i)
    {
        const float x = plot.getX() + (float) i * bw;
        const float ia = jmin (1.0f, wfInMax[(size_t) i]) * half;
        g.setColour (Colours::white.withAlpha (0.10f));
        g.fillRect (x, mid - ia, jmax (1.0f, bw - 0.5f), ia * 2.0f);

        const float top = jmin (1.0f, wfMax[(size_t) i]) * half, bot = jmin (1.0f, -wfMin[(size_t) i]) * half;
        const float t = (float) i / (float) n;
        g.setColour (Palette::denoise.interpolatedWith (Palette::comp, t).withAlpha (0.35f + 0.65f * t));
        g.fillRect (x, mid - top, jmax (1.0f, bw - 0.5f), top + bot);
    }
}

void Visualizer::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff080a0f), 0, r.getY(), Colour (0xff111725), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    // header tabs
    auto head = r.reduced (14, 8).removeFromTop (16);
    g.setFont (uiFont (11.0f, true));
    const char* names[] = { "SPECTRUM", "SCOPE", "WAVEFORM" };
    const int active = mode == Mode::Spectrum ? 0 : (mode == Mode::Scope ? 1 : 2);
    float tx = head.getX();
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == active ? Palette::autoCol : Palette::dim);
        g.drawText (names[i], Rectangle<float> (tx, head.getY(), 80, 16), Justification::centredLeft);
        tx += 84;
    }
    g.setColour (Palette::dim.withAlpha (0.7f));
    g.setFont (uiFont (10.0f));
    g.drawText ("click to switch view", head, Justification::centredRight);

    auto plot = r.reduced (14, 8).withTrimmedTop (22);
    if (mode == Mode::Spectrum)
    {
        plot = plot.withTrimmedLeft (26).withTrimmedBottom (14);
        drawSpectrum (g, plot);
    }
    else if (mode == Mode::Scope) drawScope (g, plot);
    else                          drawWaveform (g, plot);
}

//==============================================================================
// Meters
//==============================================================================
void Meters::setLevels (float inDb, float outDb, float grDb, float nrDb)
{
    in  = jmax (inDb,  in  - 1.2f);
    out = jmax (outDb, out - 1.2f);
    if (inDb  > inHold)  { inHold  = inDb;  inHoldAge  = 0; } else if (++inHoldAge  > 45) inHold  -= 1.5f;
    if (outDb > outHold) { outHold = outDb; outHoldAge = 0; } else if (++outHoldAge > 45) outHold -= 1.5f;
    gr = grDb < gr ? grDb : gr + (grDb - gr) * 0.15f;
    nr = nr + (nrDb - nr) * 0.2f;
    repaint();
}

void Meters::drawBar (Graphics& g, Rectangle<float> r, const String& label, float norm, Colour col,
                      bool fromTop, const String& value, float holdNorm)
{
    g.setColour (Palette::dim);
    g.setFont (uiFont (10.5f, true));
    g.drawText (label, r.removeFromTop (14), Justification::centred);
    g.setColour (Palette::text);
    g.setFont (uiFont (10.0f));
    g.drawText (value, r.removeFromBottom (14), Justification::centred);

    auto bar = r.reduced (4, 4);
    g.setColour (Palette::track);
    g.fillRoundedRectangle (bar, 3.0f);
    norm = jlimit (0.0f, 1.0f, norm);
    auto fill = fromTop ? bar.withHeight (bar.getHeight() * norm) : bar.withTrimmedTop (bar.getHeight() * (1.0f - norm));
    if (fromTop)
        g.setColour (col);
    else
        g.setGradientFill (ColourGradient (Palette::denoise, 0, bar.getBottom(), Palette::limiter, 0, bar.getY(), false));
    g.fillRoundedRectangle (fill, 3.0f);
    if (holdNorm > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (0.8f));
        const float y = bar.getBottom() - bar.getHeight() * jlimit (0.0f, 1.0f, holdNorm);
        g.fillRect (bar.getX(), y - 1.0f, bar.getWidth(), 2.0f);
    }
}

void Meters::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0b0e14));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    r = r.reduced (6, 10);
    const float w = r.getWidth() / 4.0f;
    auto lvl  = [] (float db) { return jmap (jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, 0.0f, 1.0f); };
    auto fmt  = [] (float db) { return db <= -59.0f ? String ("-inf") : String (db, 1); };

    drawBar (g, r.removeFromLeft (w), "IN",  lvl (in),  Palette::text, false, fmt (in),  lvl (inHold));
    drawBar (g, r.removeFromLeft (w), "OUT", lvl (out), Palette::text, false, fmt (out), lvl (outHold));
    drawBar (g, r.removeFromLeft (w), "GR",  -gr / 24.0f, Palette::comp,    true, String (gr, 1));
    drawBar (g, r,                    "NR",  -nr / 30.0f, Palette::denoise, true, String (nr, 1));
}

//==============================================================================
// Editor
//==============================================================================
ClarityStripAudioProcessorEditor::ClarityStripAudioProcessorEditor (ClarityStripAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), visualizer (p)
{
    setLookAndFeel (&lnf);

    const char* names[] = { "DYNAMICS", "EQ", "COMPRESSOR", "LIMITER", "ENHANCER", "NOISE REMOVER" };
    const char* subs[]  = { "Expand gaps + punch", "Cut mud, add presence", "Smooth & even level",
                            "Loudness, -1 dB ceiling", "Exciter + air", "Spectral denoise" };
    const Colour cols[] = { Palette::dynamics, Palette::eq, Palette::comp, Palette::limiter, Palette::enhancer, Palette::denoise };

    for (int i = 0; i < clarity::NumMacros; ++i)
    {
        knobs[(size_t) i] = std::make_unique<MacroKnob> (names[i], subs[i], cols[i]);
        addAndMakeVisible (*knobs[(size_t) i]);
        attachments[(size_t) i] = std::make_unique<Attachment> (proc.apvts, ParamIDs::macros[(size_t) i], knobs[(size_t) i]->slider);
    }

    outputKnob.slider.getProperties().set ("bipolar", true);
    outputKnob.formatValue = [] (double v) { return (v > 0.0 ? String ("+") : String()) + String (v, 1) + " dB"; };

    addAndMakeVisible (autoKnob);
    addAndMakeVisible (outputKnob);
    autoAttachment   = std::make_unique<Attachment> (proc.apvts, ParamIDs::autoAmt, autoKnob.slider);
    outputAttachment = std::make_unique<Attachment> (proc.apvts, ParamIDs::output,  outputKnob.slider);

    analyzeButton.setTooltip ("Listens to 5 seconds of playing audio and sets every knob for you");
    analyzeButton.onClick = [this]
    {
        proc.engine.startLearn();
        status = "Listening... keep the most typical part of your clip playing.";
        repaint (headerArea);
    };
    addAndMakeVisible (analyzeButton);

    addAndMakeVisible (visualizer);
    addAndMakeVisible (meters);

    setResizable (true, true);
    setResizeLimits (840, 540, 1800, 1100);
    setSize (1000, 640);
    startTimerHz (30);
}

ClarityStripAudioProcessorEditor::~ClarityStripAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ClarityStripAudioProcessorEditor::applyLearned (const float* v)
{
    for (int i = 0; i < clarity::NumMacros; ++i)
        if (auto* prm = proc.apvts.getParameter (ParamIDs::macros[(size_t) i]))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v[i] * 100.0f));
            prm->endChangeGesture();
        }

    status = String::formatted ("Analyzed: noise floor %.0f dBFS, programme level %.0f dBFS. Knobs set; fine-tune by ear, or raise AUTO CLEAN to keep adapting.",
                                proc.engine.learnedFloorDb(), proc.engine.learnedLevelDb());
    repaint (headerArea);
}

void ClarityStripAudioProcessorEditor::timerCallback()
{
    visualizer.update();

    auto& e = proc.engine;
    meters.setLevels (e.inPeakDb.load(), e.outPeakDb.load(), e.grDb.load(), e.nrDb.load());

    const bool autoOn = autoKnob.slider.getValue() > 0.05;
    for (int i = 0; i < clarity::NumMacros; ++i)
        knobs[(size_t) i]->setEffective (autoOn ? e.effective[(size_t) i].load() : -1.0f);

    const float prog = e.learnProgress();
    analyzeButton.setButtonText (prog >= 0.0f ? "ANALYZING  " + String (roundToInt (prog * 100.0f)) + "%" : String ("ANALYZE  5s"));

    float learned[clarity::NumMacros];
    if (e.consumeLearned (learned))
        applyLearned (learned);
}

void ClarityStripAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (Palette::bg);

    // header
    auto h = headerArea;
    auto titleArea = h.removeFromLeft (260);
    g.setColour (Palette::text);
    g.setFont (uiFont (26.0f, true));
    g.drawText ("CLARITY", titleArea.removeFromLeft (118), Justification::centredLeft);
    g.setColour (Palette::autoCol);
    g.setFont (uiFont (26.0f));
    g.drawText ("STRIP", titleArea, Justification::centredLeft);

    g.setColour (Palette::dim);
    g.setFont (uiFont (11.0f));
    g.drawFittedText ("Chain:  NOISE REMOVER > EQ > DYNAMICS > COMPRESSOR > ENHANCER > LIMITER > OUT",
                      h.removeFromTop (h.getHeight() / 2), Justification::bottomRight, 1, 0.7f);
    g.setColour (Palette::text.withAlpha (0.85f));
    g.drawFittedText (status, h, Justification::topRight, 1, 0.6f);

    // panels
    for (auto pr : { knobPanel, autoPanel, outPanel })
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (pr.toFloat(), 10.0f);
        g.setColour (Palette::panelEdge);
        g.drawRoundedRectangle (pr.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }
    g.setColour (Palette::autoCol.withAlpha (0.35f));
    g.drawRoundedRectangle (autoPanel.toFloat().reduced (0.5f), 10.0f, 1.4f);
}

void ClarityStripAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    headerArea = r.removeFromTop (48);
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (jmax (220, (int) ((float) getHeight() * 0.38f)));
    r.removeFromBottom (12);

    auto meterArea = r.removeFromRight (160);
    r.removeFromRight (10);
    visualizer.setBounds (r);
    meters.setBounds (meterArea);

    outPanel = bottom.removeFromRight (130);
    bottom.removeFromRight (10);
    autoPanel = bottom.removeFromRight (210);
    bottom.removeFromRight (10);
    knobPanel = bottom;

    auto kp = knobPanel.reduced (6, 10);
    const int w = kp.getWidth() / clarity::NumMacros;
    for (auto& k : knobs) k->setBounds (kp.removeFromLeft (w));

    auto ap = autoPanel.reduced (12, 10);
    analyzeButton.setBounds (ap.removeFromBottom (34));
    ap.removeFromBottom (6);
    autoKnob.setBounds (ap);

    outputKnob.setBounds (outPanel.reduced (6, 10));
}
