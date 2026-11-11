// Offline DSP test: g++ -std=c++17 -O2 -I../Source tests/offline_test.cpp -o test && ./test
#include "DSP/ClarityDSP.h"
#include <cstdio>
#include <random>

using namespace clarity;

static const double fs = 48000.0;

// "speech-like" source: harmonic voice with moving pitch, syllable envelope and pauses
static std::vector<float> makeVoice (int n, float levelDb)
{
    std::vector<float> v ((size_t) n);
    double ph = 0;
    const float g = dbToGain (levelDb);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double f0 = 140 + 30 * std::sin (2 * 3.14159 * 0.7 * t);
        ph += 2 * 3.14159 * f0 / fs;
        double s = 0;
        for (int h = 1; h < 30; ++h)
        {
            const double fh = h * f0;
            const double formant = std::exp (-std::pow ((fh - 500) / 300, 2)) + 0.6 * std::exp (-std::pow ((fh - 1500) / 400, 2)) + 0.3 * std::exp (-std::pow ((fh - 2800) / 500, 2));
            s += (0.15 + formant) * std::sin (h * ph) / h;
        }
        const double syl = std::max (0.0, std::sin (2 * 3.14159 * 3.0 * t));          // syllables
        const double phrase = std::fmod (t, 2.0) < 1.4 ? 1.0 : 0.0;                    // 0.6 s pause every 2 s
        v[(size_t) i] = (float) (s * syl * phrase) * g;
    }
    return v;
}

static float rmsDb (const std::vector<float>& x, int a, int b)
{
    double e = 0;
    for (int i = a; i < b; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
    return powToDb (e / (b - a));
}

static void run (Engine& eng, std::vector<float>& L, std::vector<float>& R, const Params& p, int block = 512)
{
    for (size_t i = 0; i < L.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, L.size() - i);
        float* ch[2] = { L.data() + i, R.data() + i };
        eng.process (ch, 2, n, p);
    }
}

