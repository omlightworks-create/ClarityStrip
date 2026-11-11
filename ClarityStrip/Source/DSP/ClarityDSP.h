/*
    ClarityStrip DSP engine
    -----------------------
    Pure C++17 (no JUCE dependency) so it can be unit-tested offline.

    Signal chain:
      Input -> Noise Remover (STFT spectral gate) -> Clarity EQ -> Dynamics (expander + punch)
            -> Compressor -> Enhancer (exciter + air) -> Limiter (look-ahead) -> Output gain

    Every module is driven by ONE macro value 0..1 (a "knob").
    The AUTO amount blends each manual knob toward the value suggested by the
    continuous audio Analyzer:  effective = manual + auto * (suggested - manual)
*/
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

namespace clarity
{
constexpr int   kMaxChannels = 8;
constexpr float kPi          = 3.14159265358979323846f;

enum Macro { kDynamics = 0, kEQ, kCompressor, kLimiter, kEnhancer, kDenoise, NumMacros };

inline float dbToGain (float db)  { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float powToDb  (double p)  { return (float) (10.0 * std::log10 (std::max (p, 1.0e-12))); }
inline float clamp01  (float x)   { return std::min (1.0f, std::max (0.0f, x)); }
inline float timeCoef (double fs, float ms) { return std::exp (-1.0f / (float) (0.001 * (double) ms * fs)); }

//==============================================================================
// RBJ biquad, transposed direct form II, per-channel state.
class Biquad
{
public:
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void setPeak (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = omega (fs, f), cw = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
    }
    void setLowShelf (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = omega (fs, f), cw = std::cos (w), al = std::sin (w) / (2.0 * q), s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * cw + s), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - s),
             (A + 1) + (A - 1) * cw + s, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - s);
    }
    void setHighShelf (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = omega (fs, f), cw = std::cos (w), al = std::sin (w) / (2.0 * q), s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * cw + s), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - s),
             (A + 1) - (A - 1) * cw + s, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - s);
    }
    void setHighPass (double fs, double f, double q)
    {
        const double w = omega (fs, f), cw = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    void setBandPass (double fs, double f, double q)
    {
        const double w = omega (fs, f), cw = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (al, 0, -al, 1 + al, -2 * cw, 1 - al);
    }

    inline float process (float x, int ch) noexcept
    {
        auto& s = st[(size_t) ch];
        const float y = b0 * x + s[0];
        s[0] = b1 * x - a1 * y + s[1];
        s[1] = b2 * x - a2 * y;
        return y;
    }

    void reset() { for (auto& s : st) s = { 0.0f, 0.0f }; }

private:
    static double omega (double fs, double f) { return 2.0 * 3.14159265358979323846 * std::min (f, fs * 0.45) / fs; }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<std::array<float, 2>, kMaxChannels> st {};
};

//==============================================================================
// Small radix-2 complex FFT (precomputed twiddles + bit reversal).
class FFT
{
public:
    void init (int size)
    {
        n = size;
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
            tw[(size_t) k] = std::polar (1.0f, -2.0f * kPi * (float) k / (float) n);
    }

