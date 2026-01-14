/**
 * Gigatron TTL Microcomputer Emulator Core
 * 
 * Implementation of the Gigatron CPU emulation.
 */

#include "gigatron.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Instruction field extraction macros */
#define INST_OP(ir)     (((ir) >> 13) & 0x07)
#define INST_MODE(ir)   (((ir) >> 10) & 0x07)
#define INST_BUS(ir)    (((ir) >> 8) & 0x03)
#define INST_D(ir)      ((ir) & 0xFF)

/* Opcodes */
#define OP_LD   0
#define OP_AND  1
#define OP_OR   2
#define OP_XOR  3
#define OP_ADD  4
#define OP_SUB  5
#define OP_ST   6
#define OP_BR   7

/* Bus sources */
#define BUS_D   0
#define BUS_RAM 1
#define BUS_AC  2
#define BUS_IN  3

/* Address modes (for RAM access) */
#define MODE_D      0
#define MODE_X      1
#define MODE_YD     2
#define MODE_YX     3
#define MODE_D_X    4   /* Also writes to X */
#define MODE_D_Y    5   /* Also writes to Y */
#define MODE_D_OUT  6   /* Also writes to OUT */
#define MODE_YX_INC 7   /* Y,X with X++ */

/* Branch conditions */
#define BR_JMP  0       /* Jump (uses Y register for high byte) */
#define BR_GT   1       /* Branch if AC > 0 */
#define BR_LT   2       /* Branch if AC < 0 */
#define BR_NE   3       /* Branch if AC != 0 */
#define BR_EQ   4       /* Branch if AC == 0 */
#define BR_GE   5       /* Branch if AC >= 0 */
#define BR_LE   6       /* Branch if AC <= 0 */
#define BR_BRA  7       /* Branch always (within page) */

/**
 * Get default configuration
 * Note: Default RAM is 128KB (17-bit address) to support extended ROMs like dev128k7.rom
 */
gigatron_config_t gigatron_default_config(void) {
    gigatron_config_t config = {
        .hz = GIGATRON_HZ,
        .rom_address_width = 16,
        .ram_address_width = 17  /* 128KB for extended ROM support */
    };
    return config;
}

/**
 * Initialize the Gigatron
 */
bool gigatron_init(gigatron_t* cpu, const gigatron_config_t* config) {
    if (!cpu) return false;
    
    memset(cpu, 0, sizeof(gigatron_t));
    
    /* Apply configuration */
    if (config) {
        cpu->hz = config->hz ? config->hz : GIGATRON_HZ;
        cpu->rom_size = 1u << (config->rom_address_width ? config->rom_address_width : 16);
        cpu->ram_size = 1u << (config->ram_address_width ? config->ram_address_width : 15);
    } else {
        cpu->hz = GIGATRON_HZ;
        cpu->rom_size = GIGATRON_ROM_SIZE;
        cpu->ram_size = GIGATRON_RAM_SIZE;
    }
    
    cpu->rom_mask = cpu->rom_size - 1;
    cpu->ram_mask = cpu->ram_size - 1;
    
    /* Allocate ROM */
    cpu->rom = (uint16_t*)calloc(cpu->rom_size, sizeof(uint16_t));
    if (!cpu->rom) {
        return false;
    }
    
    /* Allocate RAM */
    cpu->ram = (uint8_t*)malloc(cpu->ram_size);
    if (!cpu->ram) {
        free(cpu->rom);
        cpu->rom = NULL;
        return false;
    }
    
    /* Randomize RAM (like real hardware) */
    srand((unsigned int)time(NULL));
    for (uint32_t i = 0; i < cpu->ram_size; i++) {
        cpu->ram[i] = (uint8_t)(rand() & 0xFF);
    }
    
    /* Reset CPU state */
    gigatron_reset(cpu);
    
    return true;
}

/**
 * Shutdown and free memory
 */
void gigatron_shutdown(gigatron_t* cpu) {
    if (!cpu) return;
    
    if (cpu->rom) {
        free(cpu->rom);
        cpu->rom = NULL;
    }
    
    if (cpu->ram) {
        free(cpu->ram);
        cpu->ram = NULL;
    }
}

/**
 * Reset CPU to power-on state
 */
void gigatron_reset(gigatron_t* cpu) {
    if (!cpu) return;
    
    cpu->pc = 0;
    cpu->next_pc = 1;
    cpu->ac = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->out = 0;
    cpu->outx = 0;
    cpu->in_reg = 0xFF;     /* Active low - all buttons released */
    
    /* 128K+ expansion registers */
    cpu->ctrl = 0x7C;       /* Default CTRL value */
    cpu->bank = 0;          /* Default bank (no offset) */
    cpu->prev_ctrl = -1;    /* No previous CTRL */
    cpu->miso = 0;          /* SPI MISO */
    
    cpu->cycles = 0;
}

