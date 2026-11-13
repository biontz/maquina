// resampler_test - quality of the 44140 Hz -> host rate resampler.
// Feeds sines through Resampler and compares with the ideal sine at the output
// rate (output frame k == input position k*ratio - 1). Reports SNR per
// frequency, plus stopband rejection when the host rate is below 44140.
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdio>
#include <vector>

#include "Resampler.hpp"

using maquina::Resampler;

static double snr_db(double inRate, double outRate, double freq, double* gain_db)
{
    Resampler rs;
    rs.setup(inRate, outRate);
    const double amp = 16000.0;
    long n = 0;
    auto render = [&](int16_t* lr) {
        for (int i = 0; i < 64; ++i, ++n) {
            const int16_t v = int16_t(std::lround(amp * std::sin(2.0 * M_PI * freq * n / inRate)));
            lr[2 * i] = lr[2 * i + 1] = v;
        }
    };
    const uint32_t frames = uint32_t(outRate * 0.5);
    std::vector<float> L(frames), R(frames);
    rs.process(L.data(), R.data(), frames, render);
    double sig = 0, err = 0, out = 0;
    const double ratio = inRate / outRate;
    for (uint32_t k = 2000; k < frames; ++k) {
        const double ideal = amp / 32768.0 * std::sin(2.0 * M_PI * freq * (k * ratio - 1.0) / inRate);
        sig += ideal * ideal;
        err += (L[k] - ideal) * (L[k] - ideal);
        out += double(L[k]) * L[k];
    }
    if (gain_db) *gain_db = 10.0 * std::log10(out / sig);
    return 10.0 * std::log10(sig / err);
}

int main()
{
    const double in = 44140.0;
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    const double freqs[] = { 100.0, 1000.0, 5000.0, 10000.0, 16000.0, 19000.0, 20000.0 };
    int fail = 0;
    for (double out : rates) {
        printf("44140 -> %.0f Hz\n", out);
        for (double f : freqs) {
            double g;
            const double s = snr_db(in, out, f, &g);
            // flat passband to 19 kHz, at most 0.5 dB down at 20 kHz
            const bool ok = f <= 19000.0 ? (s > 80.0) : (g > -0.5);
            printf("  %6.0f Hz: SNR %6.1f dB, gain %+6.3f dB%s\n", f, s, g, ok ? "" : "  <-- FAIL");
            if (!ok) fail++;
        }
    }
    printf("%s\n", fail ? "FAILED" : "PASSED");
    return fail ? 1 : 0;
}
