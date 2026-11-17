// D50Sysex.hpp - reading D-50 patches from SysEx (.syx files, or messages
// received over MIDI) for Maquina LA-50. The Python twin is tools/d50_syx.py.
//
// Roland exclusive messages for the D-50 (F0 41 dev 14 cmd a1 a2 a3 data..
// sum F7, D-50 MIDI Implementation 2.00, section 4): DT1 (12H, normal mode)
// and DAT (42H, data transfer mode: what a real D-50 sends as a bulk dump).
// Data is placed by its 7-bit address; a patch is 448 bytes (7 blocks of 64):
//   00-00-00            the temporary area (the patch being played)
//   02-00-00 + n * 448  patch memory n (0..63), a bank dump
// Other messages (WSD, EOD, ACK, RQ1 ..) and other devices are skipped; a
// message with a wrong checksum is ignored and counted.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "La50Layout.generated.hpp"

namespace la50 {

using PatchData = std::array<uint8_t, kPatchBytes>;

struct SysexPatch {
    int number;       // 1..64 (bank), 0 = the temporary area
    std::string name; // the patch name (18 characters, trimmed)
    PatchData data;   // the 448 bytes, each clamped to its field's range
};

struct SysexResult {
    std::vector<SysexPatch> patches;
    int messages = 0;      // D-50 data messages read
    int badChecksums = 0;  // messages ignored
};

// every complete patch in a buffer (a whole .syx file, or several messages)
SysexResult parseSysex(const uint8_t* data, size_t size);

// a patch's name (the D-50 character set)
std::string patchName(const PatchData& p);
// the DT1 messages of one patch into the temporary area (tests, MIDI out)
std::vector<uint8_t> patchToSysex(const PatchData& p, uint8_t device = 0);

} // namespace la50
