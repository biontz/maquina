// rack.hpp - the few VCV Rack SDK pieces the vendored Noise Plethora port
// (third_party/noise-plethora) uses, so its files compile unmodified outside
// Rack. Found on the include path as <rack.hpp>.
//
//   random::uniform()         Rack: global per-thread xoroshiro128+ -> here
//                             xoshiro128+, one per plugin instance (ScopedRng)
//   dsp::RingBuffer, Timer    same semantics as Rack's
//   APP->engine               fixed 44100 Hz: Maquina runs the algorithms at the
//                             Teensy's rate and resamples (PlethoraHost)
//   DEBUG(...)                nothing
//
// SPDX-License-Identifier: GPL-3.0-or-later
// (interfaces after VCV Rack, GPL-3.0-or-later, Copyright VCV)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEBUG(...) ((void)0)

namespace rack {

namespace random {

struct Xoshiro128Plus {
    uint32_t s[4] = { 0x9e3779b9u, 0x243f6a88u, 0xb7e15162u, 0x12345678u };
    void seed(uint64_t x)
    {
        for (uint32_t& v : s) { // splitmix64
            x += 0x9e3779b97f4a7c15ull;
            uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            v = uint32_t(z ^ (z >> 31)) | 1u;
        }
    }
    uint32_t next()
    {
        const uint32_t result = s[0] + s[3];
        const uint32_t t = s[1] << 9;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t;
        s[3] = (s[3] << 11) | (s[3] >> 21);
        return result;
    }
};

// Rack has one generator per thread. Maquina gives each plugin instance its
// own (ScopedRng around everything an instance runs), so instances sharing a
// host thread do not change each other's random streams.
inline Xoshiro128Plus*& current()
{
    thread_local Xoshiro128Plus* p = nullptr;
    return p;
}
inline Xoshiro128Plus& local()
{
    thread_local Xoshiro128Plus fallback;
    Xoshiro128Plus* p = current();
    return p ? *p : fallback;
}
struct ScopedRng {
    Xoshiro128Plus* prev;
    explicit ScopedRng(Xoshiro128Plus& r) : prev(current()) { current() = &r; }
    ~ScopedRng() { current() = prev; }
    ScopedRng(const ScopedRng&) = delete;
    ScopedRng& operator=(const ScopedRng&) = delete;
};
// [0, 1), 24 bits like Rack's
inline float uniform() { return float(local().next() >> 8) * (1.f / 16777216.f); }
inline void seed(uint64_t x) { local().seed(x); }

} // namespace random

namespace dsp {

// Rack's dsp::RingBuffer: S must be a power of 2
template <typename T, size_t S>
struct RingBuffer {
    T data[S] {};
    size_t start = 0, end = 0;

    static size_t mask(size_t i) { return i & (S - 1); }
    void push(T t) { data[mask(end++)] = t; }
    void pushBuffer(const T* t, int n)
    {
        for (int i = 0; i < n; ++i) push(t[i]);
    }
    T shift() { return data[mask(start++)]; }
    void clear() { start = end = 0; }
    bool empty() const { return start == end; }
    bool full() const { return end - start >= S; }
    size_t size() const { return end - start; }
    size_t capacity() const { return S - size(); }
};

struct Timer {
    float time = 0.f;
    void reset() { time = 0.f; }
    float process(float deltaTime)
    {
        time += deltaTime;
        return time;
    }
    float getTime() const { return time; }
};

} // namespace dsp

namespace math {
inline float clamp(float x, float a, float b) { return std::max(std::min(x, b), a); }
inline float rescale(float x, float xMin, float xMax, float yMin, float yMax)
{
    return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}
} // namespace math

} // namespace rack

using namespace rack;
using rack::math::clamp;
using rack::math::rescale;

// APP->engine->getSampleRate(): the algorithms always run at 44100 Hz here
struct PlethoraCompatEngine {
    float getSampleRate() const { return 44100.f; }
    float getSampleTime() const { return 1.f / 44100.f; }
};
struct PlethoraCompatApp {
    PlethoraCompatEngine* engine;
};
inline PlethoraCompatEngine gPlethoraCompatEngine;
inline PlethoraCompatApp gPlethoraCompatApp { &gPlethoraCompatEngine };
#define APP (&gPlethoraCompatApp)
