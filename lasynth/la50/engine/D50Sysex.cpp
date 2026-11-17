// D50Sysex.cpp - see D50Sysex.hpp.
// SPDX-License-Identifier: GPL-3.0-only
#include "D50Sysex.hpp"

#include <algorithm>
#include <map>

namespace la50 {

namespace {

constexpr uint32_t kTempBase = 0;
constexpr uint32_t kMemoryBase = 2u << 14;
constexpr int kBankPatches = 64;

uint32_t flat(uint8_t a, uint8_t b, uint8_t c) { return uint32_t(a & 127) << 14 | uint32_t(b & 127) << 7 | (c & 127); }

bool patchAt(const std::map<uint32_t, uint8_t>& mem, uint32_t base, PatchData& out)
{
    for (int k = 0; k < kPatchBytes; ++k) {
        auto it = mem.find(base + uint32_t(k));
        if (it == mem.end()) return false;
        // fields beyond their range (or garbage in name / extension bytes): clamped
        const uint8_t mx = kFieldMax[k];
        out[size_t(k)] = mx ? std::min<uint8_t>(it->second, mx) : uint8_t(it->second & 127);
    }
    return true;
}

} // namespace

std::string patchName(const PatchData& p)
{
    std::string s;
    for (int k = 0; k < 18; ++k) s += kCharset[p[size_t(kPatch + k)] & 63];
    const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

SysexResult parseSysex(const uint8_t* data, size_t size)
{
    SysexResult r;
    std::map<uint32_t, uint8_t> mem;
    size_t i = 0;
    while (i < size) {
        while (i < size && data[i] != 0xF0) ++i;
        size_t e = i;
        while (e < size && data[e] != 0xF7) ++e;
        if (e >= size) break;
        const uint8_t* m = data + i;
        const size_t len = e - i + 1;
        i = e + 1;
        if (len < 10 || m[1] != 0x41 || m[3] != 0x14 || (m[4] != 0x12 && m[4] != 0x42)) continue;
        // body: address (3) + data, then the checksum: their sum is 0 mod 128
        const uint8_t* body = m + 5;
        const size_t bodyLen = len - 7;
        unsigned sum = m[len - 2];
        for (size_t k = 0; k < bodyLen; ++k) sum += body[k];
        if (sum & 127) {
            ++r.badChecksums;
            continue;
        }
        ++r.messages;
        const uint32_t addr = flat(body[0], body[1], body[2]);
        for (size_t k = 3; k < bodyLen; ++k) mem[addr + uint32_t(k - 3)] = body[k] & 127;
    }
    SysexPatch p;
    if (patchAt(mem, kTempBase, p.data)) {
        p.number = 0;
        p.name = patchName(p.data);
        r.patches.push_back(p);
    }
    for (int n = 0; n < kBankPatches; ++n)
        if (patchAt(mem, kMemoryBase + uint32_t(n * kPatchBytes), p.data)) {
            p.number = n + 1;
            p.name = patchName(p.data);
            r.patches.push_back(p);
        }
    return r;
}

std::vector<uint8_t> patchToSysex(const PatchData& p, uint8_t device)
{
    std::vector<uint8_t> out;
    for (int k = 0; k < kPatchBytes; k += 64) { // one block per message
        const uint32_t addr = kTempBase + uint32_t(k);
        const uint8_t a[3] = { uint8_t(addr >> 14 & 127), uint8_t(addr >> 7 & 127), uint8_t(addr & 127) };
        std::vector<uint8_t> body(a, a + 3);
        body.insert(body.end(), p.begin() + k, p.begin() + k + 64);
        unsigned sum = 0;
        for (uint8_t b : body) sum += b;
        out.insert(out.end(), { 0xF0, 0x41, uint8_t(device & 15), 0x14, 0x12 });
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(uint8_t((128 - (sum & 127)) & 127));
        out.push_back(0xF7);
    }
    return out;
}

} // namespace la50