/**
 * Calculate RAM address based on mode
 */
static inline uint16_t calc_addr(gigatron_t* cpu, uint8_t mode, uint8_t d) {
    switch (mode) {
        case MODE_D:
        case MODE_D_X:
        case MODE_D_Y:
        case MODE_D_OUT:
            return d;
        case MODE_X:
            return cpu->x;
        case MODE_YD:
            return ((uint16_t)cpu->y << 8) | d;
        case MODE_YX:
            return ((uint16_t)cpu->y << 8) | cpu->x;
        case MODE_YX_INC: {
            uint16_t addr = ((uint16_t)cpu->y << 8) | cpu->x;
            cpu->x = (cpu->x + 1) & 0xFF;
            return addr;
        }
        default:
            return d;
    }
}

/**
 * Calculate branch offset based on bus source
 */
static inline uint8_t calc_offset(gigatron_t* cpu, uint8_t bus, uint8_t d) {
    switch (bus) {
        case BUS_D:
            return d;
        case BUS_RAM:
            /* RAM always has at least 1 page, so no need to mask address for offset */
            return cpu->ram[d & cpu->ram_mask];
        case BUS_AC:
            return cpu->ac;
        case BUS_IN:
            return cpu->in_reg;
        default:
            return d;
    }
}

/**
 * Translate RAM address for 128K+ expansion (bank switching)
 */
static inline uint32_t translate_ram_addr(gigatron_t* cpu, uint16_t addr) {
    /* Convert to 32-bit to avoid truncation when XORing with bank */
    uint32_t phys_addr = addr;
    /* If address bit 15 is set, XOR with bank value */
    if (phys_addr & 0x8000) {
        phys_addr ^= cpu->bank;
    }
    return phys_addr & cpu->ram_mask;
}

/**
 * Execute ALU operation (OP 0-5)
 */
static void exec_alu_op(gigatron_t* cpu, uint8_t op, uint8_t mode, uint8_t bus, uint8_t d) {
    uint8_t b;
    
    /* Get bus value */
    switch (bus) {
        case BUS_D:
            b = d;
            break;
        case BUS_RAM: {
            uint16_t addr = calc_addr(cpu, mode, d);
            /* 128K+ expansion: SPI mode check */
            if (cpu->ctrl & 1) {
                b = cpu->miso;  /* SPI MISO data */
            } else {
                uint32_t phys_addr = translate_ram_addr(cpu, addr);
                b = cpu->ram[phys_addr];
            }
            break;
        }
        case BUS_AC:
            b = cpu->ac;
            break;
        case BUS_IN:
            b = cpu->in_reg;
            break;
        default:
            b = d;
            break;
    }
    
    /* Perform ALU operation */
    switch (op) {
        case OP_LD:
            /* b = b (no operation) */
            break;
        case OP_AND:
            b = cpu->ac & b;
            break;
        case OP_OR:
            b = cpu->ac | b;
            break;
        case OP_XOR:
            b = cpu->ac ^ b;
            break;
        case OP_ADD:
            b = (cpu->ac + b) & 0xFF;
            break;
        case OP_SUB:
            b = (cpu->ac - b) & 0xFF;
            break;
    }
    
    /* Write result to destination */
    switch (mode) {
        case MODE_D:
        case MODE_X:
        case MODE_YD:
        case MODE_YX:
            cpu->ac = b;
            break;
        case MODE_D_X:
            cpu->x = b;
            break;
        case MODE_D_Y:
            cpu->y = b;
            break;
        case MODE_D_OUT:
        case MODE_YX_INC: {
            uint8_t rising = ~cpu->out & b;
            cpu->out = b;
            
            /* Rising edge of out[6] latches AC into OUTX */
            if (rising & 0x40) {
                cpu->outx = cpu->ac;
            }
            break;
        }
    }
}

/**
 * Execute store operation (OP 6)
 */
static void exec_store_op(gigatron_t* cpu, uint8_t mode, uint8_t bus, uint8_t d) {
    uint8_t b = 0;
    bool do_write = true;
    uint16_t addr = calc_addr(cpu, mode, d);
    
    /* Get value to store */
    switch (bus) {
        case BUS_D:
            b = d;
            break;
        case BUS_RAM:
            /* 128K+ expansion: ST [Y,X++],$xx becomes CTRL register write */
            if (cpu->ram_size > 65536) {
                /* Write to CTRL register instead of RAM */
                cpu->prev_ctrl = cpu->ctrl;
                cpu->ctrl = addr & 0x80FD;
                /* Calculate bank: ((ctrl & 0xC0) << 9) ^ 0x8000 */
                cpu->bank = ((cpu->ctrl & 0xC0) << 9) ^ 0x8000;
                do_write = false;  /* Don't write to RAM */
            } else {
                /* Original undefined behavior - use 0 */
                b = 0;
            }
            break;
        case BUS_AC:
            b = cpu->ac;
            break;
        case BUS_IN:
            b = cpu->in_reg;
            break;
        default:
            b = d;
            break;
    }
    
    /* Write to RAM if not CTRL write */
    if (do_write) {
        uint32_t phys_addr = translate_ram_addr(cpu, addr);
        cpu->ram[phys_addr] = b;
    }
    
    /* Some modes also write to a register */
    switch (mode) {
        case MODE_D_X:
            cpu->x = cpu->ac;  /* Note: writes AC, not b */
            break;
        case MODE_D_Y:
            cpu->y = cpu->ac;  /* Note: writes AC, not b */
            break;
    }
}

