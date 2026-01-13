/**
 * Gigatron Audio Output Emulation
 * 
 * Generates audio samples from the OUTX register.
 */

#ifndef GIGATRON_AUDIO_H
#define GIGATRON_AUDIO_H

#include "gigatron.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audio configuration */
#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_BUFFER_SIZE   2048    /* Samples per buffer */
#define AUDIO_NUM_BUFFERS   4       /* Number of buffers for double/triple buffering */

/**
 * Audio ring buffer for samples
 */
typedef struct audio_buffer_t {
    float* samples;
    uint32_t size;
    uint32_t write_pos;
    uint32_t read_pos;
} audio_buffer_t;

/**
 * Audio state
 */
typedef struct audio_t {
    /* Reference to CPU */
    gigatron_t* cpu;
    
    /* Sample rate */
    uint32_t sample_rate;
    
    /* Cycle counter for sample timing */
    uint32_t cycle_counter;
    
    /* DC bias removal (high-pass filter) */
    float bias;
    float alpha;    /* Filter coefficient */
    
    /* Volume (0.0 to 1.0) */
    float volume;
    
    /* Mute flag */
    bool mute;
    
    /* Sample buffer */
    audio_buffer_t buffer;
} audio_t;

/**
 * Initialize audio emulation.
 * Returns true on success, false on failure.
 */
bool audio_init(audio_t* audio, gigatron_t* cpu);

/**
 * Shutdown audio and free resources.
 */
void audio_shutdown(audio_t* audio);

/**
 * Reset audio state.
 */
void audio_reset(audio_t* audio);

/**
 * Advance audio simulation by one tick.
 * Should be called once per CPU cycle.
 */
void audio_tick(audio_t* audio);

/**
 * Read samples from the audio buffer.
 * Returns the number of samples read.
 */
uint32_t audio_read_samples(audio_t* audio, float* out_samples, uint32_t count);

/**
 * Get number of available samples in buffer.
 */
uint32_t audio_available_samples(const audio_t* audio);

/**
 * Check if buffer is full.
 */
bool audio_buffer_full(const audio_t* audio);

/**
 * Set volume (0.0 to 1.0).
 */
static inline void audio_set_volume(audio_t* audio, float volume) {
    if (audio) {
        audio->volume = (volume < 0.0f) ? 0.0f : ((volume > 1.0f) ? 1.0f : volume);
    }
}

/**
 * Set mute state.
 */
static inline void audio_set_mute(audio_t* audio, bool mute) {
    if (audio) {
        audio->mute = mute;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* GIGATRON_AUDIO_H */
