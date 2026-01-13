/**
 * Gigatron GT1 File Loader
 * 
 * The GT1 loader simulates the serial loading process used by the real
 * Gigatron to receive programs through the controller port.
 */

#include "loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Internal frame sending states */
enum {
    FRAME_IDLE,
    FRAME_WAIT_VSYNC_NEG,
    FRAME_WAIT_HSYNC_POS_1,
    FRAME_WAIT_HSYNC_POS_2,
    FRAME_SEND_FIRST_BYTE,
    FRAME_SEND_LENGTH,
    FRAME_SEND_ADDR_LOW,
    FRAME_SEND_ADDR_HIGH,
    FRAME_SEND_PAYLOAD,
    FRAME_SEND_CHECKSUM,
    FRAME_DONE
};

/**
 * Initialize loader
 */
bool loader_init(loader_t* loader, gigatron_t* cpu) {
    if (!loader || !cpu) return false;
    
    memset(loader, 0, sizeof(loader_t));
    loader->cpu = cpu;
    loader->state = LOADER_IDLE;
    
    return true;
}

/**
 * Shutdown loader
 */
void loader_shutdown(loader_t* loader) {
    if (!loader) return;
    
    if (loader->gt1) {
        loader_free_gt1(loader->gt1);
        loader->gt1 = NULL;
    }
}

/**
 * Reset loader state
 */
void loader_reset(loader_t* loader) {
    if (!loader) return;
    
    if (loader->gt1) {
        loader_free_gt1(loader->gt1);
        loader->gt1 = NULL;
    }
    
    loader->state = LOADER_IDLE;
    loader->current_segment = 0;
    loader->segment_offset = 0;
    loader->frame_state = FRAME_IDLE;
    loader->bit_index = 0;
    loader->checksum = 0;
    loader->vsync_count = 0;
    loader->hsync_count = 0;
    loader->prev_out = 0;
    loader->error_msg = NULL;
}

/**
 * Parse GT1 file from memory
 * 
 * GT1 format:
 * - Segments: [high_addr][low_addr][size][data...]
 *   - size=0 means 256 bytes
 * - End marker: [0x00][high_start][low_start]
 *   - If start_addr is 0, execution continues at vCPU
 */
gt1_file_t* loader_parse_gt1(const uint8_t* data, size_t size) {
    if (!data || size < 3) return NULL;
    
    gt1_file_t* gt1 = (gt1_file_t*)calloc(1, sizeof(gt1_file_t));
    if (!gt1) return NULL;
    
    /* First pass: count segments */
    size_t offset = 0;
    uint32_t num_segments = 0;
    
    while (offset < size) {
        if (data[offset] == 0x00 && offset > 0) {
            /* End marker */
            break;
        }
        
        if (offset + 3 > size) {
            free(gt1);
            return NULL;
        }
        
        /* uint16_t addr = ((uint16_t)data[offset] << 8) | data[offset + 1]; */
        offset += 2;
        
        uint16_t seg_size = data[offset];
        if (seg_size == 0) seg_size = 256;
        offset += 1;
        
        if (offset + seg_size > size) {
            free(gt1);
            return NULL;
        }
        
        offset += seg_size;
        num_segments++;
    }
    
    if (num_segments == 0) {
        free(gt1);
        return NULL;
    }
    
    /* Allocate segments */
    gt1->segments = (gt1_segment_t*)calloc(num_segments, sizeof(gt1_segment_t));
    if (!gt1->segments) {
        free(gt1);
        return NULL;
    }
    gt1->num_segments = num_segments;
    
    /* Second pass: parse segments */
    offset = 0;
    uint32_t seg_idx = 0;
    
    while (offset < size && seg_idx < num_segments) {
        if (data[offset] == 0x00 && offset > 0) {
            break;
        }
        
        uint16_t addr = ((uint16_t)data[offset] << 8) | data[offset + 1];
        offset += 2;
        
        uint16_t seg_size = data[offset];
        if (seg_size == 0) seg_size = 256;
        offset += 1;
        
        gt1->segments[seg_idx].address = addr;
        gt1->segments[seg_idx].size = seg_size;
        gt1->segments[seg_idx].data = (uint8_t*)malloc(seg_size);
        
        if (!gt1->segments[seg_idx].data) {
            loader_free_gt1(gt1);
            return NULL;
        }
        
        memcpy(gt1->segments[seg_idx].data, data + offset, seg_size);
        offset += seg_size;
        seg_idx++;
    }
    
    /* Parse end marker */
    if (offset < size && data[offset] == 0x00) {
        offset++;
        if (offset + 2 <= size) {
            gt1->start_address = ((uint16_t)data[offset] << 8) | data[offset + 1];
            gt1->has_start_address = (gt1->start_address != 0);
        }
    }
    
    return gt1;
}