    void transform (std::complex<float>* a, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (a[i], a[rev[(size_t) i]]);

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = tw[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const auto u = a[i + j], v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
        if (inverse)
        {
            const float s = 1.0f / (float) n;
            for (int i = 0; i < n; ++i) a[i] *= s;
        }
    }

private:
    int n = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

//==============================================================================
// NOISE REMOVER — STFT spectral subtraction with minimum-statistics noise tracking.
// Learns the noise floor automatically (tracks per-bin minima, slowly rising),
// so it adapts to hiss, hum, fan, room tone, traffic... without a manual "learn".
class NoiseReducer
{
public:
    static constexpr int N = 1024, H = 256;

    void prepare (int numCh)
    {
        fft.init (N);
        win.resize (N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) N);
        buf.assign (N, {});
        chans.assign ((size_t) std::min (numCh, kMaxChannels), {});
        for (auto& c : chans)
        {
            c.inFifo.assign (N, 0.0f); c.outFifo.assign (N, 0.0f); c.accum.assign (2 * N, 0.0f);
            c.noise.assign (N / 2 + 1, 0.0f); c.smooth.assign (N / 2 + 1, 0.0f);
            c.gain.assign (N / 2 + 1, 1.0f); c.tmpGain.assign (N / 2 + 1, 1.0f);
            c.rover = N - H; c.frames = 0;
        }
    }

    void setAmount (float a) { amount = clamp01 (a); }
    int  latency() const      { return N; }  // measured: impulse emerges exactly N samples later
    float getReductionDb() const { return reductionDb; }

    void process (float* const* data, int numCh, int n)
    {
        numCh = std::min (numCh, (int) chans.size());
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = chans[(size_t) ch];
            float* x = data[ch];
            for (int i = 0; i < n; ++i)
            {
                c.inFifo[(size_t) c.rover] = x[i];
                x[i] = c.outFifo[(size_t) (c.rover - (N - H))];
                if (++c.rover >= N)
                {
                    c.rover = N - H;
                    processFrame (c);
                }
            }
        }
    }

private:
    struct Chan
    {
        std::vector<float> inFifo, outFifo, accum, noise, smooth, gain, tmpGain;
        int rover = N - H;
        long frames = 0;
    };

    void processFrame (Chan& c)
    {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = { c.inFifo[(size_t) i] * win[(size_t) i], 0.0f };
        fft.transform (buf.data(), false);

        const float overSub = 1.2f + 3.0f * amount;
        const float floorG  = dbToGain (-(6.0f + 26.0f * amount));
        double pin = 0.0, pout = 0.0;

        // Don't learn from digital silence (gaps between clips, transport start):
        // otherwise the noise estimate collapses and needs many seconds to recover.
        double framePow = 0.0;
        for (int k = 0; k <= N / 2; ++k) framePow += std::norm (buf[(size_t) k]);
        const bool learn = framePow / (N / 2 + 1) > 1.0e-9;
        const float rise = c.frames < 150 ? 1.03f : 1.0018f;       // fast warm-up, then ~1.5 dB/s

        for (int k = 0; k <= N / 2; ++k)
        {
            const float p = std::norm (buf[(size_t) k]) + 1.0e-20f;
            float& sm = c.smooth[(size_t) k];
            float& nz = c.noise[(size_t) k];

            if (learn)
            {
                sm = (c.frames == 0) ? p : 0.75f * sm + 0.25f * p;  // smoothed periodogram
                if (c.frames == 0)       nz = sm;
                else if (sm < nz)        nz = 0.85f * nz + 0.15f * sm;  // fall quickly to minima
                else                     nz *= rise;                // rise slowly
            }

            float g = 1.0f;
            if (amount > 0.001f)
            {
                // minima sit below the mean noise power -> bias-compensate (~4 dB);
                // judge against a lightly smoothed power to avoid "musical noise"
                const float noiseMean = nz * 2.5f, ps = 0.5f * (sm + p);
                g = std::max (floorG, std::sqrt (std::max (0.0f, 1.0f - overSub * noiseMean / ps)));
            }
            c.tmpGain[(size_t) k] = g;
        }

        // temporal smoothing (fast open, slower close) + 3-tap frequency smoothing => less "musical noise"
        for (int k = 0; k <= N / 2; ++k)
        {
            const float l = c.tmpGain[(size_t) std::max (0, k - 1)], m = c.tmpGain[(size_t) k], r = c.tmpGain[(size_t) std::min (N / 2, k + 1)];
            const float g = 0.25f * l + 0.5f * m + 0.25f * r;
            float& gs = c.gain[(size_t) k];
            gs = g > gs ? 0.4f * gs + 0.6f * g : 0.8f * gs + 0.2f * g;
            if (amount <= 0.001f) gs = 1.0f;

            const float p = std::norm (buf[(size_t) k]);
            pin += p; pout += p * gs * gs;
            buf[(size_t) k] *= gs;
            if (k > 0 && k < N / 2) buf[(size_t) (N - k)] *= gs;
        }
        if (learn) ++c.frames;

        fft.transform (buf.data(), true);

        constexpr float olaScale = 2.0f / 3.0f; // Hann^2 at 75% overlap sums to 1.5
        for (int i = 0; i < N; ++i) c.accum[(size_t) i] += buf[(size_t) i].real() * win[(size_t) i] * olaScale;
        for (int i = 0; i < H; ++i) c.outFifo[(size_t) i] = c.accum[(size_t) i];
        std::memmove (c.accum.data(), c.accum.data() + H, sizeof (float) * (size_t) N);
        std::fill (c.accum.begin() + N, c.accum.end(), 0.0f);
        std::memmove (c.inFifo.data(), c.inFifo.data() + H, sizeof (float) * (size_t) (N - H));

        const float r = pin > 1.0e-12 ? powToDb (pout / pin) : 0.0f;
        reductionDb = 0.9f * reductionDb + 0.1f * r;
    }

