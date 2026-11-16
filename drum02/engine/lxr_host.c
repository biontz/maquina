/*
 * lxr_host.c - host-side driver for the LXR-02 firmware sound engine.
 * See lxr_host.h.
 *
 * Copyright (C) 2026 Maquina contributors
 * Based on the LXR-02 firmware sound engine, Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "lxr_host.h"

#include <string.h>

#include "Soundengine/config.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/DSPAudio/ResonantFilter.h"
#include "Soundengine/MIDI/ParameterArray.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/FxBus.h"
#include "Preset/InitKit.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "menu/CcNr2Text.h"
#include "frontPanelParser_ui.h"

/* ======================================================================
 * Definitions the engine links against that normally live in the UI,
 * sequencer, I/O or HAL parts of the firmware.
 * ====================================================================== */

uint8_t activeParameters[NUM_PARAMS];   /* the kit edit buffer (menu.c) */
uint8_t menu_activePage = 0;            /* never SEQ_PAGE: notes always play */
uint8_t frontParser_activeTrack = 0;
volatile uint32_t systick_ticks = 0;
PatternSet seq_patternSet;              /* only touched in sequencer page mode */
GPIO_TypeDef lxr_shim_gpio_all_high = { 0xFFFFFFFFu, 0, 0 };
RNG_HandleTypeDef hrng;

static uint16_t lxr_bpm = 120;
static uint32_t lxr_rng = 0x2545F491u;
static int lxr_rng_frozen = 0;      /* test hook, see lxr_test_freeze_rng() */

/* Hardware RNG stand-in: xorshift32, per instance (part of the state blob). */
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef* h)
{
    (void)h;
    if (lxr_rng_frozen)
        return lxr_rng;
    uint32_t x = lxr_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    lxr_rng = x;
    return x;
}

/* The channel faders sit at full scale; level is the voice's own volume. */
float adc_getFaderVolume(uint8_t idx) { (void)idx; return 1.0f; }

uint16_t seq_getBpm(void) { return lxr_bpm; }
uint8_t seq_getExtSync(void) { return 0; }
uint8_t seq_isRunning(void) { return 0; }
uint8_t seq_isTrackMuted(uint8_t trackNr) { (void)trackNr; return 0; }
void seq_setMute(uint8_t trackNr, uint8_t isMuted) { (void)trackNr; (void)isMuted; }
void seq_addNote(uint8_t trackNr, uint8_t vel, uint8_t note) { (void)trackNr; (void)vel; (void)note; }
void seq_midiNoteOff(uint8_t chan) { (void)chan; }
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc) { (void)channel; (void)note; (void)veloc; }
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value) { (void)voice; (void)dest; (void)value; }
void seq_setNextPattern(const uint8_t patNr) { (void)patNr; }
void seq_setRunning(uint8_t isRunning) { (void)isRunning; }
void seq_sync(void) {}
void sync_midiResetPosition(void) {}
void sync_midiStartStop(uint8_t isStart) { (void)isStart; }
uint8_t menu_getActiveVoice(void) { return 0; }
uint8_t menu_getSubPage(void) { return 0; }
uint8_t menu_getViewedPattern(void) { return 0; }
void menu_repaint(void) {}
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle) { (void)subPageNr; (void)toggle; }
void uart_sendFrontpanelByte(uint8_t data) { (void)data; }
void uart_sendMidi(MidiMsg msg) { (void)msg; }
void usb_sendMidi(MidiMsg msg) { (void)msg; }
void Error_Handler(void) {}

/* ======================================================================
 * Per-instance state
 *
 * Every mutable global the sound path reads or writes. tools/check_state.sh
 * compares this list against the symbol table of the engine objects, so a
 * firmware update that adds state fails the build instead of leaking state
 * between plugin instances.
 *
 * Deliberately not captured (shared, never used by the plugins):
 *   fxBus, FX_BUFFER   - master FX bus; the voice plugins never route to it
 *   seq_patternSet     - sequencer data, only used on the sequencer page
 *   MIDI byte-stream parser state in MidiParser.c (static) - unused, the
 *     plugins call midiParser_ccHandler() directly
 * ====================================================================== */
