/**
 * Gigatron GT1 File Loader
 * 
 * Implements the GT1 serial loading protocol exactly as jsemu does.
 * Data is sent bit by bit through the input register, synchronized
 * with HSYNC pulses.
 * 
 * Key details from jsemu:
 * 1. Shift bit FIRST, then wait for HSYNC posedge
 * 2. Checksum accumulates across frames (not reset per frame)
 * 3. After sending checksum, update checksum = (-checksum) & 0xFF
 */

#include "loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Button press timing (in VSYNC frames) */
#define BUTTON_DOWN_TIME    1
#define BUTTON_UP_TIME      1
#define BUTTON_A_UP_TIME    60
#define RESET_WAIT_FRAMES   100
#define MENU_DOWN_PRESSES   5

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
    loader->frame_state = FRAME_WAIT_VSYNC_NEG;
    loader->bits_remaining = 0;
    loader->checksum = 0;
    loader->vsync_count = 0;
    loader->button_timer = 0;
    loader->prev_out = 0;
    loader->error_msg = NULL;
    
    if (loader->cpu) {
        loader->cpu->in_reg = 0xFF;
    }
}

/**
 * Parse GT1 file from memory
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
            break;
        }
        
        if (offset + 3 > size) {
            free(gt1);
            return NULL;
        }
        
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
    loader->frame_state = FRAME_WAIT_VSYNC_NEG;
    loader->bits_remaining = 0;
    loader->vsync_count = 0;
    loader->button_timer = 0;
    loader->checksum = 0;
    loader->prev_out = loader->cpu->out;
    loader->error_msg = NULL;
    
    /* Start by resetting the CPU */
    gigatron_reset(loader->cpu);
    loader->state = LOADER_RESET_WAIT;
    
    return true;
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
 * Check for VSYNC rising edge (signal goes high)
 */
static bool vsync_posedge(loader_t* loader) {
    return (~loader->prev_out & loader->cpu->out & GIGATRON_OUT_VSYNC) != 0;
}

/**
 * Check for VSYNC falling edge (signal goes low)
 */
static bool vsync_negedge(loader_t* loader) {
    return (loader->prev_out & ~loader->cpu->out & GIGATRON_OUT_VSYNC) != 0;
}

/**
 * Check for HSYNC rising edge (signal goes high)
 */
static bool hsync_posedge(loader_t* loader) {
    return (~loader->prev_out & loader->cpu->out & GIGATRON_OUT_HSYNC) != 0;
}

/**
 * Shift one bit into input register (MSB first)
 * This is called BEFORE waiting for HSYNC, matching JS behavior
 */
static void shift_bit(loader_t* loader, bool bit) {
    loader->cpu->in_reg = ((loader->cpu->in_reg << 1) & 0xFF) | (bit ? 1 : 0);
}

/**
 * Prepare a frame for sending
 * Note: Does NOT reset checksum - caller is responsible for that
 */
static void prepare_frame(loader_t* loader, uint8_t first_byte, uint16_t addr, 
                          const uint8_t* payload, uint8_t payload_len) {
    loader->frame_first_byte = first_byte;
    loader->frame_length = payload_len;
    loader->frame_addr = addr;
    
    /* Clear and copy payload */
    memset(loader->frame_payload, 0, LOADER_MAX_PAYLOAD_SIZE);
    if (payload && payload_len > 0) {
        memcpy(loader->frame_payload, payload, payload_len);
    }
    
    loader->frame_state = FRAME_WAIT_VSYNC_NEG;
    loader->bits_remaining = 0;
    loader->payload_index = 0;
}

/**
 * Process frame sending state machine
 * 
 * Critical timing from JS:
 *   shiftBit(bit);           // shift FIRST
 *   await atPosedge(HSYNC);  // then wait
 * 
 * Checksum flow from JS:
 *   - sendDataBits adds value to checksum, then sends bits
 *   - After firstByte, add (firstByte << 6) to checksum
 *   - At end, checksum = (-checksum) & 0xFF, send that, and KEEP that value
 * 
 * Returns true when frame is complete
 */