    FFT fft;
    std::vector<float> win;
    std::vector<std::complex<float>> buf;
    std::vector<Chan> chans;
    float amount = 0.0f, reductionDb = 0.0f;
};

//==============================================================================
// CLARITY EQ — rumble high-pass, mud cut, presence lift.
class ClarityEQ
{
public:
    void prepare (double sampleRate) { fs = sampleRate; last = -1.0f; hp.reset(); mud.reset(); pres.reset(); }

    void process (float* const* d, int numCh, int n, float a)
    {
        if (std::abs (a - last) > 0.002f)
        {
            if (last < 0.001f) { hp.reset(); mud.reset(); pres.reset(); }
            hp.setHighPass (fs, 20.0 + 70.0 * a, 0.707);
            mud.setPeak (fs, 280.0, 1.1, -5.0 * a);
            pres.setPeak (fs, 3300.0, 0.9, 4.5 * a);
            last = a;
        }
        if (a < 0.001f) return;
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i)
                d[ch][i] = pres.process (mud.process (hp.process (d[ch][i], ch), ch), ch);
    }

private:
    double fs = 48000.0;
    float last = -1.0f;
    Biquad hp, mud, pres;
};

//==============================================================================
// DYNAMICS — downward expander that tracks the measured noise floor (cleans gaps
// between words / notes) plus a transient "punch" shaper.
class DynamicsProcessor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        envA = timeCoef (fs, 1.0f);  envR = timeCoef (fs, 60.0f);
        openC = timeCoef (fs, 1.5f); closeC = timeCoef (fs, 60.0f);
        fA = timeCoef (fs, 0.5f);    fR = timeCoef (fs, 30.0f);
        sA = timeCoef (fs, 25.0f);   sR = timeCoef (fs, 200.0f);
        env = fast = slow = 0.0f; gDb = 0.0f;
    }

    void process (float* const* d, int numCh, int n, float a, float floorDb)
    {
        grDb = 0.0f;
        if (a < 0.001f) { gDb = 0.0f; return; }

        const float T = std::min (-25.0f, std::max (-80.0f, floorDb + 4.0f + 10.0f * a));
        const float R = 1.0f + 3.0f * a, range = 4.0f + 20.0f * a, punchMax = 4.0f * a;
        float minG = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float pk = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) pk = std::max (pk, std::abs (d[ch][i]));

            env  = pk > env  ? envA * env  + (1 - envA) * pk : envR * env  + (1 - envR) * pk;
            fast = pk > fast ? fA * fast + (1 - fA) * pk     : fR * fast + (1 - fR) * pk;
            slow = pk > slow ? sA * slow + (1 - sA) * pk     : sR * slow + (1 - sR) * pk;

            const float lvl = gainToDb (env);
            const float target = lvl < T ? std::max (-range, (lvl - T) * (R - 1.0f)) : 0.0f;
            gDb = target > gDb ? openC * gDb + (1 - openC) * target : closeC * gDb + (1 - closeC) * target;

            float punch = 0.0f;
            if (lvl > T) punch = clamp01 ((gainToDb (fast) - gainToDb (slow)) / 8.0f) * punchMax;

            const float g = dbToGain (gDb + punch);
            for (int ch = 0; ch < numCh; ++ch) d[ch][i] *= g;
            minG = std::min (minG, gDb);
        }
        grDb = minG;
    }

    float grDb = 0.0f;

private:
    double fs = 48000.0;
    float envA = 0, envR = 0, openC = 0, closeC = 0, fA = 0, fR = 0, sA = 0, sR = 0;
    float env = 0, fast = 0, slow = 0, gDb = 0;
};

