/*
 * lv2_render - minimal offline LV2 host for testing the Maquina bundles.
 *
 *   lv2_render URI RATE SECONDS OUT.wav [options] [symbol=value ...]
 *
 * Options:
 *   --block N        host block size (default 256)
 *   --aligned        put note-ons where they hit engine block starts exactly
 *                    (for bit-exact comparison at RATE 44140, see test.sh)
 *   --twin           run a second instance with the same notes but
 *                    "twin:symbol=value" settings, interleaved block by block,
 *                    then check the first instance against a solo run
 *   --bench          report the CPU time per second of audio
 *
 * Notes: a fixed pattern (velocity varies), note 60, plus note 46 for every
 * other hit (with Trigger Note = Any both play the voice at its set pitch).
 * Each note is released 0.15 s later (the LXR voices ignore note-off;
 * Maquina Dredrum's Gate mode holds its decay until then).
 *
 * Audio inputs (effects, Maquina Oneiroi FX): the same pattern as plucks, a
 * decaying sine at the note's pitch (left) and a fifth above (right).
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <math.h>

#include "../../plugins/common/Timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- URID map ------------------------------------------------------------ */
static char* g_uris[256];
static int g_nuris = 0;
static LV2_URID map_uri(LV2_URID_Map_Handle h, const char* uri)
{
    (void)h;
    for (int i = 0; i < g_nuris; i++)
        if (!strcmp(g_uris[i], uri))
            return (LV2_URID)(i + 1);
    g_uris[g_nuris] = strdup(uri);
    return (LV2_URID)(++g_nuris);
}

typedef struct {
    LilvInstance* inst;
    float* controls;
    float* out[2];
    float* in[2];
    uint8_t* atom_buf;
    uint32_t atom_port;
    float latency;
} Instance;

static LilvWorld* world;
static const LilvPlugin* plugin;
static uint32_t nports;
static LV2_URID urid_midi, urid_chunk, urid_seq, urid_int, urid_float;
static LV2_URID_Map map = { NULL, map_uri };
static LV2_Feature map_feature = { LV2_URID__map, &map };
static int32_t opt_block, opt_nominal;
static float opt_rate;
static LV2_Options_Option options[4];
static LV2_Feature options_feature = { LV2_OPTIONS__options, options };
/* worker: required by plugins with state; this offline host never sends
 * state, so scheduled work is refused */
static LV2_Worker_Status schedule_work(LV2_Worker_Schedule_Handle h, uint32_t size, const void* data)
{
    (void)h; (void)size; (void)data;
    return LV2_WORKER_ERR_UNKNOWN;
}
static LV2_Worker_Schedule worker_schedule = { NULL, schedule_work };
static LV2_Feature worker_feature = { LV2_WORKER__schedule, &worker_schedule };
static const LV2_Feature* features[] = { &map_feature, &options_feature, &worker_feature, NULL };

#define ATOM_CAP 8192

static void apply_settings(Instance* in, int argc, char** argv, int twin)
{
    for (int a = 0; a < argc; a++) {
        const char* s = argv[a];
        if (twin) {
            if (strncmp(s, "twin:", 5)) continue;
            s += 5;
        } else if (!strncmp(s, "twin:", 5)) {
            continue;
        }
        const char* eq = strchr(s, '=');
        if (!eq || s[0] == '-') continue;
        char sym[128];
        snprintf(sym, sizeof(sym), "%.*s", (int)(eq - s), s);
        LilvNode* n = lilv_new_string(world, sym);
        const LilvPort* port = lilv_plugin_get_port_by_symbol(plugin, n);
        lilv_node_free(n);
        if (!port) { fprintf(stderr, "no port %s\n", sym); exit(2); }
        in->controls[lilv_port_get_index(plugin, port)] = (float)atof(eq + 1);
    }
}

