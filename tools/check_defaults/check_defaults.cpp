// check_defaults - the generated table defaults (used by the GUI for reset)
// must equal what the engine reports after loading the Initkit (LXR voices).
// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include "EngineHost.hpp"
#include "VoiceTables.generated.hpp"

int main()
{
    int bad = 0;
    for (const auto& v : maquina::k_voices) {
        if (!maquina::isLxrVoice(v.voice)) continue; // not an LXR voice: defaults are the table's
        maquina::EngineHost host(v, 1);
        for (uint32_t i = 0; i < v.numParams; ++i)
            if (host.defaultParam(i) != float(v.params[i].def)) {
                std::printf("%s.%s: table %d, engine %g\n", v.id, v.params[i].symbol, v.params[i].def,
                            host.defaultParam(i));
                ++bad;
            }
    }
    std::printf("check_defaults: %s\n", bad ? "FAILED" : "ok");
    return bad ? 1 : 0;
}