static bool process_frame(loader_t* loader) {
    switch (loader->frame_state) {
        case FRAME_WAIT_VSYNC_NEG:
            /* Wait for VSYNC falling edge to start frame */
            if (vsync_negedge(loader)) {
                loader->frame_state = FRAME_WAIT_HSYNC_1;
            }
            break;
            
        case FRAME_WAIT_HSYNC_1:
            /* Wait for first HSYNC posedge */
            if (hsync_posedge(loader)) {
                loader->frame_state = FRAME_WAIT_HSYNC_2;
            }
            break;
            
        case FRAME_WAIT_HSYNC_2:
            /* Wait for second HSYNC posedge, then start sending */
            if (hsync_posedge(loader)) {
                /* sendDataBits(firstByte, 8): add to checksum, then send */
                loader->checksum = (loader->checksum + loader->frame_first_byte) & 0xFF;
                loader->current_byte = loader->frame_first_byte;
                loader->bits_remaining = 8;
                
                /* Shift first bit (MSB) - shift BEFORE waiting for next HSYNC */
                shift_bit(loader, (loader->current_byte & 0x80) != 0);
                loader->current_byte <<= 1;
                loader->bits_remaining--;
                
                loader->frame_state = FRAME_SEND_FIRST_BYTE;
            }
            break;
            
        case FRAME_SEND_FIRST_BYTE:
            /* Sending first byte bits */
            if (hsync_posedge(loader)) {
                if (loader->bits_remaining > 0) {
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                } else {
                    /* First byte done */
                    /* Add (firstByte << 6) to checksum - this is per JS protocol */
                    loader->checksum = (loader->checksum + (loader->frame_first_byte << 6)) & 0xFF;
                    
                    /* sendDataBits(length, 6): add to checksum, then send */
                    loader->checksum = (loader->checksum + loader->frame_length) & 0xFF;
                    loader->current_byte = loader->frame_length << 2; /* Align to MSB for 6-bit */
                    loader->bits_remaining = 6;
                    
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                    
                    loader->frame_state = FRAME_SEND_LENGTH;
                }
            }
            break;
            
        case FRAME_SEND_LENGTH:
            /* Sending length bits */
            if (hsync_posedge(loader)) {
                if (loader->bits_remaining > 0) {
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                } else {
                    /* sendDataBits(addr & 0xff, 8) */
                    uint8_t addr_low = loader->frame_addr & 0xFF;
                    loader->checksum = (loader->checksum + addr_low) & 0xFF;
                    loader->current_byte = addr_low;
                    loader->bits_remaining = 8;
                    
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                    
                    loader->frame_state = FRAME_SEND_ADDR_LOW;
                }
            }
            break;
            
        case FRAME_SEND_ADDR_LOW:
            /* Sending addr low bits */
            if (hsync_posedge(loader)) {
                if (loader->bits_remaining > 0) {
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                } else {
                    /* sendDataBits(addr >> 8, 8) */
                    uint8_t addr_high = (loader->frame_addr >> 8) & 0xFF;
                    loader->checksum = (loader->checksum + addr_high) & 0xFF;
                    loader->current_byte = addr_high;
                    loader->bits_remaining = 8;
                    
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                    
                    loader->frame_state = FRAME_SEND_ADDR_HIGH;
                }
            }
            break;
            
        case FRAME_SEND_ADDR_HIGH:
            /* Sending addr high bits */
            if (hsync_posedge(loader)) {
                if (loader->bits_remaining > 0) {
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                } else {
                    /* sendDataBytes: send 60 bytes (payload padded with zeros) */
                    loader->payload_index = 0;
                    uint8_t byte = loader->frame_payload[0];
                    loader->checksum = (loader->checksum + byte) & 0xFF;
                    loader->current_byte = byte;
                    loader->bits_remaining = 8;
                    
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                    
                    loader->frame_state = FRAME_SEND_PAYLOAD;
                }
            }
            break;
            
        case FRAME_SEND_PAYLOAD:
            /* Sending payload bytes */
            if (hsync_posedge(loader)) {
                if (loader->bits_remaining > 0) {
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                } else {
                    /* Current byte done */
                    loader->payload_index++;
                    
                    if (loader->payload_index >= LOADER_MAX_PAYLOAD_SIZE) {
                        /* All 60 payload bytes sent */
                        /* Calculate final checksum and UPDATE loader->checksum */
                        /* JS: this.checksum = (-this.checksum) & 0xff; */
                        loader->checksum = (-loader->checksum) & 0xFF;
                        
                        /* sendBits(checksum, 8) - does NOT add to checksum */
                        loader->current_byte = loader->checksum;
                        loader->bits_remaining = 8;
                        
                        shift_bit(loader, (loader->current_byte & 0x80) != 0);
                        loader->current_byte <<= 1;
                        loader->bits_remaining--;
                        
                        loader->frame_state = FRAME_SEND_CHECKSUM;
                    } else {
                        /* Send next payload byte (sendDataBits adds to checksum) */
                        uint8_t byte = loader->frame_payload[loader->payload_index];
                        loader->checksum = (loader->checksum + byte) & 0xFF;
                        loader->current_byte = byte;
                        loader->bits_remaining = 8;
                        
                        shift_bit(loader, (loader->current_byte & 0x80) != 0);
                        loader->current_byte <<= 1;
                        loader->bits_remaining--;
                    }
                }
            }
            break;
            
        case FRAME_SEND_CHECKSUM:
            /* Sending checksum bits */
            if (hsync_posedge(loader)) {
                if (loader->bits_remaining > 0) {
                    shift_bit(loader, (loader->current_byte & 0x80) != 0);
                    loader->current_byte <<= 1;
                    loader->bits_remaining--;
                } else {
                    /* Frame complete - checksum already updated to final value */
                    loader->frame_state = FRAME_DONE;
                    return true;
                }
            }
            break;
            
        case FRAME_DONE:
            return true;
    }
    
    return false;
}