static Instance* make_instance(double rate, uint32_t block)
{
    Instance* in = calloc(1, sizeof(Instance));
    in->inst = lilv_plugin_instantiate(plugin, rate, features);
    if (!in->inst) { fprintf(stderr, "instantiate failed\n"); exit(2); }
    in->controls = calloc(nports, sizeof(float));
    in->out[0] = calloc(block, sizeof(float));
    in->out[1] = calloc(block, sizeof(float));
    in->in[0] = calloc(block, sizeof(float));
    in->in[1] = calloc(block, sizeof(float));
    in->atom_buf = aligned_alloc(16, ATOM_CAP);

    LilvNode* audio = lilv_new_uri(world, LV2_CORE__AudioPort);
    LilvNode* control = lilv_new_uri(world, LV2_CORE__ControlPort);
    LilvNode* atom = lilv_new_uri(world, LV2_ATOM__AtomPort);
    LilvNode* input = lilv_new_uri(world, LV2_CORE__InputPort);
    float* mins = calloc(nports, sizeof(float));
    float* maxs = calloc(nports, sizeof(float));
    float* defs = calloc(nports, sizeof(float));
    lilv_plugin_get_port_ranges_float(plugin, mins, maxs, defs);
    int audio_out = 0, audio_in = 0;
    for (uint32_t i = 0; i < nports; i++) {
        const LilvPort* p = lilv_plugin_get_port_by_index(plugin, i);
        if (lilv_port_is_a(plugin, p, audio)) {
            if (lilv_port_is_a(plugin, p, input)) {
                if (audio_in < 2) lilv_instance_connect_port(in->inst, i, in->in[audio_in++]);
            } else if (audio_out < 2) {
                lilv_instance_connect_port(in->inst, i, in->out[audio_out++]);
            }
        } else if (lilv_port_is_a(plugin, p, control)) {
            in->controls[i] = isnan(defs[i]) ? 0.f : defs[i];
            if (lilv_port_is_a(plugin, p, input))
                lilv_instance_connect_port(in->inst, i, &in->controls[i]);
            else
                lilv_instance_connect_port(in->inst, i, &in->latency);
        } else if (lilv_port_is_a(plugin, p, atom)) {
            in->atom_port = i;
            lilv_instance_connect_port(in->inst, i, in->atom_buf);
        }
    }
    free(mins); free(maxs); free(defs);
    lilv_node_free(audio); lilv_node_free(control); lilv_node_free(atom); lilv_node_free(input);
    lilv_instance_activate(in->inst);
    return in;
}

/* note pattern in host frames */
typedef struct { uint64_t frame; uint8_t note, vel; } Note; /* vel 0: note-off */
static Note notes[128];
static int nnotes;

static void make_pattern(double rate, double seconds, int aligned)
{
    static const uint8_t vels[] = { 127, 64, 100, 127, 30, 90, 110, 50 };
    double t = 0.0;
    nnotes = 0;
    for (int i = 0; t < seconds - 0.4 && nnotes < 126; i++, t += 0.37) {
        uint64_t f = (uint64_t)(t * rate) + (uint64_t)(0.05 * rate);
        if (aligned) /* engine block start = H + MAQUINA_EVENT_DELAY */
            f = ((f + MAQUINA_EVENT_DELAY + 63) / 64) * 64 - MAQUINA_EVENT_DELAY;
        notes[nnotes].frame = f;
        notes[nnotes].note = (i % 2) ? 46 : 60;
        notes[nnotes].vel = vels[i % 8];
        nnotes++;
        notes[nnotes] = notes[nnotes - 1];
        notes[nnotes].frame = f + (uint64_t)(0.15 * rate);
        notes[nnotes].vel = 0;
        nnotes++;
    }
}

static void fill_events(Instance* in, uint64_t start, uint32_t frames)
{
    LV2_Atom_Sequence* seq = (LV2_Atom_Sequence*)in->atom_buf;
    seq->atom.type = urid_seq;
    seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
    seq->body.unit = 0;
    seq->body.pad = 0;
    for (int i = 0; i < nnotes; i++) {
        if (notes[i].frame < start || notes[i].frame >= start + frames) continue;
        struct { LV2_Atom_Event ev; uint8_t msg[4]; } e;
        memset(&e, 0, sizeof(e));
        e.ev.time.frames = (int64_t)(notes[i].frame - start);
        e.ev.body.type = urid_midi;
        e.ev.body.size = 3;
        e.msg[0] = notes[i].vel ? 0x90 : 0x80; e.msg[1] = notes[i].note; e.msg[2] = notes[i].vel;
        LV2_Atom_Event* dst = lv2_atom_sequence_end(&seq->body, seq->atom.size);
        memcpy(dst, &e, sizeof(LV2_Atom_Event) + 3);
        seq->atom.size += lv2_atom_pad_size(sizeof(LV2_Atom_Event) + 3);
    }
}

/* the input signal: plucks on the note pattern */
static double g_rate;
static void fill_input(Instance* in, uint64_t start, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; i++) {
        const uint64_t f = start + i;
        float l = 0.f, r = 0.f;
        for (int k = 0; k < nnotes; k++) {
            if (!notes[k].vel || notes[k].frame > f) continue;
            const double t = (double)(f - notes[k].frame) / g_rate;
            if (t > 1.0) continue;
            const double hz = 440.0 * pow(2.0, (notes[k].note - 69) / 12.0);
            const double env = 0.5 * exp(-t * 6.0) * notes[k].vel / 127.0;
            l += (float)(env * sin(2 * M_PI * hz * t));
            r += (float)(env * sin(2 * M_PI * hz * 1.5 * t));
        }
        in->in[0][i] = l;
        in->in[1][i] = r;
    }
}

static void run_block(Instance* in, uint64_t pos, uint32_t frames)
{
    fill_input(in, pos, frames);
    fill_events(in, pos, frames);
    lilv_instance_run(in->inst, frames);
}