//==============================================================================
// COMPRESSOR — soft-knee, stereo-linked, auto make-up.
class Compressor
{
public:
    void prepare (double sampleRate) { fs = sampleRate; env = 0.0f; grS = 0.0f; rel = timeCoef (fs, 160.0f); }

    void process (float* const* d, int numCh, int n, float a)
    {
        grDb = 0.0f;
        if (a < 0.001f) { grS = 0.0f; return; }

        const float T = -12.0f - 18.0f * a, R = 1.0f + 5.0f * a, W = 6.0f;
        const float makeup = -T * (1.0f - 1.0f / R) * 0.5f;
        const float att = timeCoef (fs, 8.0f - 4.0f * a), grRel = timeCoef (fs, 120.0f), grAtt = timeCoef (fs, 2.0f);
        float minGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float pk = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) pk = std::max (pk, std::abs (d[ch][i]));
            env = pk > env ? att * env + (1 - att) * pk : rel * env + (1 - rel) * pk;

            const float x = gainToDb (env), over = x - T;
            float y = x;
            if (2.0f * over > W)                   y = T + over / R;
            else if (2.0f * std::abs (over) <= W)  y = x + (1.0f / R - 1.0f) * (over + W * 0.5f) * (over + W * 0.5f) / (2.0f * W);
            const float gr = y - x;
            grS = gr < grS ? grAtt * grS + (1 - grAtt) * gr : grRel * grS + (1 - grRel) * gr;

            const float g = dbToGain (grS + makeup);
            for (int ch = 0; ch < numCh; ++ch) d[ch][i] *= g;
            minGr = std::min (minGr, grS);
        }
        grDb = minGr;
    }

    float grDb = 0.0f;

private:
    double fs = 48000.0;
    float env = 0, grS = 0, rel = 0;
};

//==============================================================================
// ENHANCER — high-band harmonic exciter + air shelf + low warmth.
class Enhancer
{
public:
    void prepare (double sampleRate) { fs = sampleRate; hp.setHighPass (fs, 2500.0, 0.707); hp.reset(); air.reset(); warm.reset(); last = -1.0f; }

    void process (float* const* d, int numCh, int n, float a)
    {
        if (std::abs (a - last) > 0.002f)
        {
            if (last < 0.001f) { hp.reset(); air.reset(); warm.reset(); }
            air.setHighShelf (fs, 11000.0, 0.707, 5.0 * a);
            warm.setLowShelf (fs, 120.0, 0.707, 1.5 * a);
            last = a;
        }
        if (a < 0.001f) return;

        const float drive = 1.0f + 7.0f * a, mix = 0.3f * a;
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float x = d[ch][i];
                const float h = hp.process (x, ch);
                const float e = std::tanh (drive * h) / drive;
                d[ch][i] = warm.process (air.process (x + mix * e, ch), ch);
            }
    }

private:
    double fs = 48000.0;
    float last = -1.0f;
    Biquad hp, air, warm;
};

//==============================================================================
// LIMITER — look-ahead brickwall with drive. Ceiling -1 dBFS.
class Limiter
{
public:
    void prepare (double sampleRate, int numCh)
    {
        fs = sampleRate;
        L = std::min (256, std::max (16, (int) std::lround (0.0015 * fs)));
        for (int ch = 0; ch < kMaxChannels; ++ch) delay[(size_t) ch].assign ((size_t) L, 0.0f);
        req.assign ((size_t) L + 1, 1.0f);
        pos = rpos = 0; g = 1.0f;
        attC = std::exp (-3.0f / (float) L);
        relC = timeCoef (fs, 80.0f);
        (void) numCh;
    }

    int latency() const { return L; }

    void process (float* const* d, int numCh, int n, float a)
    {
        const bool active = a >= 0.001f;
        const float drive = dbToGain (9.0f * a), ceil = dbToGain (-1.0f);
        float minG = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            float pk = 0.0f;
            std::array<float, kMaxChannels> out {};
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = active ? d[ch][i] * drive : d[ch][i];
                pk = std::max (pk, std::abs (x));
                out[(size_t) ch] = delay[(size_t) ch][(size_t) pos];
                delay[(size_t) ch][(size_t) pos] = x;
            }
            if (++pos >= L) pos = 0;