/**
 * Setup next data frame from GT1 segments
 * Note: Does NOT reset checksum - checksum accumulates across frames per JS behavior
 * Returns false if no more data to send
 */
static bool setup_next_data_frame(loader_t* loader) {
    if (!loader->gt1) return false;
    
    while (loader->current_segment < loader->gt1->num_segments) {
        gt1_segment_t* seg = &loader->gt1->segments[loader->current_segment];
        
        if (loader->segment_offset < seg->size) {
            /* Calculate how many bytes to send in this frame */
            uint16_t remaining = seg->size - loader->segment_offset;
            uint8_t frame_len = (remaining > LOADER_MAX_PAYLOAD_SIZE) ? 
                                LOADER_MAX_PAYLOAD_SIZE : (uint8_t)remaining;
            
            /* Get address for this chunk */
            uint16_t addr = seg->address + loader->segment_offset;
            
            /* Prepare the frame - checksum continues from previous frame */
            prepare_frame(loader, LOADER_START_OF_FRAME, addr,
                         seg->data + loader->segment_offset, frame_len);
            
            loader->segment_offset += frame_len;
            return true;
        }
        
        /* Move to next segment */
        loader->current_segment++;
        loader->segment_offset = 0;
    }
    
    return false;
}

/**
 * Advance loader by one tick
 */