int main()
{
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; };

    const int n = (int) (fs * 8);

    // 1) NULL TEST: all knobs at 0 => output == input delayed by reported latency
    {
        Engine eng; eng.prepare (fs, 512, 2);
        std::mt19937 rng (1); std::normal_distribution<float> nd (0, 0.1f);
        std::vector<float> in ((size_t) n); for (auto& s : in) s = nd (rng);
        auto L = in, R = in;
        Params p; run (eng, L, R, p, 333);
        const int lat = eng.getLatency();
        double err = 0, ref = 0;
        for (int i = 4096; i < n; ++i) { const double d = L[(size_t) i] - in[(size_t) (i - lat)]; err += d * d; ref += (double) in[(size_t) (i - lat)] * in[(size_t) (i - lat)]; }
        const float nullDb = powToDb (err / ref);
        std::printf ("    latency = %d samples, null residual = %.1f dB\n", lat, nullDb);
        check (nullDb < -90.0f, "Transparent when all knobs are 0 (latency-compensated null)");
    }

    // Noisy voice for the remaining tests
    auto voice = makeVoice (n, -20.0f);
    std::mt19937 rng (7); std::normal_distribution<float> nd (0, 1);
    std::vector<float> noise ((size_t) n);
    const float ng = dbToGain (-50.0f);
    for (int i = 0; i < n; ++i) noise[(size_t) i] = ng * nd (rng) + 0.003f * (float) std::sin (2 * 3.14159 * 50 * i / fs); // hiss + hum
    std::vector<float> mix ((size_t) n);
    for (int i = 0; i < n; ++i) mix[(size_t) i] = voice[(size_t) i] + noise[(size_t) i];

    // pause window inside the last 2 s cycle: t in [7.45, 7.95]
    const int pa = (int) (7.45 * fs), pb = (int) (7.95 * fs);
    const float noiseIn = rmsDb (mix, pa, pb);

    // 2) NOISE REMOVER
    {
        Engine eng; eng.prepare (fs, 512, 2);
        auto L = mix, R = mix; Params p; p.macro[kDenoise] = 1.0f;
        run (eng, L, R, p);
        const int lat = eng.getLatency();
        const float noiseOut = rmsDb (L, pa + lat, pb + lat);
        const float voiceIn = rmsDb (mix, (int) (6.1 * fs), (int) (7.3 * fs));
        const float voiceOut = rmsDb (L, (int) (6.1 * fs) + lat, (int) (7.3 * fs) + lat);
        std::printf ("    noise in pause: %.1f dB -> %.1f dB | voice: %.1f -> %.1f dB | NR meter %.1f dB\n", noiseIn, noiseOut, voiceIn, voiceOut, eng.nrDb.load());
        check (noiseIn - noiseOut > 15.0f, "Noise remover cuts background noise by >15 dB in pauses");
        check (std::abs (voiceIn - voiceOut) < 3.0f, "Noise remover keeps voice level within 3 dB");
    }

    // 3) FULL CHAIN maxed + LIMITER CEILING + no NaNs
    {
        Engine eng; eng.prepare (fs, 512, 2);
        auto L = mix, R = mix;
        for (auto& s : L) s *= 4.0f;   // hot input
        for (auto& s : R) s *= 4.0f;
        Params p; for (auto& m : p.macro) m = 1.0f;
        run (eng, L, R, p, 480);
        float pk = 0; bool finite = true;
        for (float s : L) { pk = std::max (pk, std::abs (s)); finite &= std::isfinite (s); }
        std::printf ("    full chain peak = %.2f dBFS\n", gainToDb (pk));
        check (finite, "Full chain produces finite output");
        check (gainToDb (pk) <= -0.99f, "Limiter holds the -1 dBFS ceiling");
    }

    // 4) ANALYZE (one-shot learn) on noisy, quiet voice
    {
        Engine eng; eng.prepare (fs, 512, 2);
        auto L = mix, R = mix; Params p;
        eng.startLearn();
        run (eng, L, R, p);
        float s[NumMacros];
        const bool got = eng.consumeLearned (s);
        std::printf ("    floor %.1f dB, level %.1f dB -> dyn %.2f eq %.2f comp %.2f lim %.2f enh %.2f nr %.2f\n",
                     eng.learnedFloorDb(), eng.learnedLevelDb(), s[kDynamics], s[kEQ], s[kCompressor], s[kLimiter], s[kEnhancer], s[kDenoise]);
        check (got, "Analyze returns knob suggestions after 5 s");
        check (got && s[kDenoise] > 0.3f, "Analyze recommends noise removal for noisy audio");
    }

    // 5) AUTO on clean audio should recommend little noise removal
    {
        Engine eng; eng.prepare (fs, 512, 2);
        auto clean = makeVoice (n, -18.0f);
        for (int i = 0; i < n; ++i) clean[(size_t) i] += dbToGain (-95.0f) * nd (rng);
        auto L = clean, R = clean; Params p; p.autoAmount = 1.0f;
        run (eng, L, R, p);
        std::printf ("    clean voice: effective NR = %.2f, floor = %.1f dB\n", eng.effective[kDenoise].load(), eng.floorDb.load());
        check (eng.effective[kDenoise].load() < 0.35f, "AUTO stays gentle on clean audio");
    }

    // 6) MONO and odd block sizes
    {
        Engine eng; eng.prepare (fs, 64, 1);
        auto L = mix; Params p; p.autoAmount = 1.0f;
        for (size_t i = 0; i < L.size(); i += 37) { const int k = (int) std::min ((size_t) 37, L.size() - i); float* ch[1] = { L.data() + i }; eng.process (ch, 1, k, p); }
        bool finite = true; for (float s : L) finite &= std::isfinite (s);
        check (finite, "Mono track + odd buffer sizes OK");
    }

    std::printf (fails ? "\n%d test(s) FAILED\n" : "\nAll tests passed\n", fails);
    return fails;
}
