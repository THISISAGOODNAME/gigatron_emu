/**
 * Gigatron VGA Output Emulation
 */

#include "vga.h"
#include <stdlib.h>
#include <string.h>

/**
 * Initialize VGA emulation
 */
bool vga_init(vga_t* vga, gigatron_t* cpu) {
    if (!vga || !cpu) return false;
    
    memset(vga, 0, sizeof(vga_t));
    
    vga->cpu = cpu;
    vga->width = VGA_WIDTH;
    vga->height = VGA_HEIGHT;
    
    /* Allocate framebuffer (RGBA) */
    size_t fb_size = (size_t)vga->width * vga->height * 4;
    vga->pixels = (uint8_t*)malloc(fb_size);
    if (!vga->pixels) {
        return false;
    }
    
    /* Initialize to black with full alpha */
    for (size_t i = 0; i < fb_size; i += 4) {
        vga->pixels[i + 0] = 0;     /* R */
        vga->pixels[i + 1] = 0;     /* G */
        vga->pixels[i + 2] = 0;     /* B */
        vga->pixels[i + 3] = 255;   /* A */
    }
    
    /* Set timing boundaries */
    vga->min_row = VGA_V_BACK_PORCH;
    vga->max_row = VGA_V_BACK_PORCH + VGA_V_VISIBLE;
    vga->min_col = VGA_H_BACK_PORCH;
    vga->max_col = VGA_H_BACK_PORCH + VGA_H_VISIBLE;
    
    vga_reset(vga);
    
    return true;
}

/**
 * Shutdown VGA
 */
void vga_shutdown(vga_t* vga) {
    if (!vga) return;
    
    if (vga->pixels) {
        free(vga->pixels);
        vga->pixels = NULL;
    }
}

/**
 * Reset VGA state
 */
void vga_reset(vga_t* vga) {
    if (!vga) return;
    
    vga->row = 0;
    vga->col = 0;
    vga->pixel_index = 0;
    vga->prev_out = 0;
    vga->frame_complete = false;
}

/**
 * Advance VGA simulation by one tick
 */
void vga_tick(vga_t* vga) {
    if (!vga || !vga->cpu) return;
    
    uint8_t out = vga->cpu->out;
    uint8_t falling = vga->prev_out & ~out;
    
    /* Detect falling edge of VSYNC */
    if (falling & GIGATRON_OUT_VSYNC) {
        vga->row = 0;
        vga->pixel_index = 0;
        vga->frame_complete = true;
        vga->frame_count++;
    }
    
    /* Detect falling edge of HSYNC */
    if (falling & GIGATRON_OUT_HSYNC) {
        vga->col = 0;
        vga->row++;
    }
    
    vga->prev_out = out;
    
    /* Check if we're in blanking interval */
    if ((out & (GIGATRON_OUT_VSYNC | GIGATRON_OUT_HSYNC)) != 
        (GIGATRON_OUT_VSYNC | GIGATRON_OUT_HSYNC)) {
        return;
    }
    
    /* Check if we're in visible area */
    if (vga->row >= vga->min_row && vga->row < vga->max_row &&
        vga->col >= vga->min_col && vga->col < vga->max_col) {
        
        /* Get color from OUT register */
        uint8_t color = out & 0x3F;
        uint8_t r, g, b;
        vga_color_to_rgba(color, &r, &g, &b);
        
        /* Each Gigatron pixel is 4 VGA pixels wide */
        uint8_t* pixels = vga->pixels;
        uint32_t idx = vga->pixel_index;
        
        for (int i = 0; i < 4; i++) {
            pixels[idx++] = r;
            pixels[idx++] = g;
            pixels[idx++] = b;
            pixels[idx++] = 255;
        }
        
        vga->pixel_index = idx;
    }
    
    /* Advance by 4 columns (Gigatron outputs 1 pixel per tick, 4x VGA) */
    vga->col += 4;
}
