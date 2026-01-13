/**
 * Gigatron VGA Output Emulation
 * 
 * Emulates the VGA signal timing and generates a framebuffer.
 */

#ifndef GIGATRON_VGA_H
#define GIGATRON_VGA_H

#include "gigatron.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VGA timing configuration */
#define VGA_H_FRONT_PORCH   16
#define VGA_H_BACK_PORCH    48
#define VGA_H_VISIBLE       640

#define VGA_V_FRONT_PORCH   10
#define VGA_V_BACK_PORCH    34
#define VGA_V_VISIBLE       480

/* Total VGA dimensions */
#define VGA_WIDTH           VGA_H_VISIBLE
#define VGA_HEIGHT          VGA_V_VISIBLE

/**
 * VGA state
 */
typedef struct vga_t {
    /* Reference to CPU */
    gigatron_t* cpu;
    
    /* Framebuffer (RGBA format, 4 bytes per pixel) */
    uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    
    /* Timing state */
    uint16_t row;           /* Current scanline */
    uint16_t col;           /* Current column (in Gigatron pixels, 4x horizontal) */
    uint32_t pixel_index;   /* Current pixel index in framebuffer */
    
    /* Previous OUT register value for edge detection */
    uint8_t prev_out;
    
    /* Timing boundaries */
    uint16_t min_row;
    uint16_t max_row;
    uint16_t min_col;
    uint16_t max_col;
    
    /* Frame counter */
    uint32_t frame_count;
    
    /* Flag to indicate frame complete */
    bool frame_complete;
} vga_t;

/**
 * Initialize VGA emulation.
 * Returns true on success, false on failure.
 */
bool vga_init(vga_t* vga, gigatron_t* cpu);

/**
 * Shutdown VGA and free framebuffer.
 */
void vga_shutdown(vga_t* vga);

/**
 * Reset VGA state.
 */
void vga_reset(vga_t* vga);

/**
 * Advance VGA simulation by one tick.
 * Should be called once per CPU cycle.
 */
void vga_tick(vga_t* vga);

/**
 * Get pointer to framebuffer.
 * Format: RGBA, width * height * 4 bytes
 */
static inline const uint8_t* vga_get_framebuffer(const vga_t* vga) {
    return vga->pixels;
}

/**
 * Check if a new frame was completed since last check.
 * Resets the flag after reading.
 */
static inline bool vga_frame_ready(vga_t* vga) {
    bool ready = vga->frame_complete;
    vga->frame_complete = false;
    return ready;
}

/**
 * Get frame count.
 */
static inline uint32_t vga_get_frame_count(const vga_t* vga) {
    return vga->frame_count;
}

/**
 * Convert 6-bit Gigatron color to RGBA.
 * Input: RRGGBB (2 bits each)
 * Output: R, G, B, A (8 bits each)
 */
static inline void vga_color_to_rgba(uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b) {
    /* Expand 2-bit color to 8-bit by replicating bits */
    uint8_t r2 = (color >> 4) & 0x03;
    uint8_t g2 = (color >> 2) & 0x03;
    uint8_t b2 = color & 0x03;
    
    /* Map 2-bit to 8-bit: 0->0, 1->85, 2->170, 3->255 */
    *r = (r2 << 6) | (r2 << 4) | (r2 << 2) | r2;
    *g = (g2 << 6) | (g2 << 4) | (g2 << 2) | g2;
    *b = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;
}

#ifdef __cplusplus
}
#endif

#endif /* GIGATRON_VGA_H */
