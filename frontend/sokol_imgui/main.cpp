/**
 * Gigatron TTL Microcomputer Emulator
 * 
 * A sokol + imgui based frontend for the Gigatron emulator.
 */

#define SOKOL_IMPL
#define SOKOL_GLCORE
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_audio.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#include "sokol_time.h"

#define SOKOL_IMGUI_IMPL
#include "imgui.h"
#include "util/sokol_imgui.h"

#include "nfd.h"

extern "C" {
#include "gigatron.h"
#include "vga.h"
#include "audio.h"
#include "loader.h"
}

#include <cstdio>
#include <cstring>
#include <cmath>

/* ============================================================================
 * Application State
 * ============================================================================ */

static struct {
    /* Emulator core */
    gigatron_t cpu;
    vga_t vga;
    audio_t audio;
    loader_t loader;
    
    /* Graphics */
    sg_pass_action pass_action;
    sg_image screen_texture;
    sg_sampler screen_sampler;
    sg_view screen_view;
    
    /* UI state */
    bool show_debug_window;
    bool show_cpu_state;
    bool show_memory_viewer;
    bool emulator_running;
    bool rom_loaded;
    
    /* Input state */
    uint8_t button_state;
    
    /* Audio buffer for sokol_audio */
    float audio_buffer[4096];
    
    /* Performance metrics */
    uint64_t last_time;
    double frame_time_ms;
    double emulator_speed;
    
    /* Status message */
    char status_message[256];
    float status_timeout;
    
    /* ROM path */
    char rom_path[512];
} state;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static void set_status(const char* msg) {
    strncpy(state.status_message, msg, sizeof(state.status_message) - 1);
    state.status_timeout = 3.0f;
}

/* ============================================================================
 * Audio Callback
 * ============================================================================ */

static void audio_callback(float* buffer, int num_frames, int num_channels) {
    /* Read samples from emulator audio buffer */
    uint32_t samples_read = audio_read_samples(&state.audio, state.audio_buffer, (uint32_t)num_frames);
    
    /* Fill output buffer (mono to stereo if needed) */
    for (int i = 0; i < num_frames; i++) {
        float sample = (i < (int)samples_read) ? state.audio_buffer[i] : 0.0f;
        for (int ch = 0; ch < num_channels; ch++) {
            buffer[i * num_channels + ch] = sample;
        }
    }
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
    gt1_file_t* gt1 = loader_load_gt1_file(path);
    if (gt1) {
        if (loader_start(&state.loader, gt1)) {
            set_status("Loading GT1 file...");
            return true;
        }
        loader_free_gt1(gt1);
    }
    set_status("Failed to load GT1 file");
    return false;
}

static void open_rom_dialog() {
    nfdchar_t* path = NULL;
    nfdfilteritem_t filters[1] = { { "ROM Files", "rom" } };
    nfdresult_t result = NFD_OpenDialog(&path, filters, 1, NULL);
    
    if (result == NFD_OKAY) {
        load_rom(path);
        NFD_FreePath(path);
    }
}

static void open_gt1_dialog() {
    nfdchar_t* path = NULL;
    nfdfilteritem_t filters[1] = { { "GT1 Files", "gt1" } };
    nfdresult_t result = NFD_OpenDialog(&path, filters, 1, NULL);
    
    if (result == NFD_OKAY) {
        load_gt1(path);
        NFD_FreePath(path);
    }
}

/* ============================================================================
 * Emulator Core
 * ============================================================================ */