/**
 * Free GT1 file structure
 */
void loader_free_gt1(gt1_file_t* gt1) {
    if (!gt1) return;
    
    if (gt1->segments) {
        for (uint32_t i = 0; i < gt1->num_segments; i++) {
            if (gt1->segments[i].data) {
                free(gt1->segments[i].data);
            }
        }
        free(gt1->segments);
    }
    
    free(gt1);
}

/**
 * Load GT1 file from disk
 */
gt1_file_t* loader_load_gt1_file(const char* filename) {
    if (!filename) return NULL;
    
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(f);
        return NULL;
    }
    
    uint8_t* buffer = (uint8_t*)malloc((size_t)file_size);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, f);
    fclose(f);
    
    if (bytes_read != (size_t)file_size) {
        free(buffer);
        return NULL;
    }
    
    gt1_file_t* gt1 = loader_parse_gt1(buffer, bytes_read);
    free(buffer);
    
    return gt1;
}

/**
 * Start loading a GT1 file
 */
bool loader_start(loader_t* loader, gt1_file_t* gt1) {
    if (!loader || !loader->cpu || !gt1) return false;
    
    /* Clean up any previous load */
    if (loader->gt1) {
        loader_free_gt1(loader->gt1);
    }
    
    loader->gt1 = gt1;
    loader->current_segment = 0;
    loader->segment_offset = 0;
    loader->frame_state = FRAME_IDLE;
    loader->bit_index = 0;
    loader->vsync_count = 0;
    loader->hsync_count = 0;
    loader->error_msg = NULL;
    
    /* Start by resetting the CPU */
    gigatron_reset(loader->cpu);
    loader->state = LOADER_RESET_WAIT;
    
    return true;
}

/**
 * Shift one bit into the input register
 */
static void shift_bit(loader_t* loader, uint8_t bit) {
    loader->cpu->in_reg = ((loader->cpu->in_reg << 1) & 0xFF) | (bit ? 1 : 0);
}

/**
 * Get loading progress
 */
float loader_get_progress(const loader_t* loader) {
    if (!loader || !loader->gt1 || loader->gt1->num_segments == 0) {
        return 0.0f;
    }
    
    if (loader->state == LOADER_COMPLETE) {
        return 1.0f;
    }
    
    if (loader->state == LOADER_IDLE || loader->state == LOADER_ERROR) {
        return 0.0f;
    }
    
    /* Calculate total bytes and bytes sent */
    uint32_t total_bytes = 0;
    uint32_t sent_bytes = 0;
    
    for (uint32_t i = 0; i < loader->gt1->num_segments; i++) {
        total_bytes += loader->gt1->segments[i].size;
        if (i < loader->current_segment) {
            sent_bytes += loader->gt1->segments[i].size;
        } else if (i == loader->current_segment) {
            sent_bytes += loader->segment_offset;
        }
    }
    
    return total_bytes > 0 ? (float)sent_bytes / (float)total_bytes : 0.0f;
}

/**
 * Check for positive edge of signal
 */
static bool posedge(loader_t* loader, uint8_t mask) {
    return (~loader->prev_out & loader->cpu->out & mask) != 0;
}

/**
 * Check for negative edge of signal
 */
