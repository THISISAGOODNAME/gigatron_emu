/**
 * Gigatron TTL Microcomputer Emulator
 * 
 * A raylib based frontend for the Gigatron emulator.
 */

#include "raylib.h"
#include "gigatron.h"
#include "vga.h"
#include "audio.h"
#include "loader.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define WINDOW_WIDTH    1024
#define WINDOW_HEIGHT   720
#define SCREEN_SCALE    1.5f

/* Colors */
#define COLOR_BG        (Color){ 25, 25, 38, 255 }
#define COLOR_PANEL     (Color){ 35, 35, 50, 255 }
#define COLOR_TEXT      (Color){ 200, 200, 210, 255 }
#define COLOR_ACCENT    (Color){ 100, 180, 255, 255 }
#define COLOR_SUCCESS   (Color){ 100, 200, 120, 255 }
#define COLOR_WARNING   (Color){ 255, 200, 100, 255 }

/* ============================================================================
 * Application State
 * ============================================================================ */

typedef struct {
    /* Emulator core */
    gigatron_t cpu;
    vga_t vga;
    audio_t audio;
    loader_t loader;
    
    /* Graphics */
    Texture2D screen_texture;
    Image screen_image;
    
    /* State flags */
    bool rom_loaded;
    bool emulator_running;
    bool show_debug;
    
    /* Input state */
    uint8_t button_state;
    
    /* Audio */
    AudioStream audio_stream;
    float audio_buffer[4096];
    
    /* Status message */
    char status_message[256];
    float status_timeout;
    
    /* Paths */
    char rom_path[512];
    char gt1_path[512];
    
    /* Performance */
    double frame_time;
    int fps;
} AppState;

static AppState state = {0};

/* ============================================================================
 * Audio Callback
 * ============================================================================ */

static void audio_input_callback(void* buffer, unsigned int frames) {
    float* out = (float*)buffer;
    
    /* Read samples from emulator audio buffer */
    uint32_t samples_read = audio_read_samples(&state.audio, state.audio_buffer, frames);
    
    /* Fill output buffer (mono to stereo) */
    for (unsigned int i = 0; i < frames; i++) {
        float sample = (i < samples_read) ? state.audio_buffer[i] : 0.0f;
        out[i * 2] = sample;        /* Left */
        out[i * 2 + 1] = sample;    /* Right */
    }
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static void set_status(const char* msg) {
    strncpy(state.status_message, msg, sizeof(state.status_message) - 1);
    state.status_timeout = 3.0f;
}

static const char* get_filename(const char* path) {
    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    const char* name = path;
    
    if (last_slash && last_slash > name) name = last_slash + 1;
    if (last_backslash && last_backslash > name) name = last_backslash + 1;
    
    return name;
}

/* ============================================================================
 * File Operations
 * ============================================================================ */

static bool load_rom(const char* path) {
    if (gigatron_load_rom_file(&state.cpu, path)) {
        gigatron_reset(&state.cpu);
        vga_reset(&state.vga);
        audio_reset(&state.audio);
        loader_reset(&state.loader);
        state.rom_loaded = true;
        state.emulator_running = true;
        strncpy(state.rom_path, path, sizeof(state.rom_path) - 1);
        set_status("ROM loaded successfully");
        return true;
    }
    set_status("Failed to load ROM");
    return false;
}

static bool load_gt1(const char* path) {
    if (!state.rom_loaded) {
        set_status("Please load a ROM first");
        return false;
    }
    
    gt1_file_t* gt1 = loader_load_gt1_file(path);
    if (gt1) {
        if (loader_start(&state.loader, gt1)) {
            strncpy(state.gt1_path, path, sizeof(state.gt1_path) - 1);
            set_status("Loading GT1 file...");
            return true;
        }
        loader_free_gt1(gt1);
    }
    set_status("Failed to load GT1 file");
    return false;
}

static void handle_dropped_files(void) {
    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        
        if (files.count > 0) {
            const char* path = files.paths[0];
            const char* ext = GetFileExtension(path);
            
            if (TextIsEqual(ext, ".rom") || TextIsEqual(ext, ".ROM")) {
                load_rom(path);
            } else if (TextIsEqual(ext, ".gt1") || TextIsEqual(ext, ".GT1")) {
                load_gt1(path);
            }
        }
        
        UnloadDroppedFiles(files);
    }
}