/* Execute one frame of emulation (used by step function) */
static void run_one_frame() {
    if (!state.rom_loaded) return;
    
    /* Run enough cycles for ~60fps (6.25MHz / 60 = ~104166 cycles per frame) */
    const uint32_t cycles_per_frame = state.cpu.hz / 60;
    
    for (uint32_t i = 0; i < cycles_per_frame; i++) {
        /* 
         * IMPORTANT: Only update input from user when loader is not active!
         * The loader controls in_reg to send data bits via the serial protocol.
         * This matches jsemu behavior where gamepad.stop() is called during loading.
         */
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

static void run_emulator_frame() {
    if (!state.rom_loaded || !state.emulator_running) return;
    run_one_frame();
}

static void update_screen_texture() {
    if (!state.vga.pixels) return;
    
    sg_image_data img_data = {};
    img_data.mip_levels[0].ptr = state.vga.pixels;
    img_data.mip_levels[0].size = VGA_WIDTH * VGA_HEIGHT * 4;
    sg_update_image(state.screen_texture, &img_data);
}

/* ============================================================================
 * Input Handling
 * ============================================================================ */

static void handle_key(sapp_keycode key, bool down) {
    uint8_t bit = 0;
    
    switch (key) {
        case SAPP_KEYCODE_UP:
        case SAPP_KEYCODE_W:
            bit = GIGATRON_BTN_UP;
            break;
        case SAPP_KEYCODE_DOWN:
        case SAPP_KEYCODE_S:
            bit = GIGATRON_BTN_DOWN;
            break;
        case SAPP_KEYCODE_LEFT:
        case SAPP_KEYCODE_A:
            bit = GIGATRON_BTN_LEFT;
            break;
        case SAPP_KEYCODE_RIGHT:
        case SAPP_KEYCODE_D:
            bit = GIGATRON_BTN_RIGHT;
            break;
        case SAPP_KEYCODE_Z:
        case SAPP_KEYCODE_J:
            bit = GIGATRON_BTN_A;
            break;
        case SAPP_KEYCODE_X:
        case SAPP_KEYCODE_K:
            bit = GIGATRON_BTN_B;
            break;
        case SAPP_KEYCODE_ENTER:
            bit = GIGATRON_BTN_START;
            break;
        case SAPP_KEYCODE_BACKSPACE:
        case SAPP_KEYCODE_ESCAPE:
            bit = GIGATRON_BTN_SELECT;
            break;
        default:
            return;
    }
    
    if (down) {
        state.button_state |= bit;
    } else {
        state.button_state &= ~bit;
    }
}

/* ============================================================================
 * UI Rendering
 * ============================================================================ */

static void draw_main_menu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open ROM...", "Ctrl+O")) {
                open_rom_dialog();
            }
            if (ImGui::MenuItem("Load GT1...", "Ctrl+L", false, state.rom_loaded)) {
                open_gt1_dialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset", "F5", false, state.rom_loaded)) {
                gigatron_reset(&state.cpu);
                vga_reset(&state.vga);
                audio_reset(&state.audio);
                loader_reset(&state.loader);
                set_status("Emulator reset");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                sapp_quit();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Emulation")) {
            if (ImGui::MenuItem(state.emulator_running ? "Pause" : "Resume", "Space", false, state.rom_loaded)) {
                state.emulator_running = !state.emulator_running;
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Debug Window", "F1", &state.show_debug_window);
            ImGui::MenuItem("CPU State", "F2", &state.show_cpu_state);
            ImGui::MenuItem("Memory Viewer", "F3", &state.show_memory_viewer);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Controls")) {
                set_status("Arrow/WASD: D-pad | Z/J: A | X/K: B | Enter: Start | Esc: Select");
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }
}

static void draw_screen_window() {
    ImGui::SetNextWindowSize(ImVec2(660, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    
    if (ImGui::Begin("Gigatron Display", nullptr, flags)) {
        /* Calculate display size maintaining aspect ratio */
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float aspect = (float)VGA_WIDTH / (float)VGA_HEIGHT;
        
        float display_w = avail.x;
        float display_h = display_w / aspect;
        
        if (display_h > avail.y) {
            display_h = avail.y;
            display_w = display_h * aspect;
        }
        
        /* Center the display */
        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(
            cursor.x + (avail.x - display_w) * 0.5f,
            cursor.y + (avail.y - display_h) * 0.5f
        ));
        
        /* Draw the screen texture */
        ImGui::Image((ImTextureID)simgui_imtextureid_with_sampler(state.screen_view, state.screen_sampler), 
                    ImVec2(display_w, display_h));
    }
    ImGui::End();
}

static void draw_debug_window() {
    if (!state.show_debug_window) return;
    
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(700, 40), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Debug", &state.show_debug_window)) {
        ImGui::Text("Frame Time: %.2f ms", state.frame_time_ms);
        ImGui::Text("FPS: %.1f", state.frame_time_ms > 0 ? 1000.0 / state.frame_time_ms : 0);
        ImGui::Text("VGA Frames: %u", state.vga.frame_count);
        ImGui::Text("CPU Cycles: %llu", (unsigned long long)state.cpu.cycles);
        ImGui::Text("RAM: %uKB (bank=0x%05X, ctrl=0x%02X)", 
                    state.cpu.ram_size / 1024, state.cpu.bank, state.cpu.ctrl);
        ImGui::Separator();
        ImGui::Text("Audio Samples: %u", audio_available_samples(&state.audio));
        ImGui::Text("Loader State: %d", state.loader.state);
        ImGui::Separator();
        
        if (ImGui::Button("Step (1 cycle)") && state.rom_loaded) {
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
        ImGui::SameLine();
        if (ImGui::Button("Step (1 frame)") && state.rom_loaded) {
            run_one_frame();  /* Use run_one_frame instead of run_emulator_frame to allow stepping when paused */
        }
    }
    ImGui::End();
}

static void draw_cpu_state_window() {
    if (!state.show_cpu_state) return;
    
    ImGui::SetNextWindowSize(ImVec2(250, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(700, 260), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("CPU State", &state.show_cpu_state)) {
        ImGui::Text("Registers:");
        ImGui::Separator();
        ImGui::Text("PC:     0x%04X", state.cpu.pc);
        ImGui::Text("Next PC:0x%04X", state.cpu.next_pc);
        ImGui::Text("AC:     0x%02X (%3d)", state.cpu.ac, state.cpu.ac);
        ImGui::Text("X:      0x%02X (%3d)", state.cpu.x, state.cpu.x);
        ImGui::Text("Y:      0x%02X (%3d)", state.cpu.y, state.cpu.y);
        ImGui::Separator();
        ImGui::Text("OUT:    0x%02X", state.cpu.out);
        ImGui::Text("  HSYNC: %d", (state.cpu.out & GIGATRON_OUT_HSYNC) ? 1 : 0);
        ImGui::Text("  VSYNC: %d", (state.cpu.out & GIGATRON_OUT_VSYNC) ? 1 : 0);
        ImGui::Text("  Color: 0x%02X", state.cpu.out & 0x3F);
        ImGui::Text("OUTX:   0x%02X", state.cpu.outx);
        ImGui::Separator();
        ImGui::Text("IN:     0x%02X", state.cpu.in_reg);
        ImGui::Text("Buttons: %s%s%s%s%s%s%s%s",
            (state.button_state & GIGATRON_BTN_UP) ? "U" : "-",
            (state.button_state & GIGATRON_BTN_DOWN) ? "D" : "-",
            (state.button_state & GIGATRON_BTN_LEFT) ? "L" : "-",
            (state.button_state & GIGATRON_BTN_RIGHT) ? "R" : "-",
            (state.button_state & GIGATRON_BTN_A) ? "A" : "-",
            (state.button_state & GIGATRON_BTN_B) ? "B" : "-",
            (state.button_state & GIGATRON_BTN_START) ? "S" : "-",
            (state.button_state & GIGATRON_BTN_SELECT) ? "s" : "-"
        );
        
        if (state.rom_loaded) {
            ImGui::Separator();
            uint16_t ir = state.cpu.rom[state.cpu.pc];
            ImGui::Text("Current IR: 0x%04X", ir);
            ImGui::Text("  OP:   %d", (ir >> 13) & 0x07);
            ImGui::Text("  MODE: %d", (ir >> 10) & 0x07);
            ImGui::Text("  BUS:  %d", (ir >> 8) & 0x03);
            ImGui::Text("  D:    0x%02X", ir & 0xFF);
        }
    }
    ImGui::End();
}

static void draw_memory_viewer() {
    if (!state.show_memory_viewer) return;
    
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Memory Viewer", &state.show_memory_viewer)) {
        static int view_addr = 0;
        static bool show_rom = false;
        
        ImGui::Checkbox("Show ROM", &show_rom);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Address", &view_addr, 16, 256);
        
        if (view_addr < 0) view_addr = 0;
        
        const int bytes_per_row = 16;
        const int rows = 16;
        
        ImGui::BeginChild("MemoryView", ImVec2(0, 0), true);
        
        if (show_rom && state.cpu.rom) {
            int max_addr = (int)state.cpu.rom_size * 2;
            if (view_addr >= max_addr) view_addr = max_addr - 1;
            
            ImGui::Text("ROM (16-bit words as bytes):");
            for (int row = 0; row < rows; row++) {
                int addr = view_addr + row * bytes_per_row;
                if (addr >= max_addr) break;
                
                ImGui::Text("%04X: ", addr);
                ImGui::SameLine();
                
                for (int col = 0; col < bytes_per_row && addr + col < max_addr; col++) {
                    int word_idx = (addr + col) / 2;
                    uint16_t word = state.cpu.rom[word_idx];
                    uint8_t byte = ((addr + col) & 1) ? (word & 0xFF) : (word >> 8);
                    ImGui::Text("%02X ", byte);
                    ImGui::SameLine();
                }
                ImGui::NewLine();
            }
        } else if (state.cpu.ram) {
            int max_addr = (int)state.cpu.ram_size;
            if (view_addr >= max_addr) view_addr = max_addr - 1;
            
            ImGui::Text("RAM (8-bit bytes):");
            for (int row = 0; row < rows; row++) {
                int addr = view_addr + row * bytes_per_row;
                if (addr >= max_addr) break;
                
                ImGui::Text("%04X: ", addr);
                ImGui::SameLine();
                
                /* Hex view */
                for (int col = 0; col < bytes_per_row && addr + col < max_addr; col++) {
                    ImGui::Text("%02X ", state.cpu.ram[addr + col]);
                    ImGui::SameLine();
                }
                
                ImGui::Text(" | ");
                ImGui::SameLine();
                
                /* ASCII view */
                for (int col = 0; col < bytes_per_row && addr + col < max_addr; col++) {
                    uint8_t c = state.cpu.ram[addr + col];
                    ImGui::Text("%c", (c >= 32 && c < 127) ? c : '.');
                    ImGui::SameLine();
                }
                ImGui::NewLine();
            }
        }
        
        ImGui::EndChild();
    }
    ImGui::End();
}

static void draw_status_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - 25));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 25));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    
    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        if (state.status_timeout > 0) {
            ImGui::Text("%s", state.status_message);
        } else if (state.rom_loaded) {
            ImGui::Text("ROM: %s | %s", state.rom_path, 
                       state.emulator_running ? "Running" : "Paused");
        } else {
            ImGui::Text("No ROM loaded - Press Ctrl+O to open a ROM file");
        }
    }
    ImGui::End();
    
    ImGui::PopStyleVar(2);
}

