// Resampler.hpp - windowed-sinc stereo resampler, engine rate -> host rate.
//
// The engine runs at the hardware's 44140 Hz; hosts run at anything. The
// engine is a generator, so the resampler simply pulls 64-frame engine blocks
// on demand ("lookahead" costs no latency, it just renders a little ahead).
//
// Output frame k is the band-limited value of the input at position
// k * ratio - 1 (one input sample behind the newest tap centre).
// The filter reads input up to floor(k * ratio) + kHalf.
//
// Kaiser-windowed sinc (beta 9), 64 taps, 512 phases with linear
// interpolation between phases. Cutoff at 0.96 x the lower Nyquist:
// flat (+-0.01 dB) to 19 kHz, -0.3 dB at 20 kHz, ~90 dB SNR in the passband
// (tools/resampler_test).
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace maquina {

class Resampler {
public:
    static constexpr int kTaps = 64;
    static constexpr int kHalf = kTaps / 2;
    static constexpr int kPhases = 512;
    static constexpr int kBlock = 64;

    // Max input frames rendered ahead of floor(outputFrame * ratio).
    static constexpr int kMaxLookahead = kHalf + kBlock;

    void setup(double inputRate, double outputRate)
    {
        fRatio = inputRate / outputRate;
        fBypass = std::fabs(fRatio - 1.0) < 1e-12;
        const double cutoff = (fRatio > 1.0 ? 1.0 / fRatio : 1.0) * 0.96;
        fTable.assign((kPhases + 1) * kTaps, 0.f);
        const double beta = 9.0;
        for (int p = 0; p <= kPhases; ++p) {
            const double frac = double(p) / kPhases;
            double sum = 0.0;
            double tmp[kTaps];
            for (int t = 0; t < kTaps; ++t) {
                // tap t reads input at offset (t - kHalf + 1) from floor(pos)
                const double x = double(t - kHalf + 1) - frac;
                const double w = kaiser(x / kHalf, beta);
                const double s = x == 0.0 ? 1.0 : std::sin(M_PI * cutoff * x) / (M_PI * cutoff * x);
                tmp[t] = s * w;
                sum += tmp[t];
            }
            for (int t = 0; t < kTaps; ++t)
                fTable[p * kTaps + t] = float(tmp[t] / sum); // unity DC gain per phase
        }
        reset();
    }

    void reset()
    {
        std::memset(fHistL, 0, sizeof(fHistL));
        std::memset(fHistR, 0, sizeof(fHistR));
        fWrite = 0;
        fFrac = 0.0;
        fNeed = kHalf;
        fBlockPos = kBlock;
    }

    double ratio() const { return fRatio; }

    // `render(int16_t* lr)` fills one interleaved 64-frame engine block.
    template <class Render>
    void process(float* outL, float* outR, uint32_t frames, Render&& render)
    {
        if (fBypass) {
            for (uint32_t i = 0; i < frames; ++i) {
                if (fBlockPos >= kBlock) { render(fBlock); fBlockPos = 0; }
                outL[i] = fBlock[2 * fBlockPos] * kScale;
                outR[i] = fBlock[2 * fBlockPos + 1] * kScale;
                ++fBlockPos;
            }
            return;
        }
        for (uint32_t i = 0; i < frames; ++i) {
            for (; fNeed > 0; --fNeed) push(render);
            const double pf = fFrac * kPhases;
            const int p = int(pf);
            const float a = float(pf - p);
            const float* __restrict c0 = &fTable[p * kTaps];
            const float* __restrict c1 = c0 + kTaps;
            // oldest..newest window, contiguous thanks to the mirrored ring
            const float* __restrict hl = &fHistL[fWrite];
            const float* __restrict hr = &fHistR[fWrite];
            // 8 independent lanes: maps onto SSE/NEON without peeling
            float al[8] = {}, ar[8] = {};
            for (int t = 0; t < kTaps; t += 8) {
                for (int k = 0; k < 8; ++k) {
                    const float c = c0[t + k] + a * (c1[t + k] - c0[t + k]);
                    al[k] += c * hl[t + k];
                    ar[k] += c * hr[t + k];
                }
            }
            const float l = ((al[0] + al[4]) + (al[1] + al[5])) + ((al[2] + al[6]) + (al[3] + al[7]));
            const float r = ((ar[0] + ar[4]) + (ar[1] + ar[5])) + ((ar[2] + ar[6]) + (ar[3] + ar[7]));
            outL[i] = l * kScale;
            outR[i] = r * kScale;
            fFrac += fRatio;
            const int adv = int(fFrac); // fFrac >= 0: truncation == floor, no libm call
            fFrac -= adv;
            fNeed += adv;
        }
    }

private:
    static constexpr float kScale = 1.0f / 32768.0f;

    static double bessel0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    }
    static double kaiser(double x, double beta)
    {
        if (x <= -1.0 || x >= 1.0) return 0.0;
        return bessel0(beta * std::sqrt(1.0 - x * x)) / bessel0(beta);
    }

    // History ring of kTaps frames, every sample stored twice (at w and
    // w + kTaps) so the current window [fWrite, fWrite + kTaps) is contiguous.
    template <class Render>
    void push(Render&& render)
    {
        if (fBlockPos >= kBlock) { render(fBlock); fBlockPos = 0; }
        const float l = fBlock[2 * fBlockPos], r = fBlock[2 * fBlockPos + 1];
        fHistL[fWrite] = fHistL[fWrite + kTaps] = l;
        fHistR[fWrite] = fHistR[fWrite + kTaps] = r;
        fWrite = (fWrite + 1) % kTaps;
        ++fBlockPos;
    }

    double fRatio = 1.0;
    bool fBypass = true;
    std::vector<float> fTable;
    alignas(32) float fHistL[kTaps * 2] = {};
    alignas(32) float fHistR[kTaps * 2] = {};
    int fWrite = 0;
    double fFrac = 0.0;
    int fNeed = kHalf;
    int16_t fBlock[kBlock * 2] = {};
    int fBlockPos = kBlock;
};

} // namespace maquina