            req[(size_t) rpos] = (active && pk > ceil) ? ceil / pk : 1.0f;
            if (++rpos > L) rpos = 0;

            float target = 1.0f;
            for (float r : req) target = std::min (target, r);
            g = target < g ? attC * g + (1 - attC) * target : relC * g + (1 - relC) * target;
            minG = std::min (minG, g);

            for (int ch = 0; ch < numCh; ++ch)
            {
                float y = out[(size_t) ch] * (active ? g : 1.0f);
                if (active) y = std::max (-ceil, std::min (ceil, y));
                d[ch][i] = y;
            }
        }
        grDb = active ? gainToDb (minG) : 0.0f;
    }

    float grDb = 0.0f;

private:
    double fs = 48000.0;
    int L = 64, pos = 0, rpos = 0;
    float g = 1.0f, attC = 0.9f, relC = 0.999f;
    std::array<std::vector<float>, kMaxChannels> delay;
    std::vector<float> req;
};

//==============================================================================
// ANALYZER — measures noise floor, programme level, dynamic range, tonal balance
// and crest factor, and turns them into suggested knob positions.
// Runs continuously (for AUTO) and in a 5-second "learn" pass (for ANALYZE).
class Analyzer
{
public:
    static constexpr int kLearnBlocks = 250; // 250 x 20 ms = 5 s

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        blockLen = std::max (64, (int) std::lround (0.02 * fs));
        mudF.setBandPass (fs, 300.0, 0.8);  presF.setBandPass (fs, 3500.0, 0.8);  airF.setHighPass (fs, 8000.0, 0.707);
        mudF.reset(); presF.reset(); airF.reset();
        count = 0; eT = eM = eP = eA = 0.0; peak = 0.0f; init = false;
        floorDb = -70.0f; actDb = -25.0f; m1 = -25.0f; m2 = 625.0f; tilt = 15.0f; airRel = -30.0f; crest = 12.0f;
        lv.resize (kLearnBlocks); tl.resize (kLearnBlocks); ar.resize (kLearnBlocks); cr.resize (kLearnBlocks);
        for (int i = 0; i < NumMacros; ++i) sugg[(size_t) i] = 0.3f;
    }

    void startLearn() { learnRequested.store (true); }

    void process (const float* mono, int n)
    {
        if (learnRequested.exchange (false)) { learning = true; learnCount = 0; progress.store (0.0f); }

        for (int i = 0; i < n; ++i)
        {
            const float x = mono[i];
            const float m = mudF.process (x, 0), p = presF.process (x, 0), a = airF.process (x, 0);
            eT += (double) x * x; eM += (double) m * m; eP += (double) p * p; eA += (double) a * a;
            peak = std::max (peak, std::abs (x));
            if (++count >= blockLen) onBlock();
        }
    }

    float getFloorDb() const { return floorDb; }
    const std::array<float, NumMacros>& suggestions() const { return sugg; }

    std::atomic<bool>  learnReady { false };
    std::atomic<float> progress { -1.0f };   // -1 = idle, 0..1 = learning
    std::array<std::atomic<float>, NumMacros> learned {};
    std::atomic<float> learnedFloor { -90.0f }, learnedLevel { -90.0f };

    static void suggest (float floorDb, float actDb, float dr, float tiltDb, float airRelDb, float crestDb, float* out)
    {
        const float snr = actDb - floorDb;
        out[kDenoise]    = clamp01 ((42.0f - snr) / 27.0f) * (floorDb > -80.0f ? 1.0f : 0.3f);
        out[kDynamics]   = 0.1f + 0.7f * clamp01 ((45.0f - snr) / 30.0f);
        out[kCompressor] = 0.15f + 0.8f * clamp01 ((dr - 2.5f) / 8.0f);
        out[kEQ]         = 0.15f + 0.8f * clamp01 ((tiltDb - 10.0f) / 15.0f);
        out[kEnhancer]   = 0.1f + 0.7f * clamp01 ((-airRelDb - 25.0f) / 18.0f);
        out[kLimiter]    = clamp01 (0.8f * clamp01 ((-16.0f - actDb) / 14.0f) + 0.2f * clamp01 ((crestDb - 14.0f) / 10.0f) + 0.1f);
        for (int i = 0; i < NumMacros; ++i) out[i] = clamp01 (out[i]);
    }