/* ============================================================================
 * Sokol App Callbacks
 * ============================================================================ */

static void init(void) {
    /* Initialize sokol_gfx */
    sg_desc sg_desc_ = {};
    sg_desc_.environment = sglue_environment();
    sg_desc_.logger.func = slog_func;
    sg_setup(&sg_desc_);
    
    /* Initialize sokol_imgui */
    simgui_desc_t simgui_desc = {};
    simgui_desc.logger.func = slog_func;
    simgui_setup(&simgui_desc);
    
    /* Initialize sokol_audio */
    saudio_desc saudio_desc_ = {};
    saudio_desc_.sample_rate = AUDIO_SAMPLE_RATE;
    saudio_desc_.num_channels = 2;
    saudio_desc_.stream_cb = audio_callback;
    saudio_desc_.logger.func = slog_func;
    saudio_setup(&saudio_desc_);
    
    /* Initialize sokol_time */
    stm_setup();
    
    /* Initialize NFD */
    NFD_Init();
    
    /* Initialize emulator */
    gigatron_config_t cpu_config = gigatron_default_config();
    gigatron_init(&state.cpu, &cpu_config);
    vga_init(&state.vga, &state.cpu);
    audio_init(&state.audio, &state.cpu);
    loader_init(&state.loader, &state.cpu);
    
    /* Create screen texture */
    sg_image_desc img_desc = {};
    img_desc.width = VGA_WIDTH;
    img_desc.height = VGA_HEIGHT;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.usage.stream_update = true;
    state.screen_texture = sg_make_image(&img_desc);
    
    /* Create sampler */
    sg_sampler_desc smp_desc = {};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    state.screen_sampler = sg_make_sampler(&smp_desc);
    
    /* Create texture view for ImGui */
    sg_view_desc view_desc = {};
    view_desc.texture.image = state.screen_texture;
    state.screen_view = sg_make_view(&view_desc);
    
    /* Clear color */
    state.pass_action.colors[0] = { 
        .load_action = SG_LOADACTION_CLEAR, 
        .clear_value = { 0.1f, 0.1f, 0.15f, 1.0f } 
    };
    
    /* Initial state */
    state.emulator_running = false;
    state.rom_loaded = false;
    state.button_state = 0;
    state.show_debug_window = false;
    state.show_cpu_state = false;
    state.show_memory_viewer = false;
    state.last_time = stm_now();
    
    /* Try to load default ROM */
    if (load_rom("roms/gigatron.rom")) {
        set_status("Default ROM loaded");
    }
}

