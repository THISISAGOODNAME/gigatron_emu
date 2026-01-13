/**
 * Gigatron Audio Output Emulation
 */

#include "audio.h"
#include <stdlib.h>
#include <string.h>

/**
 * Initialize audio emulation
 */
bool audio_init(audio_t* audio, gigatron_t* cpu) {
    if (!audio || !cpu) return false;
    
    memset(audio, 0, sizeof(audio_t));
    
    audio->cpu = cpu;
    audio->sample_rate = AUDIO_SAMPLE_RATE;
    audio->volume = 1.0f;
    audio->mute = false;
    
    /* High-pass filter coefficient for DC removal */
    audio->alpha = 0.99f;
    audio->bias = 0.0f;
    
    /* Allocate sample buffer */
    audio->buffer.size = AUDIO_BUFFER_SIZE * AUDIO_NUM_BUFFERS;
    audio->buffer.samples = (float*)calloc(audio->buffer.size, sizeof(float));
    if (!audio->buffer.samples) {
        return false;
    }
    
    audio->buffer.write_pos = 0;
    audio->buffer.read_pos = 0;
    
    return true;
}

/**
 * Shutdown audio
 */
void audio_shutdown(audio_t* audio) {
    if (!audio) return;
    
    if (audio->buffer.samples) {
        free(audio->buffer.samples);
        audio->buffer.samples = NULL;
    }
}

/**
 * Reset audio state
 */
void audio_reset(audio_t* audio) {
    if (!audio) return;
    
    audio->cycle_counter = 0;
    audio->bias = 0.0f;
    audio->buffer.write_pos = 0;
    audio->buffer.read_pos = 0;
    
    if (audio->buffer.samples) {
        memset(audio->buffer.samples, 0, audio->buffer.size * sizeof(float));
    }
}

/**
 * Get number of available samples
 */
uint32_t audio_available_samples(const audio_t* audio) {
    if (!audio) return 0;
    
    uint32_t write = audio->buffer.write_pos;
    uint32_t read = audio->buffer.read_pos;
    
    if (write >= read) {
        return write - read;
    } else {
        return audio->buffer.size - read + write;
    }
}

/**
 * Check if buffer is full
 */
bool audio_buffer_full(const audio_t* audio) {
    if (!audio) return true;
    
    uint32_t next_write = (audio->buffer.write_pos + 1) % audio->buffer.size;
    return next_write == audio->buffer.read_pos;
}

/**
 * Write a sample to the buffer
 */
static void audio_write_sample(audio_t* audio, float sample) {
    uint32_t next_write = (audio->buffer.write_pos + 1) % audio->buffer.size;
    
    /* Don't overwrite unread samples */
    if (next_write != audio->buffer.read_pos) {
        audio->buffer.samples[audio->buffer.write_pos] = sample;
        audio->buffer.write_pos = next_write;
    }
}

/**
 * Advance audio simulation by one tick
 */
void audio_tick(audio_t* audio) {
    if (!audio || !audio->cpu) return;
    
    /* Accumulate cycles and generate sample when enough have passed */
    audio->cycle_counter += audio->sample_rate;
    
    if (audio->cycle_counter >= audio->cpu->hz) {
        audio->cycle_counter -= audio->cpu->hz;
        
        /* Get 4-bit audio sample from OUTX (upper 4 bits) */
        float raw_sample = (float)(audio->cpu->outx >> 4) / 8.0f;
        
        /* Apply DC bias removal (high-pass filter) */
        audio->bias = (audio->alpha * audio->bias) + ((1.0f - audio->alpha) * raw_sample);
        float sample = raw_sample - audio->bias;
        
        /* Apply volume */
        sample *= audio->volume;
        
        /* Clamp to [-1, 1] */
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        
        /* Mute if needed */
        if (audio->mute) {
            sample = 0.0f;
        }
        
        /* Write to buffer */
        audio_write_sample(audio, sample);
    }
}

/**
 * Read samples from the audio buffer
 */
uint32_t audio_read_samples(audio_t* audio, float* out_samples, uint32_t count) {
    if (!audio || !out_samples || count == 0) return 0;
    
    uint32_t available = audio_available_samples(audio);
    uint32_t to_read = (count < available) ? count : available;
    
    for (uint32_t i = 0; i < to_read; i++) {
        out_samples[i] = audio->buffer.samples[audio->buffer.read_pos];
        audio->buffer.read_pos = (audio->buffer.read_pos + 1) % audio->buffer.size;
    }
    
    return to_read;
}