private:
    void onBlock()
    {
        const double inv = 1.0 / (double) count;
        const float L = powToDb (eT * inv), M = powToDb (eM * inv), P = powToDb (eP * inv), A = powToDb (eA * inv);
        const float pk = gainToDb (peak);
        count = 0; eT = eM = eP = eA = 0.0; peak = 0.0f;

        if (L < -96.0f) return; // digital silence: ignore

        if (! init) { floorDb = L; actDb = std::max (L, -40.0f); init = true; }
        floorDb = L < floorDb ? floorDb + (L - floorDb) * 0.3f : floorDb + 0.02f;

        if (L > floorDb + 10.0f && L > -70.0f)
        {
            constexpr float k = 0.02f;
            actDb  += (L - actDb) * k;
            m1     += (L - m1) * k;
            m2     += (L * L - m2) * k;
            tilt   += ((M - P) - tilt) * k;
            airRel += ((A - L) - airRel) * k;
            crest  += ((pk - L) - crest) * k;
        }
        const float dr = std::sqrt (std::max (0.0f, m2 - m1 * m1));
        std::array<float, NumMacros> s {};
        suggest (floorDb, actDb, dr, tilt, airRel, crest, s.data());
        for (int i = 0; i < NumMacros; ++i) sugg[(size_t) i] += (s[(size_t) i] - sugg[(size_t) i]) * 0.05f;

        if (learning)
        {
            const auto j = (size_t) learnCount;
            lv[j] = L; tl[j] = M - P; ar[j] = A - L; cr[j] = pk - L;
            progress.store ((float) ++learnCount / (float) kLearnBlocks);
            if (learnCount >= kLearnBlocks) finishLearn();
        }
    }

    void finishLearn()
    {
        learning = false;
        std::vector<float> sorted (lv.begin(), lv.begin() + learnCount);
        std::sort (sorted.begin(), sorted.end());
        const float fl = sorted[(size_t) (sorted.size() / 10)];

        double sL = 0, sL2 = 0, sT = 0, sA = 0, sC = 0; int nA = 0;
        for (int i = 0; i < learnCount; ++i)
            if (lv[(size_t) i] > fl + 10.0f && lv[(size_t) i] > -70.0f)
            {
                sL += lv[(size_t) i]; sL2 += (double) lv[(size_t) i] * lv[(size_t) i];
                sT += tl[(size_t) i]; sA += ar[(size_t) i]; sC += cr[(size_t) i]; ++nA;
            }

        float act, dr, t, a, c;
        if (nA >= 10)
        {
            act = (float) (sL / nA); dr = (float) std::sqrt (std::max (0.0, sL2 / nA - (sL / nA) * (sL / nA)));
            t = (float) (sT / nA); a = (float) (sA / nA); c = (float) (sC / nA);
        }
        else // steady material (music bed / constant noise): use the whole window
        {
            act = sorted[sorted.size() / 2]; dr = sorted.back() - sorted.front(); dr *= 0.25f;
            t = tilt; a = airRel; c = crest;
        }
        std::array<float, NumMacros> s {};
        suggest (fl, act, dr, t, a, c, s.data());
        for (int i = 0; i < NumMacros; ++i) learned[(size_t) i].store (s[(size_t) i]);
        learnedFloor.store (fl); learnedLevel.store (act);
        progress.store (-1.0f);
        learnReady.store (true);
    }

    double fs = 48000.0;
    int blockLen = 960, count = 0, learnCount = 0;
    double eT = 0, eM = 0, eP = 0, eA = 0;
    float peak = 0.0f;
    bool init = false, learning = false;
    std::atomic<bool> learnRequested { false };
    float floorDb = -70, actDb = -25, m1 = -25, m2 = 625, tilt = 15, airRel = -30, crest = 12;
    Biquad mudF, presF, airF;
    std::vector<float> lv, tl, ar, cr;
    std::array<float, NumMacros> sugg {};
};