static void frame(void) {
    /* Calculate frame time */
    uint64_t now = stm_now();
    state.frame_time_ms = stm_ms(stm_diff(now, state.last_time));
    state.last_time = now;
    
    /* Update status timeout */
    if (state.status_timeout > 0) {
        state.status_timeout -= (float)(state.frame_time_ms / 1000.0);
    }
    
    /* Run emulator */
    run_emulator_frame();
    
    /* Update screen texture */
    if (vga_frame_ready(&state.vga) || !state.emulator_running) {
        update_screen_texture();
    }
    
    /* Begin ImGui frame */
    const int width = sapp_width();
    const int height = sapp_height();
    simgui_new_frame({ width, height, sapp_frame_duration(), sapp_dpi_scale() });
    
    /* Draw UI */
    draw_main_menu();
    draw_screen_window();
    draw_debug_window();
    draw_cpu_state_window();
    draw_memory_viewer();
    draw_status_bar();
    
    /* Render */
    sg_pass pass = {};
    pass.action = state.pass_action;
    pass.swapchain = sglue_swapchain();
    
    sg_begin_pass(&pass);
    simgui_render();
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    /* Cleanup emulator */
    loader_shutdown(&state.loader);
    audio_shutdown(&state.audio);
    vga_shutdown(&state.vga);
    gigatron_shutdown(&state.cpu);
    
    /* Cleanup NFD */
    NFD_Quit();
    
    /* Cleanup sokol graphics resources */
    sg_destroy_view(state.screen_view);
    sg_destroy_sampler(state.screen_sampler);
    sg_destroy_image(state.screen_texture);
    
    /* Cleanup sokol */
    saudio_shutdown();
    simgui_shutdown();
    sg_shutdown();
}

