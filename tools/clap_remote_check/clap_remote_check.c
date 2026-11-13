// SPDX-License-Identifier: GPL-3.0-only
// clap_remote_check - minimal CLAP host: load each .clap, read its
// remote-controls pages (what Bitwig shows as remote-control pages) and check
// them against plugins/common/RemotePages.generated.hpp: page count, names,
// and that every slot points at the parameter with the expected symbol.
//
//   clap_remote_check <bundle.clap>... (Linux: the bundle is the .so itself)

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/ext/params.h"
#include "clap/ext/remote-controls.h"

typedef struct { const char* name; const char* symbols[8]; } ClapRemotePage;
typedef struct { const ClapRemotePage* pages; uint32_t count; } ClapRemotePages;
#include "../../plugins/common/RemotePages.generated.hpp"

static const char* const kVoices[] = { "kick", "tom", "snare", "cymbal", "hihat" }; // k_voices order

static const void* host_get_extension(const clap_host_t* h, const char* id) { (void)h; (void)id; return NULL; }
static void host_noop(const clap_host_t* h) { (void)h; }

static const clap_host_t host = {
    CLAP_VERSION_INIT, NULL, "clap_remote_check", "maquina", "", "1",
    host_get_extension, host_noop, host_noop, host_noop,
};

// DPF puts "<port group>/<symbol>" or "<symbol>" into clap_param_info.module
static int module_is(const char* module, const char* symbol)
{
    const char* slash = strrchr(module, '/');
    return strcmp(slash ? slash + 1 : module, symbol) == 0;
}

static int check(const char* path)
{
    void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { printf("%s: dlopen failed: %s\n", path, dlerror()); return 1; }
    const clap_plugin_entry_t* entry = (const clap_plugin_entry_t*)dlsym(lib, "clap_entry");
    if (!entry || !entry->init(path)) { printf("%s: no clap_entry\n", path); return 1; }
    const clap_plugin_factory_t* factory =
        (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    const clap_plugin_t* plugin = factory->create_plugin(factory, &host, desc->id);
    if (!plugin || !plugin->init(plugin)) { printf("%s: create failed\n", path); return 1; }

    int voice = -1;
    for (int v = 0; v < 5; ++v) {
        char id[64];
        snprintf(id, sizeof id, "org.maquina.%s", kVoices[v]);
        if (!strcmp(desc->id, id)) voice = v;
    }
    int bad = 0;
    const clap_plugin_params_t* params = (const clap_plugin_params_t*)plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    const clap_plugin_remote_controls_t* rc =
        (const clap_plugin_remote_controls_t*)plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS);
    const clap_plugin_remote_controls_t* rcCompat =
        (const clap_plugin_remote_controls_t*)plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS_COMPAT);
    if (voice < 0 || !params || !rc || rc != rcCompat) {
        printf("  %s: unknown plugin id or remote-controls extension missing  FAIL\n", desc->id);
        bad = 1;
    } else {
        const ClapRemotePages* want = &k_remote_pages[voice];
        const uint32_t count = rc->count(plugin), nparams = params->count(plugin);
        if (count != want->count) {
            printf("  %s: %u pages, expected %u\n", desc->name, count, want->count);
            bad = 1;
        }
        for (uint32_t p = 0; p < count && p < want->count; ++p) {
            clap_remote_controls_page_t page;
            if (!rc->get(plugin, p, &page) || page.page_id != p || strcmp(page.page_name, want->pages[p].name)) {
                printf("  %s: page %u: bad page (name '%s')\n", desc->name, p, page.page_name);
                bad = 1;
                continue;
            }
            for (int s = 0; s < CLAP_REMOTE_CONTROLS_COUNT; ++s) {
                const char* sym = want->pages[p].symbols[s];
                const clap_id id = page.param_ids[s];
                clap_param_info_t info;
                int ok;
                if (!sym) ok = id == CLAP_INVALID_ID;
                else {
                    ok = 0;
                    for (uint32_t i = 0; i < nparams && !ok; ++i)
                        ok = params->get_info(plugin, i, &info) && info.id == id && module_is(info.module, sym);
                }
                if (!ok) {
                    printf("  %s: page '%s' slot %d: param id %u is not '%s'\n", desc->name, page.page_name, s,
                           id, sym ? sym : "(empty)");
                    bad = 1;
                }
            }
        }
        clap_remote_controls_page_t first;
        memset(&first, 0, sizeof first);
        if (count) rc->get(plugin, 0, &first);
        printf("  %s: %u remote-control pages (section '%s')  %s\n", desc->name, count, first.section_name,
               bad ? "FAIL" : "ok");
    }

    plugin->destroy(plugin);
    entry->deinit();
    return bad;
}

int main(int argc, char** argv)
{
    int failures = 0;
    for (int i = 1; i < argc; ++i) failures += check(argv[i]);
    printf("clap_remote_check: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
