#pragma once

#include <cstdint>

// Include the generated effects from the parent directory
#include "../../decompiled-midiverb.h"
#include "../../decompiled-midifex.h"
#include "../../decompiled-midiverb2.h"

// Effect names
static const char* names_midiverb[] = {
#include "../../names-midiverb.h"
};

static const char* names_midifex[] = {
#include "../../names-midifex.h"
};

static const char* names_midiverb2[] = {
#include "../../names-midiverb2.h"
};

// Effect function pointer type (with LFO parameters for MIDIVerb 2 compatibility)
using EffectFn = void (*)(int16_t input, int16_t *out_left, int16_t *out_right, int16_t *DRAM, int ptr, uint32_t lfo1_value, uint32_t lfo2_value);

// Device info structure
struct DeviceInfo {
    const char* name;
    int numEffects;     // Number of effects
    int displayOffset;  // Added to 0-based index for display (1 for MIDIVerb/MidiFex, 0 for MIDIVerb 2)
    const char** effectNames;
    EffectFn* effects;
    double sampleRate;    // DSP sample rate of the original unit
    int analogModel;      // 0: MIDIVerb/MIDIFex schematic model, 1: MIDIVerb II estimate
    bool adc12bit;        // true: 12-bit SAR ADC, LSB of the 13-bit DSP input always 0
};

// Sample rates:
//  MIDIVerb / MIDIFex: 6 MHz resonator / 256 = 23437.5 Hz (MIDIVerb_RE slides, schematic Y1).
//  MIDIVerb II: 16 MHz crystal / 512 = 31250 Hz. Inferred: the divider chain is not traced,
//  but the delay programs only match their names at this rate (e.g. #89 "460 Msec" is
//  14063 samples = 450 ms), and the converter filters sit just below its 15.6 kHz Nyquist.
static const DeviceInfo devices[] = {
    { "MIDIVerb",   64,  1, names_midiverb,  midiverb_effects,  23437.5, 0, true  },
    { "MidiFex",    64,  1, names_midifex,   midifex_effects,   23437.5, 0, true  },
    { "MIDIVerb 2", 100, 0, names_midiverb2, midiverb2_effects, 31250.0, 1, false },
};

static constexpr int NUM_DEVICES = 3;
