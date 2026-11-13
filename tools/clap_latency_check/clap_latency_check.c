// SPDX-License-Identifier: GPL-3.0-only
// clap_latency_check - minimal CLAP host: load a .clap, activate it at a
// sample rate other than the default, process a few blocks and run the
// main-thread callback. Fails if the plugin requests a restart or reports a
// latency change while active (the warnings Bitwig logs as "Plugin needs to
// restart processing" / "reported latency change while activated"). Also
// restores the plugin's own state before activating, like a host opening a
// project; a latency report there makes Bitwig log "Plugin needs to restart
// processing during initialization".
//
//   clap_latency_check <bundle.clap>... (Linux: the bundle is the .so itself)

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/ext/latency.h"
#include "clap/ext/state.h"

static int g_restart, g_latency_changed, g_init_latency_changed, g_callback, g_active;

static void host_request_restart(const clap_host_t* h) { (void)h; ++g_restart; }
static void host_request_process(const clap_host_t* h) { (void)h; }
static void host_request_callback(const clap_host_t* h) { (void)h; ++g_callback; }
static void host_latency_changed(const clap_host_t* h)
{
    (void)h;
    if (g_active) ++g_latency_changed;
    else ++g_init_latency_changed;
}

static const clap_host_latency_t host_latency = { host_latency_changed };

static const void* host_get_extension(const clap_host_t* h, const char* id)
{
    (void)h;
    if (!strcmp(id, CLAP_EXT_LATENCY)) return &host_latency;
    return NULL;
}

static const clap_host_t host = {
    CLAP_VERSION_INIT, NULL, "clap_latency_check", "maquina", "", "1",
    host_get_extension, host_request_restart, host_request_process, host_request_callback,
};

static uint32_t in_size(const clap_input_events_t* l) { (void)l; return 0; }
static const clap_event_header_t* in_get(const clap_input_events_t* l, uint32_t i) { (void)l; (void)i; return NULL; }
static bool out_push(const clap_output_events_t* l, const clap_event_header_t* e) { (void)l; (void)e; return true; }

// in-memory stream for the state round trip
static char g_state[1 << 20];
static size_t g_state_size, g_state_pos;

static int64_t os_write(const clap_ostream_t* s, const void* buf, uint64_t size)
{
    (void)s;
    if (g_state_size + size > sizeof g_state) return -1;
    memcpy(g_state + g_state_size, buf, size);
    g_state_size += size;
    return (int64_t)size;
}

static int64_t is_read(const clap_istream_t* s, void* buf, uint64_t size)
{
    (void)s;
    const size_t left = g_state_size - g_state_pos;
    if (size > left) size = left;
    memcpy(buf, g_state + g_state_pos, size);
    g_state_pos += size;
    return (int64_t)size;
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

    g_restart = g_latency_changed = g_init_latency_changed = g_callback = 0;
    g_active = 0;

    // restore state before activation (host opening a saved project)
    const clap_plugin_state_t* st = (const clap_plugin_state_t*)plugin->get_extension(plugin, CLAP_EXT_STATE);
    if (st) {
        clap_ostream_t os = { NULL, os_write };
        clap_istream_t is = { NULL, is_read };
        g_state_size = g_state_pos = 0;
        if (!st->save(plugin, &os) || !st->load(plugin, &is)) { printf("%s: state round trip failed\n", path); return 1; }
        plugin->on_main_thread(plugin);
    }
    const double rate = 48000.0;
    const uint32_t frames = 256;
    if (!plugin->activate(plugin, rate, frames, frames)) { printf("%s: activate failed\n", path); return 1; }
    g_active = 1;
    const clap_plugin_latency_t* lat = (const clap_plugin_latency_t*)plugin->get_extension(plugin, CLAP_EXT_LATENCY);
    const uint32_t latency = lat ? lat->get(plugin) : 0;
    plugin->start_processing(plugin);

    float l[256], r[256];
    float* chans[2] = { l, r };
    clap_audio_buffer_t out = { chans, NULL, 2, 0, 0 };
    clap_input_events_t in_ev = { NULL, in_size, in_get };
    clap_output_events_t out_ev = { NULL, out_push };
    clap_process_t proc;
    memset(&proc, 0, sizeof proc);
    proc.frames_count = frames;
    proc.audio_outputs = &out;
    proc.audio_outputs_count = 1;
    proc.in_events = &in_ev;
    proc.out_events = &out_ev;
    for (int i = 0; i < 4; ++i) {
        proc.steady_time = (int64_t)i * frames;
        plugin->process(plugin, &proc);
        plugin->on_main_thread(plugin); // what a host does after request_callback
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    g_active = 0;
    plugin->destroy(plugin);
    entry->deinit();

    const int bad = g_restart || g_latency_changed || g_init_latency_changed;
    printf("  %s: latency %u frames @ %.0f Hz, restart requests %d, latency changes on state load %d / while active %d  %s\n",
           desc->name, latency, rate, g_restart, g_init_latency_changed, g_latency_changed, bad ? "FAIL" : "ok");
    return bad;
}

int main(int argc, char** argv)
{
    int failures = 0;
    for (int i = 1; i < argc; ++i) failures += check(argv[i]);
    printf("clap_latency_check: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