/* ============================================================================
 * Input Handling
 * ============================================================================ */

static void update_input(void) {
    state.button_state = 0;
    
    /* D-Pad */
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
        state.button_state |= GIGATRON_BTN_UP;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
        state.button_state |= GIGATRON_BTN_DOWN;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
        state.button_state |= GIGATRON_BTN_LEFT;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
        state.button_state |= GIGATRON_BTN_RIGHT;
    
    /* Buttons */
    if (IsKeyDown(KEY_Z) || IsKeyDown(KEY_J))
        state.button_state |= GIGATRON_BTN_A;
    if (IsKeyDown(KEY_X) || IsKeyDown(KEY_K))
        state.button_state |= GIGATRON_BTN_B;
    if (IsKeyDown(KEY_ENTER))
        state.button_state |= GIGATRON_BTN_START;
    if (IsKeyDown(KEY_BACKSPACE) || IsKeyDown(KEY_ESCAPE))
        state.button_state |= GIGATRON_BTN_SELECT;
    
    /* Gamepad support */
    if (IsGamepadAvailable(0)) {
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP))
            state.button_state |= GIGATRON_BTN_UP;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
            state.button_state |= GIGATRON_BTN_DOWN;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))
            state.button_state |= GIGATRON_BTN_LEFT;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
            state.button_state |= GIGATRON_BTN_RIGHT;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
            state.button_state |= GIGATRON_BTN_A;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
            state.button_state |= GIGATRON_BTN_B;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))
            state.button_state |= GIGATRON_BTN_START;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT))
            state.button_state |= GIGATRON_BTN_SELECT;
        
        /* Analog stick */
        float axis_x = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        float axis_y = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        if (axis_x < -0.5f) state.button_state |= GIGATRON_BTN_LEFT;
        if (axis_x > 0.5f) state.button_state |= GIGATRON_BTN_RIGHT;
        if (axis_y < -0.5f) state.button_state |= GIGATRON_BTN_UP;
        if (axis_y > 0.5f) state.button_state |= GIGATRON_BTN_DOWN;
    }
    
    /* Hotkeys */
    if (IsKeyPressed(KEY_F1)) {
        state.show_debug = !state.show_debug;
    }
    if (IsKeyPressed(KEY_F5)) {
        if (state.rom_loaded) {
            gigatron_reset(&state.cpu);
            vga_reset(&state.vga);
            audio_reset(&state.audio);
            loader_reset(&state.loader);
            set_status("Emulator reset");
        }
    }
    if (IsKeyPressed(KEY_SPACE)) {
        if (state.rom_loaded) {
            state.emulator_running = !state.emulator_running;
            set_status(state.emulator_running ? "Resumed" : "Paused");
        }
    }
    if (IsKeyPressed(KEY_F6)) {
        /* Step one frame */
        if (state.rom_loaded && !state.emulator_running) {
            const uint32_t cycles_per_frame = state.cpu.hz / 60;
            for (uint32_t i = 0; i < cycles_per_frame; i++) {
                if (!loader_is_active(&state.loader)) {
                    state.cpu.in_reg = state.button_state ^ 0xFF;
                }
                gigatron_tick(&state.cpu);
                vga_tick(&state.vga);
                audio_tick(&state.audio);
                if (loader_is_active(&state.loader)) {
                    loader_tick(&state.loader);
                }
            }
            set_status("Stepped 1 frame");
        }
    }
}

/* ============================================================================
 * Emulator Core
 * ============================================================================ */