//==============================================================================
struct Params
{
    std::array<float, NumMacros> macro {}; // 0..1 each
    float autoAmount = 0.0f;               // 0..1
    float outputDb   = 0.0f;
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock, int numCh)
    {
        fs = sampleRate;
        chans = std::min (numCh, kMaxChannels);
        mono.assign ((size_t) std::max (1, maxBlock), 0.0f);
        nr.prepare (chans);  eq.prepare (fs);  dyn.prepare (fs);  comp.prepare (fs);
        enh.prepare (fs);    lim.prepare (fs, chans);  analyzer.prepare (fs);
        outGain = 1.0f;
    }

    int getLatency() const { return nr.latency() + lim.latency(); }

    void startLearn() { analyzer.startLearn(); }
    bool consumeLearned (float* out)
    {
        if (! analyzer.learnReady.exchange (false)) return false;
        for (int i = 0; i < NumMacros; ++i) out[i] = analyzer.learned[(size_t) i].load();
        return true;
    }
    float learnProgress() const { return analyzer.progress.load(); }
    float learnedFloorDb() const { return analyzer.learnedFloor.load(); }
    float learnedLevelDb() const { return analyzer.learnedLevel.load(); }

    void process (float* const* data, int numCh, int numSamples, const Params& p)
    {
        numCh = std::min (numCh, chans);
        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min ((int) mono.size(), numSamples - done);
            std::array<float*, kMaxChannels> ptr {};
            for (int ch = 0; ch < numCh; ++ch) ptr[(size_t) ch] = data[ch] + done;
            processChunk (ptr.data(), numCh, n, p);
            done += n;
        }
    }

    // meters / UI feedback (read from the UI thread)
    std::atomic<float> inPeakDb { -100.0f }, outPeakDb { -100.0f }, grDb { 0.0f }, nrDb { 0.0f }, expDb { 0.0f }, floorDb { -90.0f };
    std::array<std::atomic<float>, NumMacros> effective {};

private:
    void processChunk (float* const* d, int numCh, int n, const Params& p)
    {
        if (numCh <= 0) return;
        const float invCh = 1.0f / (float) numCh;
        float inPk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) { s += d[ch][i]; inPk = std::max (inPk, std::abs (d[ch][i])); }
            mono[(size_t) i] = s * invCh;
        }
        analyzer.process (mono.data(), n);

        const auto& sug = analyzer.suggestions();
        std::array<float, NumMacros> e {};
        for (int i = 0; i < NumMacros; ++i)
        {
            e[(size_t) i] = clamp01 (p.macro[(size_t) i] + p.autoAmount * (sug[(size_t) i] - p.macro[(size_t) i]));
            effective[(size_t) i].store (e[(size_t) i]);
        }

        nr.setAmount (e[kDenoise]);
        nr.process (d, numCh, n);
        eq.process (d, numCh, n, e[kEQ]);
        dyn.process (d, numCh, n, e[kDynamics], analyzer.getFloorDb());
        comp.process (d, numCh, n, e[kCompressor]);
        enh.process (d, numCh, n, e[kEnhancer]);
        lim.process (d, numCh, n, e[kLimiter]);

        const float target = dbToGain (p.outputDb);
        const float step = (target - outGain) / (float) n;
        float outPk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            outGain += step;
            for (int ch = 0; ch < numCh; ++ch)
            {
                d[ch][i] *= outGain;
                outPk = std::max (outPk, std::abs (d[ch][i]));
            }
        }
        outGain = target;

        inPeakDb.store (gainToDb (inPk));
        outPeakDb.store (gainToDb (outPk));
        grDb.store (comp.grDb + lim.grDb);
        expDb.store (dyn.grDb);
        nrDb.store (nr.getReductionDb());
        floorDb.store (analyzer.getFloorDb());
    }

    double fs = 48000.0;
    int chans = 2;
    float outGain = 1.0f;
    std::vector<float> mono;
    NoiseReducer nr;
    ClarityEQ eq;
    DynamicsProcessor dyn;
    Compressor comp;
    Enhancer enh;
    Limiter lim;
    Analyzer analyzer;
};

} // namespace clarity
