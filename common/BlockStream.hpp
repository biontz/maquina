// BlockStream.hpp - runs a fixed-rate, fixed-block engine inside a host of any
// sample rate and block size: OneiroiHost's pipeline, made generic for
// Maquina Klang (Plaits, 48 kHz, 12-frame blocks) and Klong (Rings, 48 kHz,
// 24-frame blocks).
//
//   host input -> StreamResampler (host rate -> engine rate) -> engine blocks
//   -> StreamResampler (engine rate -> host rate) -> output FIFO -> host
//
// Without input (an instrument), the engine runs as host time advances: an
// engine frame is due once the host has passed its time, as if silence were
// pushed through an input resampler without its lag.
//
// The output FIFO starts with latency() frames of silence: the worst-case
// lag of the pipeline, so it never runs dry (underruns() counts if it does).
//
// The engine is called as runBlock(firstFrame, inL, inR, outL, outR):
// firstFrame = the engine frame number of the block's first frame (frame 0 =
// the first after reset()), in* = kBlock input frames (silence without
// input), out* = kBlock frames to fill.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "StreamResampler.hpp"

namespace maquina {

template <int kBlock>
class BlockStream {
public:
    static constexpr uint32_t kFifoFrames = 1 << 15;
    static constexpr uint32_t kChunk = 4096; // host frames per pass

    BlockStream() : fFifoL(kFifoFrames, 0.f), fFifoR(kFifoFrames, 0.f) {}

    void setup(double hostRate, double engineRate, bool withInput)
    {
        fHostRate = hostRate > 0.0 ? hostRate : engineRate;
        fEngineRate = engineRate;
        fWithInput = withInput;
        fIn.setup(fHostRate, fEngineRate);
        fOut.setup(fEngineRate, fHostRate);
        // input filter (kHalf host frames), one engine block, output filter
        // (kHalf engine frames), plus rounding
        const double toHost = fHostRate / fEngineRate;
        const double lag = (withInput && !fIn.bypass() ? StreamResampler::kHalf : 0.0) +
                           (kBlock + (fOut.bypass() ? 0.0 : StreamResampler::kHalf)) * toHost;
        fLatency = uint32_t(std::ceil(lag)) + 2;
        reset();
    }

    void reset()
    {
        fIn.reset();
        fOut.reset();
        fFill = 0;
        fBlockFrame = 0;
        fHostFrame = 0;
        std::fill(fFifoL.begin(), fFifoL.end(), 0.f);
        std::fill(fFifoR.begin(), fFifoR.end(), 0.f);
        fFifoRead = 0;
        fFifoCount = fLatency;
    }

    uint32_t latency() const { return fLatency; }
    double hostRate() const { return fHostRate; }
    uint64_t underruns() const { return fUnderruns; }

    // the engine frame (fractional) that host frame `offset` of the next
    // process() call corresponds to
    double engineFrameAt(uint32_t offset) const
    {
        return double(fHostFrame + offset) * fEngineRate / fHostRate;
    }

    template <class RunBlock>
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames, RunBlock&& run)
    {
        for (uint32_t done = 0; done < frames;) {
            const uint32_t n = std::min(frames - done, kChunk);
            if (fWithInput) {
                for (uint32_t i = 0; i < n; ++i) {
                    const float l = inL ? inL[done + i] : 0.f;
                    const float r = inR ? inR[done + i] : (inL ? inL[done + i] : 0.f);
                    fIn.push(l, r, [&](float el, float er) { engineFrame(el, er, run); });
                }
            } else {
                // engine frames whose time the host has passed by the end of this pass
                const uint64_t due = uint64_t(std::floor(double(fHostFrame + done + n) * fEngineRate / fHostRate));
                while (fBlockFrame + uint64_t(fFill) < due) engineFrame(0.f, 0.f, run);
            }
            for (uint32_t i = 0; i < n; ++i) {
                if (fFifoCount > 0) {
                    outL[done + i] = fFifoL[fFifoRead];
                    outR[done + i] = fFifoR[fFifoRead];
                    fFifoRead = (fFifoRead + 1) % kFifoFrames;
                    --fFifoCount;
                } else {
                    outL[done + i] = outR[done + i] = 0.f;
                    ++fUnderruns;
                }
            }
            done += n;
        }
        fHostFrame += frames;
    }

private:
    template <class RunBlock>
    void engineFrame(float l, float r, RunBlock& run)
    {
        fInL[fFill] = l;
        fInR[fFill] = r;
        if (++fFill < kBlock) return;
        fFill = 0;
        run(fBlockFrame, fInL, fInR, fOutL, fOutR);
        for (int i = 0; i < kBlock; ++i)
            fOut.push(fOutL[i], fOutR[i], [this](float ol, float orr) {
                if (fFifoCount >= kFifoFrames) return;
                const uint32_t w = (fFifoRead + fFifoCount) % kFifoFrames;
                fFifoL[w] = ol;
                fFifoR[w] = orr;
                ++fFifoCount;
            });
        fBlockFrame += kBlock;
    }

    StreamResampler fIn, fOut;
    double fHostRate = 48000.0, fEngineRate = 48000.0;
    bool fWithInput = false;
    uint32_t fLatency = 0;

    float fInL[kBlock] = {}, fInR[kBlock] = {}, fOutL[kBlock] = {}, fOutR[kBlock] = {};
    int fFill = 0;
    uint64_t fBlockFrame = 0; // engine frame of the block being filled
    uint64_t fHostFrame = 0;

    std::vector<float> fFifoL, fFifoR;
    uint32_t fFifoRead = 0, fFifoCount = 0;
    uint64_t fUnderruns = 0;
};

} // namespace maquina
