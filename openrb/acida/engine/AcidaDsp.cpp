// AcidaDsp.cpp - see AcidaDsp.hpp. Every function follows its Rust original
// in openrb-338/crates/orb-engine/src (named in the comments) operation for
// operation; the addresses in the comments are the original DLL's, as there.
// Built with -ffp-contract=off, without -ffast-math (openrb/Makefile).
// SPDX-License-Identifier: GPL-3.0-only
#include "AcidaDsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#include "PcfPatterns.generated.hpp"

#if defined(__FAST_MATH__)
#error "AcidaDsp.cpp must not be built with -ffast-math (bit-exact port)"
#endif

namespace maquina::acida {

namespace {

double f64b(uint64_t bits)
{
    double d;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}
float f32b(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
inline float st(double x) { return float(x); }

// Rust's saturating float -> int conversion (`as i64`): NaN gives 0
inline int64_t trunc64(double x)
{
    if (x != x) return 0;
    if (x >= 9.2233720368547758e18) return INT64_MAX;
    if (x <= -9.2233720368547758e18) return INT64_MIN;
    return int64_t(x);
}

} // namespace

// ===========================================================================
// tables/mod.rs

namespace {

const float LEVEL_STEP = 0.041666668f; // data::LEVEL_STEP

void levelCurve(float (&t)[128])
{
    std::fill(std::begin(t), std::end(t), 0.f);
    t[127] = 1.f;
    const double step = double(LEVEL_STEP);
    for (int i = 126; i >= 1; --i) {
        const double next = double(t[i + 1]);
        t[i] = st(next - step * next);
    }
    t[0] = 0.f;
}

void panTable(const float (&t)[128], float (&pan)[128][128][2])
{
    for (int a = 0; a < 128; ++a)
        for (int b = 0; b < 128; ++b) {
            const int d = b - 64;
            const int q = (d * d) >> 5;
            const int cut = std::clamp(a - q, 0, 127);
            const int l = d > 0 ? cut : a;
            const int r = d < 0 ? cut : a;
            pan[a][b][0] = t[l];
            pan[a][b][1] = t[r];
        }
}

void buildTb303Pitch(float (&tab)[6144])
{
    const double PITCH_SEMI_EXP = f64b(0x3FB5555555555555ull);
    const double C_10153AB0 = f64b(0x3EF7C6F8C751F177ull);
    const double PITCH_STEP_EXP = f64b(0x3F55555555555555ull);
    const double s = std::pow(2.0, PITCH_SEMI_EXP);
    const double cube = (1.0 * s) * (s * s);
    const double v18 = (cube * 55.0) * 2.0;
    const double p = std::pow(2.0, PITCH_STEP_EXP);
    const double k = (v18 * C_10153AB0) / std::pow(p, 3072.0);
    for (int i = 0; i < 6144; ++i) tab[i] = st(std::pow(p, double(i)) * k);
}

void buildTb303Filter(float (&tab)[8][1024][4])
{
    const double FILT_SPAN_EXP = f64b(0x3F50040100401004ull);
    const double Q_BASE = f64b(0x3FFC7AE140000000ull);
    const double C_101539C8 = 0.4, C_101539D8 = 0.52;
    const float C_10153A58 = f32b(0x3E124925u);
    const double C_101539E0 = f64b(0x3FC5555555555555ull);
    const double C_101539E8 = 0.5, C_101539F0 = 0.6, C_101539F8 = 128.0, C_10153A00 = 1.0 / 1024.0,
                 C_10153A08 = 0.000125, C_10153A10 = 0.3;
    const float C_10153A5C = 1.f, C_10153A60 = 0.f;
    const double C_10153A18 = 1.0;
    const double C_10153A20 = f64b(0x401921FB54524550ull);
    const double C_101539A8 = 1.0;
    const double C_10153A28 = f64b(0xC00921FB54524550ull);
    const double C_10153A30 = 2.0;
    const double C_10153A38 = f64b(0xC01921FB54524550ull);
    const double C_10153A40 = 0.85, C_10153A48 = 0.15;
    const float C_10153A64 = 4.f, C_10153A68 = 3.f;
    const double C_10153A50 = 0.8;

    const double k = std::pow(1602.0, FILT_SPAN_EXP);
    const double base = C_101539C8 / std::pow(k, 1023.0);
    for (int r = 0; r < 8; ++r) {
        const double rf = double(r);
        const double pm = std::pow(Q_BASE, rf) * C_101539D8;
        const double t = rf * double(C_10153A58);
        const double m94 = rf * C_101539E0 + C_101539E8;
        const double aw = t * C_101539C8 + C_101539F0;
        for (int i = 0; i < 1024; ++i) {
            const double fi = double(i);
            const double w = (std::pow(k, fi) * base) * aw;
            const double u = (fi * C_101539F8) * C_10153A00;
            double q = ((((u * fi) * C_101539F8) * C_10153A00) * C_10153A08) - C_10153A10;
            if (q > double(C_10153A5C)) q = double(C_10153A5C);
            if (q < double(C_10153A60)) q = double(C_10153A60);
            const double d = q * pm + (C_10153A18 - q) * m94;
            const double invD = C_101539A8 / d;
            const double cs = std::cos(w * C_10153A20);
            const double e1 = std::exp((w * C_10153A28) * invD);
            const double c0 = e1 * (cs * C_10153A30);
            const double w2 = w * C_10153A38;
            const double c1 = std::exp(w2 * invD);
            const double oneC0 = C_10153A18 - c0;
            const double c3 = std::exp((t * C_10153A40 + C_10153A48) * w2);
            const double num = (C_10153A18 - c3) * (c1 + oneC0);
            const double dq = d > double(C_10153A64) ? double(C_10153A68) : d;
            const double c2 = num / ((dq + double(C_10153A5C)) * C_10153A50);
            tab[r][i][0] = st(c0);
            tab[r][i][1] = st(c1);
            tab[r][i][2] = st(c2);
            tab[r][i][3] = st(c3);
        }
    }
}

Smoother smoother()
{
    const double W = f64b(0x3FB015BF8C180485ull);
    const double HALF_NEG_W = f64b(0xBFA015BF8C180485ull);
    const double NEG_W = f64b(0xBFB015BF8C180485ull);
    const double cs = std::cos(W);
    const double e1 = std::exp(HALF_NEG_W);
    const double a = e1 * (cs * 2.0);
    const double b2 = std::exp(NEG_W);
    const double g = (1.0 - a) + b2;
    return { st(b2), st(a), st(g) };
}

void pcfFilter(bool bandpass, float (&tab)[8][512][3])
{
    const double PCF_K = f64b(0x3FF03B93E0000000ull);
    const double Q_BASE = f64b(0x3FFC7AE140000000ull);
    const double c04 = 0.4, c052 = 0.52;
    const float c17 = f32b(0x3E124925u);
    const double c16 = f64b(0x3FC5555555555555ull);
    const double c05 = 0.5, c06 = 0.6, c128 = 128.0, c512 = 1.0 / 512.0, cq = 0.000125, c03 = 0.3;
    const float oneF = 1.f, zeroF = 0.f;
    const double one = 1.0, oneD = 1.0;
    const double twoPi = f64b(0x401921FB54524550ull);
    const double negPi = f64b(0xC00921FB54524550ull);
    const double two = 2.0;
    const double negTwoPi = f64b(0xC01921FB54524550ull);
    const float fourF = 4.f, threeF = 3.f;
    const double c08 = 0.8;
    const float C_101523CC = 2.f;
    const double C_10152468 = f64b(0x400921FB54524550ull);

    const double base = c04 / std::pow(PCF_K, 511.0);
    for (int r = 0; r < 8; ++r) {
        const double rf = double(r);
        const double pm = std::pow(Q_BASE, rf) * c052;
        const double t = rf * double(c17);
        const double m94 = rf * c16 + c05;
        const double aw = t * c04 + c06;
        for (int i = 0; i < 512; ++i) {
            const double fi = double(i);
            const double w = (std::pow(PCF_K, fi) * base) * aw;
            const double u = (fi * c128) * c512;
            double q = ((((u * fi) * c128) * c512) * cq) - c03;
            if (q > double(oneF)) q = double(oneF);
            if (q < double(zeroF)) q = double(zeroF);
            const double d = q * pm + (one - q) * m94;
            const double invD = oneD / d;
            const double cs = std::cos(w * twoPi);
            const double e1 = std::exp((w * negPi) * invD);
            const double c0 = e1 * (cs * two);
            const double c1 = std::exp((w * negTwoPi) * invD);
            double num;
            if (bandpass) {
                const double cosPi = std::cos(w * C_10152468);
                num = (cosPi * std::sqrt((double(oneF) - c0) + c1)) * double(threeF);
            } else {
                num = (c1 + (one - c0)) * double(C_101523CC);
            }
            const double dq = d > double(fourF) ? double(threeF) : d;
            const double c2 = num / ((dq + double(oneF)) * c08);
            tab[r][i][0] = st(c0);
            tab[r][i][1] = st(c1);
            tab[r][i][2] = st(c2);
        }
    }
}

void buildDistShaper(float (&tab)[32][256])
{
    const float C_10151BFC = 0.f;
    const float C_10151C00 = f32b(0x3A88642Au);
    const float C_10151C04 = f32b(0x3EC90FD0u);
    const float C_10151C08 = f32b(0x3B490FD0u);
    const float C_10151C0C = 256.f;
    const double C_10151BE0 = 1.0, C_10151BF0 = 1.0 / 128.0, C_10151BE8 = 0.7;
    for (int t = 0; t < 32; ++t) {
        float* row = tab[t];
        std::fill(row, row + 256, 0.f);
        const double tt = double(t * t);
        const double inc = (tt * double(C_10151C00)) * double(C_10151C04) + double(C_10151C08);
        double ph = double(C_10151BFC), y = double(C_10151BFC), z = double(C_10151BFC);
        for (int e = 128; e < 256; ++e) {
            const double s = std::sin(ph) * double(C_10151C0C);
            const double v = ((s * (C_10151BE0 - y)) + ((y * z) * double(C_10151C0C))) * C_10151BE8;
            row[e] = st(v);
            row[256 - e] = st(-v);
            y += C_10151BF0;
            z += C_10151BF0;
            ph += inc;
        }
        row[0] = -row[255];
    }
}

} // namespace

const Tables& Tables::get()
{
    static const std::unique_ptr<Tables> t = [] {
        std::unique_ptr<Tables> p(new Tables());
        levelCurve(p->level);
        panTable(p->level, p->pan);
        buildTb303Pitch(p->tb303Pitch);
        buildTb303Filter(p->tb303Filter);
        pcfFilter(false, p->pcfLowpass);
        pcfFilter(true, p->pcfBandpass);
        buildDistShaper(p->distShaper);
        p->tb303Smoother = smoother();
        p->pcfSmoother = smoother();
        return p;
    }();
    return *t;
}

// ===========================================================================
// tb303.rs

namespace {

const uint8_t DEFAULT_PARAMS[12] = { 1, 0, 0x40, 0x14, 0x5A, 0x14, 0x64, 0x40, 0, 0, 0, 0 };
const int16_t TUNE_TABLE[32] = { -12, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 0, 0,
                                 0,   0,   0,   1,   2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 12 };
constexpr int SEMIS_MIN = -48, SEMIS_MAX = 47;

struct Tb303Consts {
    double ACC_RESO_SCALE = f64b(0x3F14A4299AE3DC33ull);
    double ACC_RESO_BASE = f64b(0x3FEFAE147AE147AEull);
    double ACC_RESO_MUL = f64b(0x3FEFF7CED916872Bull);
    double ACC_AMOUNT_SCALE = f64b(0x3F983060C183060Cull);
    double DECAY_ONE = 1.0;
    double DECAY_SPAN = f64b(0x3F33A92A30553261ull);
    double DECAY_BASE = f64b(0x3FEFFFEB074A771Dull);
    float DECAY_SCALE = f32b(0x3C010204u);
    float ACC_DECAY = f32b(0x3F7FEBAFu);
    float AMP_VINTAGE_IN = f32b(0x3DCCCCCDu), AMP_VINTAGE_FB = f32b(0x3F666666u);
    float AMP_OFF_20_IN = f32b(0x3D810625u), AMP_OFF_20_FB = f32b(0x3F6FDF3Bu);
    double ONE = 1.0;
    double CUT_SCALE = f64b(0x3F62B4C5B45E7F8Eull);
    double CUT_BASE = f64b(0x3FDD70A3D70A3D71ull);
    double EM_SCALE = f64b(0x3F6882F167EE957Dull);
    double EM_BASE = f64b(0x3FB47AE147AE147Bull);
    double KA_SLOPE = f64b(0x3FB1111111111111ull);
    double KA_BASE = f64b(0x3FBA06D3A06D3A07ull);
    double ENV_BIAS = f64b(0x3FD999999999999Aull);
    float OFFSET = f32b(0x358637BDu);
    float RESO_VINTAGE = f32b(0x3C010204u);
    double RESO_20 = f64b(0x3F80204081020408ull);
    float RESO_ROW = f32b(0x3C00FEB7u);
    float ROWS = 7.f;
    float C_MAX = 1.f, C_MIN = 0.f, C_SCALE = 1023.f;
    double RAMP = f64b(0x3FA0000000000000ull);
    float PHASE_MAX = 1.f, PHASE_WRAP = 2.f;
    float DC = f32b(0x3F7C60E3u);
    float SQ_THRESH = f32b(0x3D3B8000u), SQ_HI = f32b(0x3F0CA000u), SQ_LO = f32b(0xBF0CA000u);
};
const Tb303Consts K303;

float squareN(float x, int n)
{
    double v = double(x);
    for (int i = 0; i < n; ++i) v = v * v;
    return st(v);
}

double clampC(double c)
{
    c = c > double(K303.C_MAX) ? double(K303.C_MAX) : c;
    return c < double(K303.C_MIN) ? double(K303.C_MIN) : c;
}

} // namespace

float Tb303::Voice::sample(double inc, bool square, const Coefs& k)
{
    double p = ph + inc;
    if (p > double(K303.PHASE_MAX)) p -= double(K303.PHASE_WRAP);
    ph = p;
    const double x = !square ? p : p <= double(K303.SQ_THRESH) ? double(K303.SQ_LO) : double(K303.SQ_HI);
    s = s * k.c3 + x;
    const double y = (s * k.g + y1 * k.c0) - y2 * k.c1;
    o = (y - y1) + o * double(K303.DC);
    y2 = y1;
    y1 = y;
    return st(o);
}

Tb303::Tb303() { std::memcpy(live, DEFAULT_PARAMS, sizeof live); }

bool Tb303::keySemis(uint8_t note, int& semis) const
{
    semis = int(note) - kMidiC;
    const int tune = TUNE_TABLE[live[TUNE] >> 2];
    const int x = tune + semis;
    return x >= SEMIS_MIN && x <= SEMIS_MAX;
}

void Tb303::keyOn(uint8_t note, uint8_t velocity, uint8_t accentVelocity)
{
    int semis;
    if (!keySemis(note, semis)) return;
    const bool accent = velocity >= accentVelocity;
    const bool legato = heldLen > 0;
    if (heldLen == kHeld) {
        std::memmove(held, held + 1, sizeof(Held) * (kHeld - 1));
        --heldLen;
    }
    held[heldLen] = { note, accent };
    ++heldLen;
    keyPlay(semis, accent, legato);
}

void Tb303::keyPlay(int semis, bool accent, bool legato)
{
    slide = legato;
    noteOnSemis(semis, 0, accent ? 1 : 0, 0);
}

void Tb303::keyOff(uint8_t note)
{
    const int n = heldLen;
    int i = 0;
    while (i < n && held[i].note != note) ++i;
    if (i == n) return;
    std::memmove(held + i, held + i + 1, sizeof(Held) * size_t(n - i - 1));
    --heldLen;
    if (i + 1 < n) return;
    while (heldLen > 0) {
        const Held h = held[heldLen - 1];
        int semis;
        if (keySemis(h.note, semis)) {
            keyPlay(semis, h.accent, true);
            return;
        }
        --heldLen;
    }
    noteOff();
}

void Tb303::noteOff()
{
    if (slide) return;
    gate = 0.f;
    if (vintage) {
        ampIn = K303.AMP_VINTAGE_IN;
        ampFb = K303.AMP_VINTAGE_FB;
    } else {
        ampIn = K303.AMP_OFF_20_IN;
        ampFb = K303.AMP_OFF_20_FB;
    }
}

void Tb303::noteOnSemis(int semis, uint8_t slideFlag, uint8_t accent, uint8_t rest)
{
    const int n = vintage ? 2 : 6;
    const float c = st((double(live[RESONANCE]) * K303.ACC_RESO_SCALE + K303.ACC_RESO_BASE) * K303.ACC_RESO_MUL);
    accCoef = squareN(c, n);
    if (accent == 0) {
        accAmount = 0.f;
        const double d = K303.DECAY_ONE - double(live[DECAY]) * double(K303.DECAY_SCALE);
        envDecay = st(K303.DECAY_BASE - d * d * K303.DECAY_SPAN);
    } else {
        envDecay = K303.ACC_DECAY;
        accAmount = st(double(live[ACCENT]) * K303.ACC_AMOUNT_SCALE);
    }
    envDecay = squareN(envDecay, n);
    if (rest == 0) {
        gate = 1.f;
        if (vintage) {
            ampIn = K303.AMP_VINTAGE_IN;
            ampFb = K303.AMP_VINTAGE_FB;
        } else {
            ampIn = 1.f;
            ampFb = 0.f;
        }
        if (!slide) {
            env = 1.f;
            retrig = true;
        }
    }
    const int tune = TUNE_TABLE[live[TUNE] >> 2];
    pitchTarget = uint32_t(((tune << 6) + (semis << 6) + 0xC00) << 4);
    if (!slide) pitch = pitchTarget;
    slide = slideFlag != 0;
}

void Tb303::silence()
{
    slide = false;
    noteOff();
    heldLen = 0;
}

void Tb303::reset()
{
    slide = false;
    pitchTarget = pitch = 0;
    accCoef = 0.f;
    o = s = y2 = y1 = 0.f;
    amp = gate = ampFb = ampIn = 0.f;
    accEnv = accAmount = envDecay = env = phase = 0.f;
    cs2 = cs1 = 0.5f;
    silence();
}

double Tb303::glide(const Tables& t)
{
    pitch = (pitch * 11u + pitchTarget) / 12u;
    return double(t.tb303Pitch[pitch >> 4]);
}

double Tb303::smoothCutoff(const Tables& t)
{
    const Smoother& k = t.tb303Smoother;
    const double cut = double(live[CUTOFF]) * K303.CUT_SCALE + K303.CUT_BASE;
    const double cs = (double(k.a) * double(cs1) + cut * double(k.g)) - double(k.b2) * double(cs2);
    cs2 = cs1;
    cs1 = st(cs);
    return cs;
}

Tb303::Voice Tb303::voice() const { return { double(phase), double(s), double(y1), double(y2), double(o) }; }

void Tb303::storeVoice(const Voice& v)
{
    phase = st(v.ph);
    s = st(v.s);
    y1 = st(v.y1);
    y2 = st(v.y2);
    o = st(v.o);
}

void Tb303::interpolate(const Tables& t, float c, double rf, float out[4])
{
    const size_t idx = size_t(int32_t(trunc64(double(c) * double(K303.C_SCALE))));
    const double r7 = rf * double(K303.ROWS);
    const size_t l = size_t(uint16_t(trunc64(r7)));
    const double frac = r7 - double(l);
    const float* lo = t.tb303Filter[l][idx];
    const float* hi = t.tb303Filter[l + 1][idx];
    for (int k = 0; k < 4; ++k) {
        const double d = double(hi[k]) - double(lo[k]);
        out[k] = st(double(lo[k]) + d * frac);
    }
}

void Tb303::process20(Block& bus, const Tables& t)
{
    const double cs = smoothCutoff(t);
    const double inc = glide(t);
    const double amp0 = double(amp) + double(K303.OFFSET);
    amp = st(amp0);
    const double ampV = amp0 * double(ampFb) + double(gate) * double(ampIn);
    const double e0 = double(K303.OFFSET) + double(env);
    env = st(e0);
    const double ac = double(accCoef);
    const double acc = ac * double(accEnv) + e0 * ((K303.ONE - ac) * double(accAmount));
    accEnv = st(acc);
    const double g = ampV * (acc + K303.ONE);
    const double em = double(live[ENVMOD]) * K303.EM_SCALE + K303.EM_BASE;
    const double r = double(live[RESONANCE]) * K303.RESO_20;
    const double ka = K303.KA_BASE - r * K303.KA_SLOPE;
    const double acc32 = double(accEnv);
    const double rf = double(live[RESONANCE]) * double(K303.RESO_ROW);
    if (retrig) {
        const double c = clampC(ka * acc32 + ((e0 - K303.ENV_BIAS) * em + cs));
        float k4[4];
        interpolate(t, st(c), rf, k4);
        coefs[0] = k4[0];
        coefs[1] = k4[1];
        coefs[2] = 0.f;
        coefs[3] = k4[3];
        retrig = false;
    }
    const double e1 = e0 * double(envDecay);
    const double c = clampC(ka * acc32 + ((e1 - K303.ENV_BIAS) * em + cs));
    float n[4];
    interpolate(t, st(c), rf, n);
    const double k0 = coefs[0], k1 = coefs[1], k2 = coefs[2], k3 = coefs[3];
    const double d2 = (double(n[2]) * g - k2) * K303.RAMP;
    const double d0 = (double(n[0]) - k0) * K303.RAMP;
    const double d1 = (double(n[1]) - k1) * K303.RAMP;
    const double d3 = (double(n[3]) - k3) * K303.RAMP;
    Coefs k { k0, k1, k2, k3 };
    const bool square = live[WAVEFORM] != 0;
    Voice v = voice();
    for (int i = 0; i < kBlock; i += 2) {
        bus[i] = v.sample(inc, square, k);
        bus[i + 1] = v.sample(inc, square, k);
        k.c0 += d0;
        k.c1 += d1;
        k.g += d2;
        k.c3 += d3;
    }
    coefs[0] = st(k.c0);
    coefs[1] = st(k.c1);
    coefs[2] = st(k.g);
    coefs[3] = st(k.c3);
    storeVoice(v);
    env = st(e1);
    amp = st(ampV);
}

void Tb303::processVintage(Block& bus, const Tables& t)
{
    const double r = double(live[RESONANCE]) * double(K303.RESO_VINTAGE);
    const double cs = smoothCutoff(t);
    const double inc = glide(t);
    double ampV = double(amp) + double(K303.OFFSET);
    amp = st(ampV);
    double e = double(K303.OFFSET) + double(env);
    env = st(e);
    const double ka = K303.KA_BASE - r * K303.KA_SLOPE;
    const double em = double(live[ENVMOD]) * K303.EM_SCALE + K303.EM_BASE;
    const double accIn = (K303.ONE - double(accCoef)) * double(accAmount);
    const double ac = double(accCoef);
    const double decay = double(envDecay);
    const auto& row = t.tb303Filter[live[RESONANCE] >> 4];
    const bool square = live[WAVEFORM] != 0;
    Voice v = voice();
    for (int q = 0; q < kBlock; q += 4) {
        const double e1 = e * decay;
        const double acc = ac * double(accEnv) + e * accIn;
        accEnv = st(acc);
        ampV = ampV * double(ampFb) + double(gate) * double(ampIn);
        const double c = clampC(((e1 - K303.ENV_BIAS) * em + cs) + acc * ka);
        const double g = (acc + K303.ONE) * ampV;
        e = e1;
        const float* f = row[size_t(int32_t(trunc64(c * double(K303.C_SCALE))))];
        const Coefs k { double(f[0]), double(f[1]), g * double(f[2]), double(f[3]) };
        for (int i = 0; i < 4; ++i) bus[q + i] = v.sample(inc, square, k);
    }
    storeVoice(v);
    env = st(e);
    amp = st(ampV);
}

void Tb303::process(Block& bus, const Tables& t)
{
    if (vintage)
        processVintage(bus, t);
    else
        process20(bus, t);
}

// ===========================================================================
// dist.rs

namespace {

struct DistConsts {
    float GRID[5][5] = {
        { f32b(0x3F99999Au), f32b(0x3ECCCCCDu), f32b(0x3E8F5C29u), f32b(0x3E4CCCCDu), f32b(0x3E19999Au) },
        { f32b(0x3EB33333u), f32b(0x3E4CCCCDu), f32b(0x3E19999Au), f32b(0x3E0F5C29u), f32b(0x3DF5C28Fu) },
        { f32b(0x3E4CCCCDu), f32b(0x3E19999Au), f32b(0x3E051EB8u), f32b(0x3DF5C28Fu), f32b(0x3DF5C28Fu) },
        { f32b(0x3E19999Au), f32b(0x3DF5C28Fu), f32b(0x3DE147AEu), f32b(0x3DCCCCCDu), f32b(0x3DCCCCCDu) },
        { f32b(0x3DF5C28Fu), f32b(0x3DCCCCCDu), f32b(0x3DA3D70Au), f32b(0x3DA3D70Au), f32b(0x3DA3D70Au) },
    };
    double GRID_SCALE = f64b(0x400FFDF3B645A1CBull);
    double IN_OFFSET = 0.0625, IN_SCALE = 2048.0;
    double OUT_SCALE = 5.0, OUT_SCALE2 = 0.00390625;
    float C127 = f32b(0x3C010204u);
    double ONE = 1.0;
    double KEEP = f64b(0x3FECCCCCCCCCCCCDull), TAKE = f64b(0x3FB999999999999Aull);
    double CLIP_SCALE = 16.0, CLIP_OFFSET = 1.0, NEG = -1.0;
    uint8_t CLIP_KNEE = 90;
    float SHAPER_LO = 0.f, SHAPER_MID = 128.f, SHAPER_HI = 254.f;
};
const DistConsts KD;

double distCoord(uint8_t x) { return (double(x) * double(KD.C127)) * KD.GRID_SCALE; }

double distGrid(uint8_t shape, uint8_t amount)
{
    const double s = distCoord(shape), a = distCoord(amount);
    const size_t i = size_t(uint16_t(trunc64(s)));
    const size_t j = size_t(uint16_t(trunc64(a)));
    const double fs = s - double(i), fa = a - double(j);
    auto g = [](size_t r, size_t c) { return double(KD.GRID[r][c]); };
    const double top = (g(i + 1, j + 1) - g(i + 1, j)) * fa + g(i + 1, j);
    const double bot = (g(i, j + 1) - g(i, j)) * fa + g(i, j);
    return fs * (top - bot) + bot;
}

double distShape(const float* w, double v)
{
    if (v < double(KD.SHAPER_LO)) return double(w[0]);
    if (v > double(KD.SHAPER_HI)) return double(w[254]);
    const size_t i = size_t(trunc64(v));
    return (v - double(i)) * (double(w[i + 1]) - double(w[i])) + double(w[i]);
}

float distSmooth(float gain, float target) { return st(double(gain) * KD.KEEP + double(target) * KD.TAKE); }

} // namespace

void Distortion::setParam(uint8_t ctrl, uint8_t value)
{
    if (ctrl == ENABLED)
        live[ENABLED] = value;
    else if (ctrl == AMOUNT || ctrl == SHAPE)
        live[ctrl] = std::min<uint8_t>(value, 127);
}

void Distortion::paramUpdate(const Tables& t)
{
    const uint8_t shape = live[SHAPE], amount = live[AMOUNT];
    clip = shape == 0;
    targetIn = st((double(t.level[amount]) + KD.IN_OFFSET) * KD.IN_SCALE);
    row = uint16_t(shape >> 2);
    targetOut = st(distGrid(shape, amount) * KD.OUT_SCALE * KD.OUT_SCALE2);
}

void Distortion::process(Block& bus, const Tables& t)
{
    if (live[ENABLED] == 0) return;
    if ((counter & 15) == 0) paramUpdate(t);
    counter = uint16_t(counter + 1);
    gainIn = distSmooth(gainIn, targetIn);
    gainOut = distSmooth(gainOut, targetOut);
    if (clip) {
        const uint8_t a = live[AMOUNT];
        const double g = KD.CLIP_SCALE * double(t.level[a]) + KD.CLIP_OFFSET;
        const double h = a < KD.CLIP_KNEE ? g : KD.CLIP_SCALE * double(t.level[KD.CLIP_KNEE]) + KD.CLIP_OFFSET;
        const double th = KD.ONE / h;
        const double nth = th * KD.NEG;
        for (float& x : bus) {
            const double y = g * double(x);
            x = st(y > th ? th : y < nth ? nth : y);
        }
    } else {
        const float* w = t.distShaper[row];
        const double gi = gainIn, go = gainOut;
        for (float& x : bus) {
            const double v = gi * double(x) + double(KD.SHAPER_MID);
            x = st(distShape(w, v) * go);
        }
    }
}

// ===========================================================================
// pcf.rs

namespace {

struct PcfConsts {
    double ONE = 1.0;
    float ZERO = 0.f;
    float FREQ_SCALE = 512.f, FREQ_SCALE2 = f32b(0x3C000000u);
    float TOP = f32b(0x43FF8000u);
    float SLOW_KEEP = f32b(0x3F7EB852u), SLOW_TAKE = f32b(0x3BA3D700u);
    float INSTANT_TAKE = 1.f;
    float DECAY[16] = { 0.99f, 0.993f, 0.995f, 0.996f, 0.997f, 0.998f, 0.999f, 0.9993f,
                        0.9995f, 0.9996f, 0.9997f, 0.9998f, 0.9999f, 0.99993f, 0.99995f, 1.0f };
};
const PcfConsts KP;

size_t pcfEnvIndex(double env)
{
    const double v = env < double(KP.ZERO) || env != env ? double(KP.ZERO) : env > double(KP.TOP) ? double(KP.TOP) : env;
    return size_t(trunc64(v));
}

} // namespace

void Pcf::setParam(uint8_t ctrl, uint8_t value)
{
    if (ctrl == WAVE)
        live[WAVE] = std::min<uint8_t>(value, kPcfWaves - 1);
    else if (ctrl <= MODE)
        live[ctrl] = value;
}

void Pcf::process(Block& bus, const Tables& t, int32_t tick)
{
    if (live[ENABLED] == 0) return;
    const uint8_t wave = live[WAVE];
    const uint16_t len = kPcfLengths[wave], res = kPcfResolutions[wave];
    if (res == 32)
        quotient = tick / 24;
    else if (res == 16)
        quotient = tick / 48;
    else if (res == 8)
        quotient = tick / 96;
    const Smoother& k = t.pcfSmoother;
    const double f = (double(live[FREQUENCY]) * double(KP.FREQ_SCALE)) * double(KP.FREQ_SCALE2) * double(k.g);
    const double s = (double(k.a) * double(smooth[0]) + f) - double(k.b2) * double(smooth[1]);
    smooth[1] = smooth[0];
    if (len == 0) return; // not reachable with waves 0..54
    const int32_t stp = quotient % int32_t(len);
    smooth[0] = st(s);
    base = st(s);
    double e = double(trigger);
    double keep = double(attack), take = KP.ONE - double(attack);
    if (stp != step) {
        step = stp;
        decay = KP.DECAY[std::min(live[DECAY] >> 3, 15)];
        const uint8_t v = stp >= 0 && stp < 32 ? kPcfSteps[wave][stp] : 0;
        if (v != 0) {
            const double raw = double((int(v) * int(live[AMOUNT])) >> 5);
            e = raw + double(base) > double(KP.TOP) ? double(KP.TOP) - double(base) : raw;
            if (v & 1) {
                attack = KP.SLOW_KEEP;
                keep = double(KP.SLOW_KEEP);
                take = double(KP.SLOW_TAKE);
            } else {
                attack = 0.f;
                keep = double(KP.ZERO);
                take = double(KP.INSTANT_TAKE);
            }
        }
    }
    double envV = double(env);
    const size_t row = std::min<size_t>((size_t(live[RESONANCE]) * 8) >> 7, 7);
    const bool lowpass = live[MODE] == 1;
    const auto& tab = lowpass ? t.pcfLowpass[row] : t.pcfBandpass[row];
    const float* state = lowpass ? lp : bp;
    double fy1 = state[0], fy2 = state[1];
    const double dec = double(decay), b = double(base);
    for (int g = 0; g < kBlock / 4; ++g) {
        e *= dec;
        envV = envV * keep + (e + b) * take;
        const float* c = tab[pcfEnvIndex(envV)];
        const double c0 = c[0], c1 = c[1], c2 = c[2];
        for (int i = 4 * g; i < 4 * g + 4; ++i) {
            const double prev = fy1;
            const double y = (double(bus[i]) * c2 + c0 * fy1) - c1 * fy2;
            fy2 = fy1;
            fy1 = y;
            bus[i] = st(lowpass ? y : y - prev);
        }
    }
    const float sv[2] = { st(fy1), st(fy2) };
    if (lowpass) {
        lp[0] = sv[0];
        lp[1] = sv[1];
        bp[0] = bp[1] = 0.f;
    } else {
        bp[0] = sv[0];
        bp[1] = sv[1];
        lp[0] = lp[1] = 0.f;
    }
    trigger = st(e);
    env = st(envV);
}

// ===========================================================================
// comp.rs

namespace {

struct CompConsts {
    float ATTACK = f32b(0x3C23D70Au);
    float RELEASE = f32b(0x3F7F9724u);
    uint16_t HOLD = 300;
    float C127 = f32b(0x3C010204u);
    double OFF_SCALE = f64b(0x3FEFAE147AE147AEull);
    double ONE = 1.0, TH_SCALE = 0.03125, X_SCALE = 0.3662109375;
    float FLOOR_SCALE = 80000.f;
    double FLOOR_SCALE2 = 1.0 / 32768.0, FLOOR_OFFSET = 0.01220703125;
    double RAMP = 0.25;
    float ZERO = 0.f;
    float GUARD = f32b(0x358637BDu);
};
const CompConsts KC;

double compEnvelope(double env, double s, uint16_t& hold, float floor)
{
    double e;
    if (s > env) {
        hold = KC.HOLD;
        e = env + (s - env) * double(KC.ATTACK);
    } else if (hold == 0) {
        e = env * double(KC.RELEASE) + double(KC.GUARD);
    } else {
        --hold;
        e = env;
    }
    return e < double(floor) || e != e ? double(floor) : e;
}

double compDetect(double s) { return s < double(KC.ZERO) || s != s ? -s : s; }

} // namespace

bool Compressor::begin()
{
    if (live[ENABLED] == 0) return false;
    if ((counter & 15) == 0) {
        const double af = double(live[AMOUNT]) * double(KC.C127);
        const double th = double(live[THRESHOLD]);
        const double offV = KC.ONE - af * KC.OFF_SCALE;
        const double xV = (af * (th * KC.TH_SCALE + KC.ONE)) * KC.X_SCALE;
        const double fl = ((th * double(KC.C127)) * double(KC.FLOOR_SCALE)) * KC.FLOOR_SCALE2 + KC.FLOOR_OFFSET;
        x = st(xV);
        off = st(offV);
        floor = st(fl);
    }
    counter = uint16_t(counter + 1);
    return true;
}

void Compressor::run(Block* const* buses, int n)
{
    const double xV = x, offV = off;
    double e = env, g = gain;
    for (int i = 0; i < kBlock; i += 4) {
        double s = double((*buses[0])[i + 1]) + double((*buses[0])[i + 3]);
        for (int b = 1; b < n; ++b) s = s + double((*buses[b])[i + 1]) + double((*buses[b])[i + 3]);
        e = compEnvelope(e, compDetect(s), hold, floor);
        const double d = ((xV / e + offV) - g) * KC.RAMP;
        for (int k = 0; k < 4; ++k) {
            g += d;
            for (int b = 0; b < n; ++b) (*buses[b])[i + k] = st(double((*buses[b])[i + k]) * g);
        }
    }
    env = st(e);
    gain = st(g);
}

void Compressor::process(Block& bus)
{
    if (!begin()) return;
    Block* b[1] = { &bus };
    run(b, 1);
}

void Compressor::processStereo(Block& l, Block& r)
{
    if (!begin()) return;
    Block* b[2] = { &l, &r };
    run(b, 2);
}

// ===========================================================================
// delay.rs

namespace {

struct DelayConsts {
    float OFFSET = f32b(0x358637BDu);
    double FB_SCALE = f64b(0x3F80204081020408ull);
    double KEEP = f64b(0x3FECCCCCCCCCCCCDull), TAKE = f64b(0x3FB999999999999Aull);
    uint32_t STEP_SAMPLES = 26460000u;
    uint32_t MAX_LENGTH = 0x1FFFFu;
};
const DelayConsts KDL;

} // namespace

uint32_t delayLength(uint32_t tempo, uint8_t steps, uint8_t mode)
{
    const int32_t t = int32_t(tempo);
    const int32_t q = mode == 0 ? int32_t(uint32_t(t) * 3u) / 100 : int32_t(uint32_t(t) * 4u) / 100;
    const uint32_t n = KDL.STEP_SAMPLES * uint32_t(steps);
    const uint32_t uq = uint32_t(q);
    if (uq == 0) return KDL.MAX_LENGTH;
    return std::min(n / uq, KDL.MAX_LENGTH);
}

Delay::Delay() : line(new float[kLen]())
{
}

void Delay::setParam(uint8_t ctrl, uint8_t value)
{
    if (ctrl == PAN)
        live[PAN] = std::min<uint8_t>(value, 127);
    else if (ctrl <= FEEDBACK)
        live[ctrl] = value;
}

void Delay::clear()
{
    std::fill(line.get(), line.get() + kLen, kGuard);
    lineGate = true;
}

void Delay::paramUpdate(const Tables& t, uint32_t tempo)
{
    length = delayLength(tempo, live[STEPS], live[MODE]);
    const float* pg = t.pan[127][live[PAN]];
    target[0] = st(double(KDL.OFFSET) + double(pg[0]));
    target[1] = st(double(KDL.OFFSET) + double(pg[1]));
    feedback = st(double(live[FEEDBACK]) * KDL.FB_SCALE);
}

void Delay::process(Block& send, Block& l, Block& r, const Tables& t, uint32_t tempo)
{
    if (live[ENABLED] == 0) {
        if (!lineGate) clear();
        std::fill(std::begin(send), std::end(send), kGuard);
        return;
    }
    lineGate = false;
    if ((counter & 15) == 0) paramUpdate(t, tempo);
    counter = uint16_t(counter + 1);
    const double gl = double(gain[0]) * KDL.KEEP + double(target[0]) * KDL.TAKE;
    const double gr = double(gain[1]) * KDL.KEEP + double(target[1]) * KDL.TAKE;
    gain[0] = st(gl);
    gain[1] = st(gr);
    const double fb = feedback;
    float* buf = line.get();
    uint32_t rd = (write - length) & kMask;
    uint32_t w = write;
    for (int i = 0; i < kBlock; ++i) {
        const double x = double(buf[rd]);
        l[i] = st(x * gl + double(l[i]));
        r[i] = st(double(r[i]) + x * gr);
        const double sv = double(send[i]);
        send[i] = kGuard;
        buf[w] = st(x * fb + sv);
        rd = (rd + 1) & kMask;
        w = (w + 1) & kMask;
    }
    write = w;
}

} // namespace maquina::acida