/**
 * Execute branch operation (OP 7)
 */
static void exec_branch_op(gigatron_t* cpu, uint8_t mode, uint8_t bus, uint8_t d) {
    const uint8_t ZERO = 0x80;
    bool condition;
    uint8_t ac = cpu->ac ^ ZERO;    /* Convert to signed comparison */
    uint16_t base = cpu->pc & 0xFF00;
    
    /* Evaluate branch condition */
    switch (mode) {
        case BR_JMP:
            condition = true;
            base = (uint16_t)cpu->y << 8;
            break;
        case BR_GT:
            condition = (ac > ZERO);
            break;
        case BR_LT:
            condition = (ac < ZERO);
            break;
        case BR_NE:
            condition = (ac != ZERO);
            break;
        case BR_EQ:
            condition = (ac == ZERO);
            break;
        case BR_GE:
            condition = (ac >= ZERO);
            break;
        case BR_LE:
            condition = (ac <= ZERO);
            break;
        case BR_BRA:
            condition = true;
            break;
        default:
            condition = false;
            break;
    }
    
    /* Take branch if condition is true */
    if (condition) {
        uint8_t offset = calc_offset(cpu, bus, d);
        cpu->next_pc = base | offset;
    }
}

/**
 * Advance simulation by one clock cycle
 */
void gigatron_tick(gigatron_t* cpu) {
    if (!cpu || !cpu->rom) return;
    
    /* Reset prev_ctrl at start of each tick */
    cpu->prev_ctrl = -1;
    
    /* Fetch instruction */
    uint16_t pc = cpu->pc;
    cpu->pc = cpu->next_pc;
    cpu->next_pc = (cpu->pc + 1) & cpu->rom_mask;
    
    uint16_t ir = cpu->rom[pc];
    
    /* Decode instruction */
    uint8_t op = INST_OP(ir);
    uint8_t mode = INST_MODE(ir);
    uint8_t bus = INST_BUS(ir);
    uint8_t d = INST_D(ir);
    
    /* Execute instruction */
    switch (op) {
        case OP_LD:
        case OP_AND:
        case OP_OR:
        case OP_XOR:
        case OP_ADD:
        case OP_SUB:
            exec_alu_op(cpu, op, mode, bus, d);
            break;
        case OP_ST:
            exec_store_op(cpu, mode, bus, d);
            break;
        case OP_BR:
            exec_branch_op(cpu, mode, bus, d);
            break;
    }
    
    cpu->cycles++;
}

/**
 * Run multiple cycles
 */
void gigatron_run(gigatron_t* cpu, uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; i++) {
        gigatron_tick(cpu);
    }
}

/**
 * Load ROM from memory buffer (big-endian 16-bit words)
 */
size_t gigatron_load_rom(gigatron_t* cpu, const uint8_t* data, size_t size) {
    if (!cpu || !cpu->rom || !data) return 0;
    
    size_t word_count = size / 2;
    if (word_count > cpu->rom_size) {
        word_count = cpu->rom_size;
    }
    
    /* Convert from big-endian to host endian */
    for (size_t i = 0; i < word_count; i++) {
        cpu->rom[i] = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
    }
    
    return word_count;
}

/**
 * Load ROM from file
 */
bool gigatron_load_rom_file(gigatron_t* cpu, const char* filename) {
    if (!cpu || !filename) return false;
    
    FILE* f = fopen(filename, "rb");
    if (!f) {
        return false;
    }
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(f);
        return false;
    }
    
    /* Allocate temporary buffer */
    uint8_t* buffer = (uint8_t*)malloc((size_t)file_size);
    if (!buffer) {
        fclose(f);
        return false;
    }
    
    /* Read file */
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, f);
    fclose(f);
    
    if (bytes_read != (size_t)file_size) {
        free(buffer);
        return false;
    }
    
    /* Load into ROM */
    size_t words = gigatron_load_rom(cpu, buffer, bytes_read);
    free(buffer);
    
    return words > 0;
}