extern uint8_t midiParser_selectedLfoVoice[];
#define LXR_NUM_LFO 6 /* NUM_LFO in MidiParser.c */
void mixer_decimateBlock(const uint8_t voiceNr, int16_t* buffer); /* mixer.c, not in mixer.h */
extern float mixer_decimation_cnt[6];
extern int16_t mixer_voice_samples[6];
extern uint8_t r1_Available, r2_Available, l1_Available, l2_Available;

typedef struct { void* ptr; size_t size; } LxrStateEntry;

#define LXR_STATE(var) { (void*)&(var), sizeof(var) }
static const LxrStateEntry lxr_state_entries[] = {
    LXR_STATE(voiceArray),
    LXR_STATE(snareVoice),
    LXR_STATE(cymbalVoice),
    LXR_STATE(hatVoice),
    LXR_STATE(velocityModulators),
    LXR_STATE(parameterArray),
    LXR_STATE(mixer_audioRouting),
    LXR_STATE(mixer_decimation_rate),
    LXR_STATE(mixer_decimation_cnt),
    LXR_STATE(mixer_voice_samples),
    LXR_STATE(midiParser_originalCcValues),
    { (void*)midiParser_selectedLfoVoice, LXR_NUM_LFO },
    LXR_STATE(midi_MidiChannels),
    LXR_STATE(voiceStatus),
    LXR_STATE(activeParameters),
    LXR_STATE(paramToModTarget),
    LXR_STATE(lxr_bpm),
    LXR_STATE(lxr_rng),
};
#define LXR_NUM_STATE_ENTRIES (sizeof(lxr_state_entries) / sizeof(lxr_state_entries[0]))

static size_t lxr_state_bytes(void)
{
    size_t n = 0;
    for (size_t i = 0; i < LXR_NUM_STATE_ENTRIES; ++i)
        n += lxr_state_entries[i].size;
    return n;
}

size_t lxr_state_size(void) { return lxr_state_bytes(); }

void lxr_state_save(void* blob)
{
    uint8_t* p = (uint8_t*)blob;
    for (size_t i = 0; i < LXR_NUM_STATE_ENTRIES; ++i) {
        memcpy(p, lxr_state_entries[i].ptr, lxr_state_entries[i].size);
        p += lxr_state_entries[i].size;
    }
}

void lxr_state_load(const void* blob)
{
    const uint8_t* p = (const uint8_t*)blob;
    for (size_t i = 0; i < LXR_NUM_STATE_ENTRIES; ++i) {
        memcpy(lxr_state_entries[i].ptr, p, lxr_state_entries[i].size);
        p += lxr_state_entries[i].size;
    }
}

/* Pristine post-init state, captured once by lxr_global_init(). Sized for
 * the current firmware; lxr_global_init() checks it fits. */
static uint8_t lxr_pristine[16384];
static int lxr_initialised = 0;

void lxr_state_reset(void) { lxr_state_load(lxr_pristine); }

/* ======================================================================
 * Initialisation - mirrors soundengine_init() minus codec/sequencer.
 * ====================================================================== */
void lxr_global_init(void)
{
    if (lxr_initialised)
        return;

    memset(activeParameters, 0, sizeof(activeParameters));

    mixer_init();
    parameterArray_init();
    for (int i = 0; i < 6; i++)
        modNode_init(&velocityModulators[i]);
    lxr_bpm = 120;
    memset(midi_MidiChannels, 0, 8);

    initDrumVoice();
    Snare_init();
    HiHat_init();
    Cymbal_init();

    paramToModTargetInit();

    if (lxr_state_bytes() > sizeof(lxr_pristine)) {
        /* The firmware grew; enlarge lxr_pristine. Fail loudly in debug. */
        return;
    }
    lxr_state_save(lxr_pristine);
    lxr_initialised = 1;
}

/* ======================================================================
 * Parameters
 * ====================================================================== */
static ModulationNode* lxr_lfo_target(uint8_t voice)
{
    switch (voice) {
    case 0: case 1: case 2: return &voiceArray[voice].lfo.modTarget;
    case 3: return &snareVoice.lfo.modTarget;
    case 4: return &cymbalVoice.lfo.modTarget;
    default: return &hatVoice.lfo.modTarget;
    }
}

uint8_t lxr_num_mod_targets(void) { return getNumModTargets(); }