void loader_tick(loader_t* loader) {
    if (!loader || !loader->cpu) return;
    
    switch (loader->state) {
        case LOADER_IDLE:
        case LOADER_COMPLETE:
        case LOADER_ERROR:
            break;
            
        case LOADER_RESET_WAIT:
            /* Wait for RESET_WAIT_FRAMES VSYNCs after reset */
            if (vsync_posedge(loader)) {
                loader->vsync_count++;
                if (loader->vsync_count >= RESET_WAIT_FRAMES) {
                    loader->state = LOADER_MENU_NAV;
                    loader->vsync_count = 0;
                    loader->button_timer = 0;
                }
            }
            break;
            
        case LOADER_MENU_NAV:
            /* Navigate menu: press DOWN 5 times, then A once */
            if (vsync_posedge(loader)) {
                loader->vsync_count++;
                
                /* 
                 * JS: replicate(5, this.pressButton(BUTTON_DOWN, 1, 1))
                 * Each button press: 1 frame down, 1 frame up = 2 frames per press
                 * 5 presses = 10 frames
                 * 
                 * JS: this.pressButton(BUTTON_A, 1, 60)
                 * 1 frame down, 60 frames up = 61 frames
                 * 
                 * Total: 10 + 61 = 71 frames before starting to send
                 */
                
                if (loader->vsync_count <= MENU_DOWN_PRESSES * 2) {
                    /* DOWN button presses (frames 1-10) */
                    if (loader->vsync_count % 2 == 1) {
                        /* Odd frames: press DOWN */
                        loader->cpu->in_reg = GIGATRON_BTN_DOWN ^ 0xFF;
                    } else {
                        /* Even frames: release */
                        loader->cpu->in_reg = 0xFF;
                    }
                } else if (loader->vsync_count == MENU_DOWN_PRESSES * 2 + 1) {
                    /* Frame 11: Press A */
                    loader->cpu->in_reg = GIGATRON_BTN_A ^ 0xFF;
                } else if (loader->vsync_count == MENU_DOWN_PRESSES * 2 + 2) {
                    /* Frame 12: Release A */
                    loader->cpu->in_reg = 0xFF;
                } else if (loader->vsync_count >= MENU_DOWN_PRESSES * 2 + 2 + BUTTON_A_UP_TIME) {
                    /* Frame 72: Done with menu navigation, start sync frame */
                    /* JS: this.checksum = 0; return this.sendFrame(0xff, 0); */
                    loader->state = LOADER_SYNC_FRAME;
                    loader->checksum = 0;
                    prepare_frame(loader, 0xFF, 0, NULL, 0);
                }
            }
            break;
            
        case LOADER_SYNC_FRAME:
            /* Send sync frame with checksum starting from 0 (intentionally bad) */
            if (process_frame(loader)) {
                /* Sync frame done */
                /* JS: this.checksum = INIT_CHECKSUM; return this.sendSegments(data); */
                loader->checksum = LOADER_INIT_CHECKSUM;
                loader->state = LOADER_SENDING;
                loader->current_segment = 0;
                loader->segment_offset = 0;
                
                if (!setup_next_data_frame(loader)) {
                    /* No data to send, go to start command */
                    if (loader->gt1->has_start_address) {
                        loader->state = LOADER_START_CMD;
                        /* Checksum continues from INIT_CHECKSUM */
                        prepare_frame(loader, LOADER_START_OF_FRAME, 
                                     loader->gt1->start_address, NULL, 0);
                    } else {
                        loader->state = LOADER_COMPLETE;
                        loader->cpu->in_reg = 0xFF;
                    }
                }
            }
            break;
            
        case LOADER_SENDING:
            /* Send GT1 data frames - checksum accumulates across frames */
            if (process_frame(loader)) {
                /* Frame done, prepare next (checksum continues) */
                if (!setup_next_data_frame(loader)) {
                    /* All data sent, send start command if needed */
                    if (loader->gt1->has_start_address) {
                        loader->state = LOADER_START_CMD;
                        /* Checksum continues from current value */
                        prepare_frame(loader, LOADER_START_OF_FRAME,
                                     loader->gt1->start_address, NULL, 0);
                    } else {
                        loader->state = LOADER_COMPLETE;
                        loader->cpu->in_reg = 0xFF;
                    }
                }
            }
            break;
            
        case LOADER_START_CMD:
            /* Send start command frame - checksum continues from data frames */
            if (process_frame(loader)) {
                loader->state = LOADER_COMPLETE;
                loader->cpu->in_reg = 0xFF;
            }
            break;
    }
    
    loader->prev_out = loader->cpu->out;
}
