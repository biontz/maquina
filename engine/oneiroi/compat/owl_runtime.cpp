// owl_runtime.cpp - the few OWL runtime pieces Oneiroi's DSP needs, for a
// desktop build without the OWL firmware (included by oneiroi_unity.cpp).
//
//   AudioBuffer::create   heap-allocated stereo buffer (OWL: ManagedMemoryBuffer)
//   assert_failed         OwlProgram's ASSERT() target (ignored)
//   fast_* tables         LibSource's default pow/log tables, set once at load
//                         (the firmware hands its own via a service call;
//                         without one it falls back to these same tables)
//   operator new/delete   zero-filled allocations, as the OWL firmware's heap
//                         (OwlProgram Source/heap_5.c pvPortMalloc memsets
//                         every block): Oneiroi leaves members uninitialised
//                         and relies on that. They replace new/delete for
//                         this plugin binary only: DPF links every plugin
//                         with a version script that exports nothing but its
//                         entry point, so these stay local and never reach
//                         the host (Bitwig, Machinista)
//   arm_rand32 / randf    the OwlProgram xorshift32, but with the state per
//                         engine instance (OneiroiRng below) instead of one
//                         global, so instances neither share nor race on it
//                         and a seeded render is reproducible
//
// SPDX-License-Identifier: GPL-3.0-only
#include "Patch.h"
#include "FastPowTable.h"
#include "FastLogTable.h"
#include "owl_runtime.h"

#include <cstdlib>
#include <new>

static void* maquinaZeroAlloc(std::size_t n)
{
  if (void* p = std::calloc(1, n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t n) { return maquinaZeroAlloc(n); }
void* operator new[](std::size_t n) { return maquinaZeroAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return std::calloc(1, n ? n : 1); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return std::calloc(1, n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {

class HeapAudioBuffer : public AudioBuffer {
public:
  HeapAudioBuffer(int channels, int samples) : channels_(channels), size_(samples) {
    data_ = new float[size_t(channels) * size_t(samples)]();
  }
  ~HeapAudioBuffer() override { delete[] data_; }
  FloatArray getSamples(int channel) override {
    return FloatArray(data_ + size_t(channel) * size_t(size_), size_t(size_));
  }
  int getChannels() override { return channels_; }
  int getSize() override { return size_; }
  void clear() override {
    for (size_t i = 0; i < size_t(channels_) * size_t(size_); ++i) data_[i] = 0.f;
  }
private:
  float* data_;
  int channels_, size_;
};

struct FastTables {
  FastTables() {
    fast_pow_set_table(fast_pow_table, fast_pow_table_size);
    fast_log_set_table(fast_log_table, fast_log_table_size);
  }
};
const FastTables kFastTables;

thread_local uint32_t* tCurrentSeed = nullptr;
uint32_t gFallbackSeed = 33641; // OwlProgram's initial seed; only used outside an OneiroiRng scope

} // namespace

AudioBuffer::~AudioBuffer() {}
AudioBuffer* AudioBuffer::create(int channels, int samples) { return new HeapAudioBuffer(channels, samples); }
void AudioBuffer::destroy(AudioBuffer* buffer) { delete buffer; }

extern "C" void assert_failed(const char*, const char*, int) {}

OneiroiRng::Scope::Scope(OneiroiRng& rng) : previous_(tCurrentSeed) { tCurrentSeed = &rng.state; }
OneiroiRng::Scope::~Scope() { tCurrentSeed = previous_; }

// basicmaths.c, with the global seed replaced by the scoped one
extern "C" void arm_srand32(uint32_t s) { *(tCurrentSeed ? tCurrentSeed : &gFallbackSeed) = s; }
extern "C" uint32_t arm_rand32() {
  uint32_t& r = *(tCurrentSeed ? tCurrentSeed : &gFallbackSeed);
  r ^= r << 13;
  r ^= r >> 17;
  r ^= r << 5;
  return r;
}
extern "C" float randf() { return arm_rand32() * (1 / 4294967296.0f); }