static void event(const sapp_event* ev) {
    /* Let ImGui handle events first */
    if (simgui_handle_event(ev)) {
        /* If ImGui captured the event, don't process it further */
        if (ImGui::GetIO().WantCaptureKeyboard) {
            return;
        }
    }
    
    switch (ev->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (ev->modifiers & SAPP_MODIFIER_CTRL) {
                if (ev->key_code == SAPP_KEYCODE_O) {
                    open_rom_dialog();
                } else if (ev->key_code == SAPP_KEYCODE_L) {
                    if (state.rom_loaded) open_gt1_dialog();
                }
            } else if (!ev->key_repeat) {
                switch (ev->key_code) {
                    case SAPP_KEYCODE_F1:
                        state.show_debug_window = !state.show_debug_window;
                        break;
                    case SAPP_KEYCODE_F2:
                        state.show_cpu_state = !state.show_cpu_state;
                        break;
                    case SAPP_KEYCODE_F3:
                        state.show_memory_viewer = !state.show_memory_viewer;
                        break;
                    case SAPP_KEYCODE_F5:
                        if (state.rom_loaded) {
                            gigatron_reset(&state.cpu);
                            vga_reset(&state.vga);
                            audio_reset(&state.audio);
                            loader_reset(&state.loader);
                            set_status("Emulator reset");
                        }
                        break;
                    case SAPP_KEYCODE_SPACE:
                        if (state.rom_loaded) {
                            state.emulator_running = !state.emulator_running;
                        }
                        break;
                    default:
                        handle_key(ev->key_code, true);
                        break;
                }
            }
            break;
            
        case SAPP_EVENTTYPE_KEY_UP:
            handle_key(ev->key_code, false);
            break;
            
        case SAPP_EVENTTYPE_FILES_DROPPED: {
            const int num_files = sapp_get_num_dropped_files();
            if (num_files > 0) {
                const char* path = sapp_get_dropped_file_path(0);
                size_t len = strlen(path);
                if (len > 4) {
                    if (strcmp(path + len - 4, ".rom") == 0 || 
                        strcmp(path + len - 4, ".ROM") == 0) {
                        load_rom(path);
                    } else if (strcmp(path + len - 4, ".gt1") == 0 ||
                               strcmp(path + len - 4, ".GT1") == 0) {
                        if (state.rom_loaded) {
                            load_gt1(path);
                        } else {
                            set_status("Please load a ROM first");
                        }
                    }
                }
            }
            break;
        }
        
        default:
            break;
    }
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1024;
    desc.height = 720;
    desc.window_title = "Gigatron TTL Emulator";
    desc.icon.sokol_default = true;
    desc.enable_dragndrop = true;
    desc.max_dropped_files = 1;
    desc.logger.func = slog_func;
    desc.high_dpi = true;
    
    return desc;
}
