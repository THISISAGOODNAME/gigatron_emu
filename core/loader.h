/**
 * Gigatron GT1 File Loader
 * 
 * Implements the GT1 file format loader that transfers programs
 * to the Gigatron via the input register, simulating serial loading.
 */

#ifndef GIGATRON_LOADER_H
#define GIGATRON_LOADER_H

#include "gigatron.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loader constants */
#define LOADER_MAX_PAYLOAD_SIZE     60
#define LOADER_START_OF_FRAME       0x4C    /* 'L' */
#define LOADER_INIT_CHECKSUM        0x67    /* 'g' */

/* Loader states */
typedef enum loader_state_t {
    LOADER_IDLE,            /* Not loading */
    LOADER_RESET_WAIT,      /* Waiting after CPU reset */
    LOADER_MENU_NAV,        /* Navigating to loader in menu */
    LOADER_SYNC_FRAME,      /* Sending sync frame with bad checksum */
    LOADER_SENDING,         /* Sending GT1 data */
    LOADER_START_CMD,       /* Sending start command */
    LOADER_COMPLETE,        /* Loading complete */
    LOADER_ERROR            /* Error occurred */
} loader_state_t;

/* Frame sending sub-states */
typedef enum frame_state_t {
    FRAME_WAIT_VSYNC_NEG,       /* Wait for VSYNC falling edge */
    FRAME_WAIT_HSYNC_1,         /* Wait for first HSYNC rising edge */
    FRAME_WAIT_HSYNC_2,         /* Wait for second HSYNC rising edge */
    FRAME_SEND_FIRST_BYTE,      /* Sending first byte (8 bits) */
    FRAME_SEND_LENGTH,          /* Sending length (6 bits) */
    FRAME_SEND_ADDR_LOW,        /* Sending low address byte (8 bits) */
    FRAME_SEND_ADDR_HIGH,       /* Sending high address byte (8 bits) */
    FRAME_SEND_PAYLOAD,         /* Sending payload bytes */
    FRAME_SEND_CHECKSUM,        /* Sending checksum (8 bits) */
    FRAME_DONE                  /* Frame complete */
} frame_state_t;

/**
 * GT1 segment
 */
typedef struct gt1_segment_t {
    uint16_t address;       /* Load address */
    uint16_t size;          /* Segment size (0 = 256) */
    uint8_t* data;          /* Segment data */
} gt1_segment_t;

/**
 * GT1 file structure
 */
typedef struct gt1_file_t {
    gt1_segment_t* segments;
    uint32_t num_segments;
    uint16_t start_address;
    bool has_start_address;
} gt1_file_t;

/**
 * Loader state
 */
typedef struct loader_t {
    /* Reference to CPU */
    gigatron_t* cpu;
    
    /* Current state */
    loader_state_t state;
    
    /* GT1 file being loaded */
    gt1_file_t* gt1;
    
    /* Segment/frame tracking */
    uint32_t current_segment;
    uint32_t segment_offset;        /* Offset within current segment */
    
    /* Frame sending state */
    frame_state_t frame_state;
    uint8_t frame_first_byte;
    uint8_t frame_length;
    uint16_t frame_addr;
    uint8_t frame_payload[LOADER_MAX_PAYLOAD_SIZE];
    uint8_t frame_checksum;
    
    /* Bit sending state */
    uint8_t current_byte;           /* Byte being sent */
    uint8_t bits_remaining;         /* Bits left to send in current byte */
    uint8_t payload_index;          /* Current payload byte index */
    
    /* Timing counters */
    uint32_t vsync_count;
    uint32_t button_timer;
    
    /* Edge detection */
    uint8_t prev_out;
    
    /* Checksum accumulator */
    uint8_t checksum;
    
    /* Error message */
    const char* error_msg;
} loader_t;

/**
 * Initialize loader.
 */
bool loader_init(loader_t* loader, gigatron_t* cpu);

/**
 * Shutdown loader.
 */
void loader_shutdown(loader_t* loader);

/**
 * Reset loader state.
 */
void loader_reset(loader_t* loader);

/**
 * Parse GT1 file from memory buffer.
 * Returns allocated gt1_file_t on success, NULL on failure.
 */
gt1_file_t* loader_parse_gt1(const uint8_t* data, size_t size);

/**
 * Free GT1 file structure.
 */
void loader_free_gt1(gt1_file_t* gt1);

/**
 * Load GT1 file from disk.
 * Returns allocated gt1_file_t on success, NULL on failure.
 */
gt1_file_t* loader_load_gt1_file(const char* filename);

/**
 * Start loading a GT1 file.
 * Takes ownership of the gt1 structure.
 */
bool loader_start(loader_t* loader, gt1_file_t* gt1);

/**
 * Advance loader by one tick.
 * Should be called once per CPU cycle.
 */
void loader_tick(loader_t* loader);

/**
 * Check if loader is active (loading in progress).
 */
static inline bool loader_is_active(const loader_t* loader) {
    return loader && loader->state != LOADER_IDLE && 
           loader->state != LOADER_COMPLETE && 
           loader->state != LOADER_ERROR;
}

/**
 * Check if loading is complete.
 */
static inline bool loader_is_complete(const loader_t* loader) {
    return loader && loader->state == LOADER_COMPLETE;
}

/**
 * Check if loader encountered an error.
 */
static inline bool loader_has_error(const loader_t* loader) {
    return loader && loader->state == LOADER_ERROR;
}

/**
 * Get error message (if any).
 */
static inline const char* loader_get_error(const loader_t* loader) {
    return loader ? loader->error_msg : NULL;
}

/**
 * Get loading progress (0.0 to 1.0).
 */
float loader_get_progress(const loader_t* loader);

#ifdef __cplusplus
}
#endif

#endif /* GIGATRON_LOADER_H */