static void run_emulator_frame(void) {
    if (!state.rom_loaded || !state.emulator_running) return;
    
    /* Run enough cycles for ~60fps (6.25MHz / 60 = ~104166 cycles per frame) */
    const uint32_t cycles_per_frame = state.cpu.hz / 60;
    
    for (uint32_t i = 0; i < cycles_per_frame; i++) {
        /* Only update input from user when loader is not active */
        if (!loader_is_active(&state.loader)) {
            state.cpu.in_reg = state.button_state ^ 0xFF;  /* Active low */
        }
        
        gigatron_tick(&state.cpu);
        vga_tick(&state.vga);
        audio_tick(&state.audio);
        
        if (loader_is_active(&state.loader)) {
            loader_tick(&state.loader);
        }
    }
    
    /* Check loader status */
    if (loader_is_complete(&state.loader)) {
        set_status("GT1 loaded successfully");
        loader_reset(&state.loader);
    } else if (loader_has_error(&state.loader)) {
        set_status(loader_get_error(&state.loader) ? loader_get_error(&state.loader) : "Loader error");
        loader_reset(&state.loader);
    }
}

static void update_screen_texture(void) {
    if (!state.vga.pixels) return;
    
    /* Update image data from VGA framebuffer */
    UpdateTexture(state.screen_texture, state.vga.pixels);
}

/* ============================================================================
 * UI Drawing
 * ============================================================================ */

static void draw_status_bar(void) {
    int bar_height = 30;
    int y = GetScreenHeight() - bar_height;
    
    DrawRectangle(0, y, GetScreenWidth(), bar_height, COLOR_PANEL);
    DrawLine(0, y, GetScreenWidth(), y, COLOR_ACCENT);
    
    /* Status message */
    if (state.status_timeout > 0) {
        DrawText(state.status_message, 10, y + 7, 16, COLOR_TEXT);
    } else if (state.rom_loaded) {
        const char* rom_name = get_filename(state.rom_path);
        DrawText(TextFormat("ROM: %s | %s | FPS: %d", 
                           rom_name,
                           state.emulator_running ? "Running" : "Paused",
                           state.fps), 
                10, y + 7, 16, COLOR_TEXT);
    } else {
        DrawText("No ROM loaded - Drag & drop a .rom file or press O to open", 10, y + 7, 16, COLOR_TEXT);
    }
    
    /* Controls hint */
    const char* hint = "F1:Debug | F5:Reset | Space:Pause | F6:Step";
    int hint_width = MeasureText(hint, 14);
    DrawText(hint, GetScreenWidth() - hint_width - 10, y + 8, 14, (Color){150, 150, 160, 255});
}