void lxr_mod_target_range(uint8_t voice, uint8_t* start, uint8_t* end)
{
    *start = modTargetVoiceOffsets[voice].start;
    *end = modTargetVoiceOffsets[voice].end;
}

uint16_t lxr_mod_target_param(uint8_t idx) { return modTargets[idx].param; }

/* What preset_sendDrumsetParametersImmediate() / CortexParser do for the
 * velocity and LFO destinations: translate modTargets index -> param number. */
static void lxr_apply_mod_targets_for(uint8_t slot)
{
    const uint8_t velo = (uint8_t)modTargets[activeParameters[PAR_VEL_DEST_1 + slot]].param;
    modNode_setDestination(&velocityModulators[slot], velo);

    if (activeParameters[PAR_VOICE_LFO1 + slot] < 1 || activeParameters[PAR_VOICE_LFO1 + slot] > 6)
        activeParameters[PAR_VOICE_LFO1 + slot] = 1;

    const uint8_t lfo = (uint8_t)modTargets[activeParameters[PAR_TARGET_LFO1 + slot]].param;
    modNode_setDestination(lxr_lfo_target(slot), lfo);
}

/* The CC path used by the preset loader, the morph and the front panel. */
static void lxr_send_cc(uint16_t par, uint8_t value)
{
    /* PAR_NONE: parameterArray[0].ptr is NULL. On the STM32 the handler reads
     * address 0 (flash) harmlessly; on a host that faults. It has no audible
     * effect on the hardware, so skip it. parameterOrder[] is zero-padded. */
    if (par == PAR_NONE)
        return;
    MidiMsg msg;
    memset(&msg, 0, sizeof(msg));
    if (par < 128) {
        msg.status = MIDI_CC;
        msg.data1 = (uint8_t)((par + 1) & 0x7f); /* the firmware's "+1 offset" */
    } else {
        msg.status = CC_2;
        msg.data1 = (uint8_t)((par - 128) & 0x7f);
    }
    msg.data2 = value;
    midiParser_ccHandler(msg, 1, 0, 0.f);
}

void lxr_set_param(uint16_t par, uint8_t value)
{
    if (par >= NUM_PARAMS)
        return;
    if (par >= END_OF_SOUND_PARAMETERS) {
        /* Global (non-kit) settings such as PAR_MIDI_NOTEx: the firmware only
         * keeps them in the edit buffer, where midiParser_noteOn reads them. */
        activeParameters[par] = value;
        return;
    }

    if ((par >= PAR_VEL_DEST_1 && par <= PAR_VEL_DEST_6)
        || (par >= PAR_TARGET_LFO1 && par <= PAR_TARGET_LFO6)) {
        if (value >= getNumModTargets())
            value = 0;
        activeParameters[par] = value;
        const uint8_t slot = (par >= PAR_TARGET_LFO1) ? (uint8_t)(par - PAR_TARGET_LFO1)
                                                      : (uint8_t)(par - PAR_VEL_DEST_1);
        lxr_apply_mod_targets_for(slot);
        return;
    }

    activeParameters[par] = value;
    lxr_send_cc(par, value);
}

uint8_t lxr_get_param(uint16_t par)
{
    return par < NUM_PARAMS ? activeParameters[par] : 0;
}

void lxr_load_init_kit(void)
{
    /* preset_init(): name in bytes 0..7, then the parameters. The firmware
     * copies initKit[i] -> param[i-8] for i in [8, END_OF_SOUND_PARAMETERS). */
    memset(activeParameters, 0, END_OF_SOUND_PARAMETERS);
    for (int i = 8; i < END_OF_SOUND_PARAMETERS; i++)
        activeParameters[i - 8] = initKit[i];

    const uint8_t nmt = getNumModTargets();
    for (int i = 0; i < 6; i++) {
        if (activeParameters[PAR_VEL_DEST_1 + i] >= nmt)
            activeParameters[PAR_VEL_DEST_1 + i] = 0;
        if (activeParameters[PAR_TARGET_LFO1 + i] >= nmt)
            activeParameters[PAR_TARGET_LFO1 + i] = 0;
    }

    /* preset_sendDrumsetParametersImmediate() */
    for (uint8_t i = 0; i < 6; i++)
        lxr_apply_mod_targets_for(i);
    for (uint32_t i = 0; i < END_OF_SOUND_PARAMETERS; i++) {
        const uint8_t p = parameterOrder[i];
        lxr_send_cc(p, activeParameters[p]);
    }
}