static void write_wav(const char* path, const float* lr, size_t frames, uint32_t rate)
{
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    uint32_t bytes = (uint32_t)(frames * 8), v;
    uint16_t s;
    fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); s = 3; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 8; fwrite(&v, 4, 1, f); s = 8; fwrite(&s, 2, 1, f);
    s = 32; fwrite(&s, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(lr, 8, frames, f);
    fclose(f);
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s URI RATE SECONDS OUT.wav [--block N] [--aligned] [--twin] [--bench] [sym=val..]\n", argv[0]);
        return 2;
    }
    const char* uri = argv[1];
    const double rate = atof(argv[2]);
    const double seconds = atof(argv[3]);
    const char* outpath = argv[4];
    uint32_t block = 256;
    int aligned = 0, twin = 0, bench = 0;
    for (int a = 5; a < argc; a++) {
        if (!strcmp(argv[a], "--block") && a + 1 < argc) block = (uint32_t)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--aligned")) aligned = 1;
        else if (!strcmp(argv[a], "--twin")) twin = 1;
        else if (!strcmp(argv[a], "--bench")) bench = 1;
    }

    urid_midi = map_uri(NULL, LV2_MIDI__MidiEvent);
    urid_chunk = map_uri(NULL, LV2_ATOM__Chunk);
    urid_seq = map_uri(NULL, LV2_ATOM__Sequence);
    urid_int = map_uri(NULL, LV2_ATOM__Int);
    urid_float = map_uri(NULL, LV2_ATOM__Float);
    opt_block = (int32_t)block;
    opt_nominal = (int32_t)block;
    opt_rate = (float)rate;
    options[0] = (LV2_Options_Option){ LV2_OPTIONS_INSTANCE, 0, map_uri(NULL, LV2_BUF_SIZE__maxBlockLength), sizeof(int32_t), urid_int, &opt_block };
    options[1] = (LV2_Options_Option){ LV2_OPTIONS_INSTANCE, 0, map_uri(NULL, LV2_BUF_SIZE__nominalBlockLength), sizeof(int32_t), urid_int, &opt_nominal };
    options[2] = (LV2_Options_Option){ LV2_OPTIONS_INSTANCE, 0, map_uri(NULL, LV2_PARAMETERS__sampleRate), sizeof(float), urid_float, &opt_rate };
    options[3] = (LV2_Options_Option){ LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, NULL };

    world = lilv_world_new();
    lilv_world_load_all(world);
    LilvNode* uri_node = lilv_new_uri(world, uri);
    plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), uri_node);
    if (!plugin) { fprintf(stderr, "plugin %s not found (LV2_PATH?)\n", uri); return 2; }
    nports = lilv_plugin_get_num_ports(plugin);

    g_rate = rate;
    make_pattern(rate, seconds, aligned);
    const size_t total = (size_t)(seconds * rate);
    float* lr = calloc(total * 2, sizeof(float));

    Instance* a = make_instance(rate, block);
    apply_settings(a, argc - 5, argv + 5, 0);
    Instance* b = NULL;
    if (twin) {
        b = make_instance(rate, block);
        apply_settings(b, argc - 5, argv + 5, 1);
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (size_t pos = 0; pos < total; pos += block) {
        const uint32_t n = (uint32_t)((total - pos) < block ? (total - pos) : block);
        run_block(a, pos, n);
        for (uint32_t i = 0; i < n; i++) {
            lr[2 * (pos + i)] = a->out[0][i];
            lr[2 * (pos + i) + 1] = a->out[1][i];
        }
        if (b) run_block(b, pos, n);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double peak = 0.0;
    int bad = 0;
    for (size_t i = 0; i < total * 2; i++) {
        if (!isfinite(lr[i])) bad = 1;
        if (fabs(lr[i]) > peak) peak = fabs(lr[i]);
    }
    printf("%s @ %.0f Hz: peak %.3f (%.1f dBFS), latency %.0f frames%s\n", uri, rate, peak,
           peak > 0 ? 20 * log10(peak) : -999.0, a->latency, bad ? ", NON-FINITE SAMPLES" : "");
    if (bench) {
        const double cpu = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        printf("  cpu: %.2f ms per second of audio (%d instance%s)\n", cpu / seconds * 1000.0,
               b ? 2 : 1, b ? "s" : "");
    }
    write_wav(outpath, lr, total, (uint32_t)rate);

    int rc = (bad || peak == 0.0) ? 1 : 0;
    if (twin) {
        /* solo re-run of instance A settings on a fresh instance */
        Instance* c = make_instance(rate, block);
        apply_settings(c, argc - 5, argv + 5, 0);
        size_t diff = 0;
        for (size_t pos = 0; pos < total; pos += block) {
            const uint32_t n = (uint32_t)((total - pos) < block ? (total - pos) : block);
            run_block(c, pos, n);
            for (uint32_t i = 0; i < n; i++)
                if (c->out[0][i] != lr[2 * (pos + i)] || c->out[1][i] != lr[2 * (pos + i) + 1]) diff++;
        }
        printf("  twin isolation: %s (%zu differing samples)\n", diff ? "LEAK" : "ok", diff);
        if (diff) rc = 1;
    }
    return rc;
}