static void draw_debug_panel(void) {
    if (!state.show_debug) return;
    
    int panel_width = 280;
    int panel_x = GetScreenWidth() - panel_width - 10;
    int panel_y = 50;
    int line_height = 18;
    int y = panel_y + 10;
    
    /* Panel background */
    DrawRectangle(panel_x, panel_y, panel_width, 400, (Color){30, 30, 45, 230});
    DrawRectangleLines(panel_x, panel_y, panel_width, 400, COLOR_ACCENT);
    
    /* Title */
    DrawText("Debug Info", panel_x + 10, y, 18, COLOR_ACCENT);
    y += line_height + 10;
    
    /* Performance */
    DrawText(TextFormat("Frame Time: %.2f ms", state.frame_time * 1000.0), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    DrawText(TextFormat("FPS: %d", state.fps), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    DrawText(TextFormat("VGA Frames: %u", state.vga.frame_count), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    DrawText(TextFormat("CPU Cycles: %llu", (unsigned long long)state.cpu.cycles), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height + 10;
    
    /* CPU Registers */
    DrawText("CPU Registers", panel_x + 10, y, 16, COLOR_ACCENT);
    y += line_height + 5;
    
    DrawText(TextFormat("PC:   0x%04X", state.cpu.pc), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    DrawText(TextFormat("AC:   0x%02X (%3d)", state.cpu.ac, state.cpu.ac), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    DrawText(TextFormat("X:    0x%02X (%3d)", state.cpu.x, state.cpu.x), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    DrawText(TextFormat("Y:    0x%02X (%3d)", state.cpu.y, state.cpu.y), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height + 10;
    
    /* I/O */
    DrawText("I/O", panel_x + 10, y, 16, COLOR_ACCENT);
    y += line_height + 5;
    
    DrawText(TextFormat("OUT:  0x%02X", state.cpu.out), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    DrawText(TextFormat("OUTX: 0x%02X", state.cpu.outx), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    DrawText(TextFormat("IN:   0x%02X", state.cpu.in_reg), panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    
    /* HSYNC/VSYNC */
    DrawText(TextFormat("HSYNC: %d  VSYNC: %d", 
                       (state.cpu.out & GIGATRON_OUT_HSYNC) ? 1 : 0,
                       (state.cpu.out & GIGATRON_OUT_VSYNC) ? 1 : 0), 
            panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height + 10;
    
    /* Input state */
    DrawText("Input", panel_x + 10, y, 16, COLOR_ACCENT);
    y += line_height + 5;
    
    char input_str[32];
    snprintf(input_str, sizeof(input_str), "%s%s%s%s %s%s%s%s",
            (state.button_state & GIGATRON_BTN_UP) ? "U" : "-",
            (state.button_state & GIGATRON_BTN_DOWN) ? "D" : "-",
            (state.button_state & GIGATRON_BTN_LEFT) ? "L" : "-",
            (state.button_state & GIGATRON_BTN_RIGHT) ? "R" : "-",
            (state.button_state & GIGATRON_BTN_A) ? "A" : "-",
            (state.button_state & GIGATRON_BTN_B) ? "B" : "-",
            (state.button_state & GIGATRON_BTN_START) ? "S" : "-",
            (state.button_state & GIGATRON_BTN_SELECT) ? "s" : "-");
    DrawText(input_str, panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height + 10;
    
    /* Loader state */
    DrawText("Loader", panel_x + 10, y, 16, COLOR_ACCENT);
    y += line_height + 5;
    
    const char* loader_state_names[] = {
        "IDLE", "RESET_WAIT", "MENU_NAV", "SYNC_FRAME",
        "SENDING", "START_CMD", "COMPLETE", "ERROR"
    };
    DrawText(TextFormat("State: %s", loader_state_names[state.loader.state]), 
            panel_x + 10, y, 14, COLOR_TEXT);
    y += line_height;
    
    if (loader_is_active(&state.loader)) {
        float progress = loader_get_progress(&state.loader);
        DrawText(TextFormat("Progress: %.1f%%", progress * 100.0f), panel_x + 10, y, 14, COLOR_SUCCESS);
        
        /* Progress bar */
        y += line_height + 5;
        int bar_width = panel_width - 20;
        DrawRectangle(panel_x + 10, y, bar_width, 10, (Color){50, 50, 60, 255});
        DrawRectangle(panel_x + 10, y, (int)(bar_width * progress), 10, COLOR_ACCENT);
        DrawRectangleLines(panel_x + 10, y, bar_width, 10, COLOR_TEXT);
    }
}

static void draw_controls_help(void) {
    if (!state.rom_loaded) {
        int center_x = GetScreenWidth() / 2;
        int center_y = GetScreenHeight() / 2;
        
        DrawText("Gigatron TTL Emulator", center_x - MeasureText("Gigatron TTL Emulator", 30) / 2, 
                center_y - 100, 30, COLOR_ACCENT);
        
        const char* instructions[] = {
            "Drag & drop a ROM file to load",
            "or press 'O' to open file dialog",
            "",
            "Controls:",
            "Arrow Keys / WASD - D-Pad",
            "Z / J - A Button",
            "X / K - B Button", 
            "Enter - Start",
            "Backspace / Esc - Select"
        };
        
        int y = center_y - 40;
        for (int i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
            int text_width = MeasureText(instructions[i], 16);
            DrawText(instructions[i], center_x - text_width / 2, y, 16, COLOR_TEXT);
            y += 22;
        }
    }
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char* argv[]) {
    /* Initialize window */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Gigatron TTL Emulator");
    SetTargetFPS(60);
    
    /* Initialize audio */
    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(2048);
    state.audio_stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 32, 2);
    SetAudioStreamCallback(state.audio_stream, audio_input_callback);
    PlayAudioStream(state.audio_stream);
    
    /* Initialize emulator */
    gigatron_config_t cpu_config = gigatron_default_config();
    gigatron_init(&state.cpu, &cpu_config);
    vga_init(&state.vga, &state.cpu);
    audio_init(&state.audio, &state.cpu);
    loader_init(&state.loader, &state.cpu);
    
    /* Create screen texture */
    state.screen_image = GenImageColor(VGA_WIDTH, VGA_HEIGHT, BLACK);
    state.screen_texture = LoadTextureFromImage(state.screen_image);
    SetTextureFilter(state.screen_texture, TEXTURE_FILTER_POINT);  /* Nearest neighbor for crisp pixels */
    
    /* Initial state */
    state.rom_loaded = false;
    state.emulator_running = false;
    state.show_debug = false;
    state.button_state = 0;
    
    /* Try to load default ROM */
    if (load_rom("roms/gigatron.rom")) {
        set_status("Default ROM loaded");
    }
    
    /* Handle command line arguments */
    if (argc > 1) {
        const char* ext = GetFileExtension(argv[1]);
        if (TextIsEqual(ext, ".rom") || TextIsEqual(ext, ".ROM")) {
            load_rom(argv[1]);
        } else if (TextIsEqual(ext, ".gt1") || TextIsEqual(ext, ".GT1")) {
            load_gt1(argv[1]);
        }
    }
    
    /* Main loop */
    while (!WindowShouldClose()) {
        /* Timing */
        state.frame_time = GetFrameTime();
        state.fps = GetFPS();
        
        /* Update status timeout */
        if (state.status_timeout > 0) {
            state.status_timeout -= state.frame_time;
        }
        
        /* Handle dropped files */
        handle_dropped_files();
        
        /* Update input */
        update_input();
        
        /* Open file dialog with O key */
        if (IsKeyPressed(KEY_O)) {
            /* Note: raylib doesn't have built-in file dialog, 
               but we support drag & drop which is cross-platform */
            set_status("Use drag & drop to load files");
        }
        
        /* Run emulator */
        run_emulator_frame();
        
        /* Update screen texture */
        if (vga_frame_ready(&state.vga) || !state.emulator_running) {
            update_screen_texture();
        }
        
        /* Drawing */
        BeginDrawing();
        ClearBackground(COLOR_BG);
        
        /* Calculate screen position and size */
        int available_height = GetScreenHeight() - 30;  /* Subtract status bar */
        float scale = (float)available_height / VGA_HEIGHT;
        float scaled_width = VGA_WIDTH * scale;
        float scaled_height = VGA_HEIGHT * scale;
        
        /* Limit width if needed */
        if (scaled_width > GetScreenWidth()) {
            scale = (float)GetScreenWidth() / VGA_WIDTH;
            scaled_width = VGA_WIDTH * scale;
            scaled_height = VGA_HEIGHT * scale;
        }
        
        /* Center the display */
        float screen_x = (GetScreenWidth() - scaled_width) / 2.0f;
        float screen_y = (available_height - scaled_height) / 2.0f;
        
        /* Draw screen with border */
        Rectangle dest = { screen_x, screen_y, scaled_width, scaled_height };
        Rectangle src = { 0, 0, VGA_WIDTH, VGA_HEIGHT };
        DrawTexturePro(state.screen_texture, src, dest, (Vector2){0, 0}, 0, WHITE);
        
        /* Draw border around screen */
        DrawRectangleLinesEx((Rectangle){ screen_x - 2, screen_y - 2, scaled_width + 4, scaled_height + 4 }, 
                            2, COLOR_ACCENT);
        
        /* Draw UI */
        draw_status_bar();
        draw_debug_panel();
        draw_controls_help();
        
        EndDrawing();
    }
    
    /* Cleanup */
    loader_shutdown(&state.loader);
    audio_shutdown(&state.audio);
    vga_shutdown(&state.vga);
    gigatron_shutdown(&state.cpu);
    
    UnloadTexture(state.screen_texture);
    UnloadImage(state.screen_image);
    UnloadAudioStream(state.audio_stream);
    CloseAudioDevice();
    CloseWindow();
    
    return 0;
}
