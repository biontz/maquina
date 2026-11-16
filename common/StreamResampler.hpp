// StreamResampler.hpp - push-style windowed-sinc stereo resampler (float).
//
// Resampler.hpp pulls engine blocks on demand, which suits a generator. Maquina
// Oneiroi FX processes the host's input, so its pipeline is push-driven:
// host input -> StreamResampler (host rate -> 48 kHz) -> engine ->
// StreamResampler (48 kHz -> host rate) -> output FIFO (OneiroiHost).
//
// push() takes one input frame and emits every output frame that has become
// computable. Output frame j is the band-limited value of the input at
// position j * ratio (ratio = inputRate / outputRate, input frame 0 = the
// first frame pushed after reset(); earlier input counts as silence). It is
// emitted once input frame floor(j * ratio) + kHalf has arrived, so the
// availability lag is kHalf input frames; the time mapping itself has no
// delay. Equal rates: a plain pass-through.
//
// Same filter as Resampler.hpp: Kaiser-windowed sinc (beta 9), 64 taps, 512
// phases with linear interpolation, cutoff at 0.96 x the lower Nyquist.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cmath>
#include <cstring>
#include <vector>

namespace maquina {

class StreamResampler {
public:
    static constexpr int kTaps = 64;
    static constexpr int kHalf = kTaps / 2;
    static constexpr int kPhases = 512;

    void setup(double inputRate, double outputRate)
    {
        fRatio = inputRate / outputRate;
        fBypass = std::fabs(fRatio - 1.0) < 1e-12;
        const double cutoff = (fRatio > 1.0 ? 1.0 / fRatio : 1.0) * 0.96;
        fTable.assign((kPhases + 1) * kTaps, 0.f);
        for (int p = 0; p <= kPhases; ++p) {
            const double frac = double(p) / kPhases;
            double sum = 0.0, tmp[kTaps];
            for (int t = 0; t < kTaps; ++t) {
                // tap t reads input at offset (t - kHalf + 1) from floor(pos)
                const double x = double(t - kHalf + 1) - frac;
                const double w = kaiser(x / kHalf, 9.0);
                const double s = x == 0.0 ? 1.0 : std::sin(M_PI * cutoff * x) / (M_PI * cutoff * x);
                tmp[t] = s * w;
                sum += tmp[t];
            }
            for (int t = 0; t < kTaps; ++t) fTable[p * kTaps + t] = float(tmp[t] / sum);
        }
        reset();
    }

    void reset()
    {
        std::memset(fHistL, 0, sizeof(fHistL));
        std::memset(fHistR, 0, sizeof(fHistR));
        fWrite = 0;
        fIn = 0;
        fPosInt = 0;
        fPosFrac = 0.0;
    }

    bool bypass() const { return fBypass; }
    double ratio() const { return fRatio; }

    // emit(float l, float r) for every output frame this input makes computable
    template <class Emit>
    void push(float l, float r, Emit&& emit)
    {
        if (fBypass) {
            emit(l, r);
            return;
        }
        fHistL[fWrite] = fHistL[fWrite + kTaps] = l;
        fHistR[fWrite] = fHistR[fWrite + kTaps] = r;
        fWrite = (fWrite + 1) % kTaps;
        ++fIn;
        // the window [floor(pos) - kHalf + 1, floor(pos) + kHalf] ends at the
        // newest input exactly when floor(pos) + kHalf + 1 == fIn
        while (fPosInt + kHalf + 1 <= fIn) {
            const double pf = fPosFrac * kPhases;
            const int p = int(pf);
            const float a = float(pf - p);
            const float* c0 = &fTable[p * kTaps];
            const float* c1 = c0 + kTaps;
            // window start: kTaps - 1 - (fIn - 1 - (fPosInt + kHalf)) frames back from the newest
            const long back = long(fIn - 1 - (fPosInt + kHalf)); // 0 while the window ends at the newest
            const float* hl = &fHistL[(fWrite + kTaps - back) % kTaps];
            const float* hr = &fHistR[(fWrite + kTaps - back) % kTaps];
            float al[8] = {}, ar[8] = {};
            for (int t = 0; t < kTaps; t += 8)
                for (int k = 0; k < 8; ++k) {
                    const float c = c0[t + k] + a * (c1[t + k] - c0[t + k]);
                    al[k] += c * hl[t + k];
                    ar[k] += c * hr[t + k];
                }
            emit(((al[0] + al[4]) + (al[1] + al[5])) + ((al[2] + al[6]) + (al[3] + al[7])),
                 ((ar[0] + ar[4]) + (ar[1] + ar[5])) + ((ar[2] + ar[6]) + (ar[3] + ar[7])));
            fPosFrac += fRatio;
            const long adv = long(fPosFrac);
            fPosFrac -= double(adv);
            fPosInt += adv;
        }
    }

private:
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

    double fRatio = 1.0;
    bool fBypass = true;
    std::vector<float> fTable;
    // the last kTaps inputs, each stored twice (at w and w + kTaps) so any
    // window of kTaps consecutive frames is contiguous
    float fHistL[kTaps * 2] = {};
    float fHistR[kTaps * 2] = {};
    int fWrite = 0;
    long fIn = 0;     // input frames pushed
    long fPosInt = 0; // integer part of the next output's input position
    double fPosFrac = 0.0;
};

} // namespace maquina