static bool negedge(loader_t* loader, uint8_t mask) {
    return (loader->prev_out & ~loader->cpu->out & mask) != 0;
}

/**
 * Advance loader by one tick
 * 
 * This implements a simplified version of the loading protocol.
 * The real protocol is quite complex with precise timing requirements.
 * This version uses a simpler state machine approach.
 */
void loader_tick(loader_t* loader) {
    if (!loader || !loader->cpu) return;
    
    uint8_t out = loader->cpu->out;
    
    /* Handle different loader states */
    switch (loader->state) {
        case LOADER_IDLE:
        case LOADER_COMPLETE:
        case LOADER_ERROR:
            /* Nothing to do */
            break;
            
        case LOADER_RESET_WAIT:
            /* Wait for some VSYNCs after reset */
            if (negedge(loader, GIGATRON_OUT_VSYNC)) {
                loader->vsync_count++;
                if (loader->vsync_count >= 100) {
                    loader->state = LOADER_MENU_NAV;
                    loader->vsync_count = 0;
                }
            }
            break;
            
        case LOADER_MENU_NAV:
            /* Navigate to loader in menu */
            /* Press DOWN 5 times, then A */
            if (negedge(loader, GIGATRON_OUT_VSYNC)) {
                loader->vsync_count++;
                
                if (loader->vsync_count <= 5) {
                    /* Press DOWN */
                    loader->cpu->in_reg = GIGATRON_BTN_DOWN ^ 0xFF;
                } else if (loader->vsync_count <= 6) {
                    /* Release */
                    loader->cpu->in_reg = 0xFF;
                } else if (loader->vsync_count <= 10) {
                    /* More DOWNs with releases */
                    if (loader->vsync_count % 2 == 1) {
                        loader->cpu->in_reg = GIGATRON_BTN_DOWN ^ 0xFF;
                    } else {
                        loader->cpu->in_reg = 0xFF;
                    }
                } else if (loader->vsync_count == 11) {
                    /* Press A */
                    loader->cpu->in_reg = GIGATRON_BTN_A ^ 0xFF;
                } else if (loader->vsync_count == 12) {
                    /* Release A */
                    loader->cpu->in_reg = 0xFF;
                } else if (loader->vsync_count >= 72) {
                    /* Wait for loader to be ready, then start sending */
                    loader->state = LOADER_SENDING;
                    loader->vsync_count = 0;
                    loader->hsync_count = 0;
                    loader->frame_state = FRAME_IDLE;
                    loader->checksum = 0;
                    
                    /* Send initial frame with bad checksum to resync */
                    loader->current_segment = 0;
                    loader->segment_offset = 0;
                }
            }
            break;
            
        case LOADER_SENDING:
            /* Send GT1 data frame by frame */
            /* This is a simplified implementation - the real one is more complex */
            
            /* For simplicity, we directly write to RAM instead of going through
             * the full serial protocol. This is a common approach in emulators. */
            if (loader->gt1) {
                /* Direct load into RAM */
                for (uint32_t i = 0; i < loader->gt1->num_segments; i++) {
                    gt1_segment_t* seg = &loader->gt1->segments[i];
                    for (uint16_t j = 0; j < seg->size; j++) {
                        uint16_t addr = (seg->address + j) & loader->cpu->ram_mask;
                        loader->cpu->ram[addr] = seg->data[j];
                    }
                }
                
                /* Set start address if specified */
                if (loader->gt1->has_start_address) {
                    /* The vCPU start address is stored at 0x0016-0x0017 (vPC) */
                    uint16_t start = loader->gt1->start_address;
                    loader->cpu->ram[0x16] = start & 0xFF;
                    loader->cpu->ram[0x17] = (start >> 8) & 0xFF;
                }
                
                loader->state = LOADER_COMPLETE;
                loader->cpu->in_reg = 0xFF;  /* Release all buttons */
            } else {
                loader->state = LOADER_ERROR;
                loader->error_msg = "No GT1 data";
            }
            break;
    }
    
    loader->prev_out = out;
}