/* ======================================================================
 * Notes, tempo
 * ====================================================================== */
int lxr_note_on(uint8_t trigger, uint8_t note, uint8_t velocity)
{
    if (trigger > LXR_TRIGGER_HIHAT_OPEN)
        return 0;

    /* midiParser_noteOn(), outside the sequencer page */
    if (activeParameters[PAR_MIDI_NOTE1 + trigger] != 0) {
        if (note == activeParameters[PAR_MIDI_NOTE1 + trigger])
            note = SEQ_DEFAULT_NOTE;
        else
            return 0;
    }
    voiceControl_noteOn(trigger, note, velocity);
    return 1;
}

void lxr_set_bpm(uint16_t bpm)
{
    if (bpm == 0)
        bpm = 1;
    if (bpm == lxr_bpm)
        return;
    lxr_bpm = bpm;
    lfo_recalcSync();
}

void lxr_seed_rng(uint32_t seed) { lxr_rng = seed ? seed : 0x2545F491u; }

void lxr_test_freeze_rng(int frozen) { lxr_rng_frozen = frozen; }

/* ======================================================================
 * Rendering
 * ====================================================================== */
void lxr_render_voice(uint8_t voice, int16_t* out)
{
    /* mixer_calcNextSampleBlock(), restricted to one voice. The other
     * voices' LFOs only ever target their own (silent) voices in a
     * single-voice plugin, so skipping them does not change this voice. */
    modNode_resetTargets();
    modNode_reassignVeloMod();

    int16_t data[LXR_BLOCK_SIZE];
    float panL, panR;

    switch (voice) {
    case LXR_VOICE_DRUM1: case LXR_VOICE_DRUM2: case LXR_VOICE_DRUM3:
        lfo_dispatchNextValue(&voiceArray[voice].lfo);
        SVF_recalcFreq(&voiceArray[voice].filter);
        calcDrumVoiceAsync(voice);
        calcDrumVoiceSyncBlock(voice, data, LXR_BLOCK_SIZE);
        panL = voiceArray[voice].triggeredPanL;
        panR = voiceArray[voice].triggeredPanR;
        break;
    case LXR_VOICE_SNARE:
        lfo_dispatchNextValue(&snareVoice.lfo);
        SVF_recalcFreq(&snareVoice.filter);
        Snare_calcAsync();
        Snare_calcSyncBlock(data, LXR_BLOCK_SIZE);
        panL = snareVoice.triggeredPanL;
        panR = snareVoice.triggeredPanR;
        break;
    case LXR_VOICE_CYMBAL:
        lfo_dispatchNextValue(&cymbalVoice.lfo);
        SVF_recalcFreq(&cymbalVoice.filter);
        Cymbal_calcAsync();
        Cymbal_calcSyncBlock(data, LXR_BLOCK_SIZE);
        panL = cymbalVoice.triggeredPanL;
        panR = cymbalVoice.triggeredPanR;
        break;
    default:
        lfo_dispatchNextValue(&hatVoice.lfo);
        SVF_recalcFreq(&hatVoice.filter);
        HiHat_calcAsync();
        HiHat_calcSyncBlock(data, LXR_BLOCK_SIZE);
        panL = hatVoice.triggeredPanL;
        panR = hatVoice.triggeredPanR;
        voice = LXR_VOICE_HIHAT;
        break;
    }

    mixer_decimateBlock(voice, data);

    /* mixer_addDataToOutput(MIXER_ROUTING_DAC1_STEREO, ...) into a cleared
     * buffer: QADD16 with zero is the packed pair itself. */
    for (int i = 0; i < LXR_BLOCK_SIZE; i++) {
        out[2 * i] = (int16_t)(data[i] * panL);
        out[2 * i + 1] = (int16_t)(data[i] * panR);
    }
}

void lxr_render_mixer_reference(int16_t* out1, int16_t* out2)
{
    mixer_calcNextSampleBlock(out1, out2);
}
