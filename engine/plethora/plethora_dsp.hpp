// plethora_dsp.hpp - the Noise Plethora module's own DSP around the
// algorithms, as in the VCV Rack port (third_party/noise-plethora/reference/):
//
//   StateVariableFilter2ndOrder / 4thOrder   NoisePlethora.cpp (the analog
//                                            multimode filters of A/B and C)
//   Saturator                                plugin.hpp (Befaco)
//   DCBlocker                                plugin.hpp: 4th-order Butterworth
//                                            highpass from 2 Rack biquads
//
// Copied without changes to the arithmetic; only the Rack types are replaced.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright VCV, Befaco and contributors (VCVRack/Befaco); adapted for Maquina.
#pragma once

#include <cmath>

namespace plethora {

enum FilterMode { LOWPASS, HIGHPASS, BANDPASS, NUM_TYPES };

// based on Chapter 4 of THE ART OF VA FILTER DESIGN and
// Chap 12.4 of "Designing Audio Effect Plugins in C++" Will Pirkle
class StateVariableFilter2ndOrder {
public:
    StateVariableFilter2ndOrder() { setParameters(0.f, M_SQRT1_2); }

    void setParameters(float fc, float q)
    {
        // avoid repeated evaluations of tanh if not needed
        if (fc != fcCached || q != qCached) {
            fcCached = fc;
            qCached = q;
            const double g = std::tan(M_PI * fc);
            const double R = 1.0f / (2 * q);
            alpha0 = 1.0 / (1.0 + 2.0 * R * g + g * g);
            alpha = g;
            rho = 2.0 * R + g;
        }
    }

    void process(float input)
    {
        hp = (input - rho * mem1 - mem2) * alpha0;
        bp = alpha * hp + mem1;
        lp = alpha * bp + mem2;
        mem1 = alpha * hp + bp;
        mem2 = alpha * bp + lp;
    }

    float output(FilterMode mode)
    {
        switch (mode) {
        case LOWPASS: return lp;
        case HIGHPASS: return hp;
        case BANDPASS: return bp;
        default: return 0.0;
        }
    }

private:
    float alpha, alpha0, rho;
    float fcCached = -1.f, qCached = -1.f;
    float hp = 0.0f, bp = 0.0f, lp = 0.0f, mem1 = 0.0f, mem2 = 0.0f;
};

class StateVariableFilter4thOrder {
public:
    void setParameters(float fc, float q)
    {
        float rootQ = std::sqrt(q);
        stage1.setParameters(fc, rootQ);
        stage2.setParameters(fc, rootQ);
    }

    float process(float input, FilterMode mode)
    {
        stage1.process(input);
        float s1 = stage1.output(mode);
        stage2.process(s1);
        float s2 = stage2.output(mode);
        return s2;
    }

private:
    StateVariableFilter2ndOrder stage1, stage2;
};

// Zavalishin 2018, "The Art of VA Filter Design", Section 6.7, adopted from
// BogAudio Saturator: saturate input at around ~[-1, +1] with soft clipping
struct Saturator {
    static float process(float sample) { return sample < 0.f ? -saturation(-sample) : saturation(sample); }

private:
    static float saturation(float sample)
    {
        const float limit = 1.05f;
        const float y1 = 0.98765f; // (2*x - 1)/x**2 where x is 0.9.
        // correction so that saturation(0) = 0
        const float offset = 0.0062522; // -0.5f + sqrtf(0.5f * 0.5f) / y1;

        float x = sample / limit;
        float x1 = (x + 1.0f) * 0.5f;

        return limit * (offset + x1 - std::sqrt(x1 * x1 - y1 * x) * (1.0f / y1));
    }
};

// Rack dsp::TBiquadFilter<float>, HIGHPASS only (Rack's IIRFilter<3, 3>)
struct BiquadHighpass {
    float b[3] = { 1.f, 0.f, 0.f }, a[2] = { 0.f, 0.f };
    float x[2] = {}, y[2] = {};

    void setParameters(float f, float Q)
    {
        const float K = std::tan(M_PI * f);
        const float norm = 1.f / (1.f + K / Q + K * K);
        b[0] = norm;
        b[1] = -2.f * b[0];
        b[2] = b[0];
        a[0] = 2.f * (K * K - 1.f) * norm;
        a[1] = (1.f - K / Q + K * K) * norm;
    }
    void reset() { x[0] = x[1] = y[0] = y[1] = 0.f; }
    float process(float in)
    {
        float out = b[0] * in + b[1] * x[0] + b[2] * x[1] - a[0] * y[0] - a[1] * y[1];
        x[1] = x[0]; x[0] = in;
        y[1] = y[0]; y[0] = out;
        return out;
    }
};

// Creates a Butterworth 2*Nth order highpass filter for blocking DC
// (https://www.earlevel.com/main/2016/09/29/cascading-filters/)
struct DCBlocker {
    static constexpr int N = 2, order = 2 * N;

    void setFrequency(float fc)
    {
        const float poleInc = M_PI / order;
        const float firstAngle = poleInc / 2;
        for (int idx = 0; idx < N; idx++) {
            const float Q = 1.0f / (2.0f * std::cos(firstAngle + idx * poleInc));
            stage[idx].setParameters(fc, Q);
        }
    }
    void reset()
    {
        for (auto& s : stage) s.reset();
    }
    float process(float x)
    {
        for (auto& s : stage) x = s.process(x);
        return x;
    }

    BiquadHighpass stage[N];
};

} // namespace plethora
