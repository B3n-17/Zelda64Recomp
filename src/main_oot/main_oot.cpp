/*
 * Minimal standalone launcher for Ocarina of Time.
 *
 * This is deliberately NOT a port of src/main/main.cpp. That file carries the
 * whole Zelda64Recomp app: RmlUi launcher, config system, mod menu, texture
 * packs, input remapping. None of that is needed to answer the question this
 * milestone exists to answer, which is "does the recompiled OoT boot".
 *
 * So this launcher does the minimum:
 *   - creates an SDL window and an RT64 render context
 *   - registers the OoT game entry, overlays and patches
 *   - loads the ROM directly from argv[1], bypassing the UI ROM picker
 *   - starts the game from update_gfx once the renderer is up, since there is no
 *     launcher UI to do it
 *
 * Audio and input are real but simplified: fixed key/pad bindings rather than the
 * MM app's remappable config, and no resampling in the audio path.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
// PRIu32 and friends. These arrive transitively through libstdc++ headers on
// Linux, but not through the MSVC STL, so include them explicitly.
#include <cinttypes>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/error_handling.hpp"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/rsp.hpp"
#include "librecomp/mods.hpp"
#include "recomp_data.h"
#include "crash_handler_oot.hpp"

#define SDL_MAIN_HANDLED
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "SDL.h"
// Declares SDL_SysWMinfo, needed to pull the HWND out of the SDL window.
#include "SDL_syswm.h"
#else
#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
// Undefine x11 macros that get included by SDL_syswm.h. Without this, X11's
// `None` collides with ultramodern::input::Device::None and the enum fails to
// parse. Same list as src/main/main.cpp.
#undef None
#undef Status
#undef LockMask
#undef ControlMask
#undef Success
#endif

#include "zelda_render.h"

// Recompiled entrypoint for OoT, emitted by N64Recomp.
// Prefixed via func_prefix in oot.us.ntsc-1.0.toml so OoT and MM can be linked
// together; see docs/OOT_MM_FUNCTION_COMPARISON.md.
extern "C" void oot_recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);

// Recompiled RSP microcode for OoT.
extern RspUcodeFunc oot_aspMain;
extern RspUcodeFunc oot_njpgdspMain;

namespace oot {
    void register_overlays();
    void register_patches();
}

template <typename... Ts>
static void exit_error(const char* str, Ts... args) {
    ((void)fprintf(stderr, str, args), ...);
    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

// ---------------------------------------------------------------- graphics

static SDL_Window* window = nullptr;

static ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    // SDL_INIT_AUDIO is required: without it SDL_OpenAudioDevice fails and the
    // game runs silently. SDL_Init reports failure with a negative return, not a
    // positive one.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        exit_error("Failed to initialize SDL2: %s\n", SDL_GetError());
    }

    fprintf(stdout, "SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());
    return {};
}

static ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    uint32_t flags = SDL_WINDOW_RESIZABLE;
#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#elif defined(RT64_SDL_WINDOW_VULKAN)
    flags |= SDL_WINDOW_VULKAN;
#endif

    window = SDL_CreateWindow("Ocarina of Time: Recompiled", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 960,
                              flags);
    if (window == nullptr) {
        exit_error("Failed to create window: %s\n", SDL_GetError());
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);

#if defined(_WIN32)
    return ultramodern::renderer::WindowHandle{ wmInfo.info.win.window, GetCurrentThreadId() };
#elif defined(__linux__) || defined(__ANDROID__)
    return ultramodern::renderer::WindowHandle{ window };
#else
    static_assert(false && "Unimplemented");
#endif
}

static void reset_audio(uint32_t output_freq);
static SDL_GameController* game_controller;
extern uint32_t sample_rate;

// Set once the renderer is up, via the gfx_init callback.
static std::atomic<bool> gfx_ready{ false };
static std::u8string pending_game_id;

static void on_gfx_init() {
    gfx_ready.store(true);
    // Open the audio device once graphics are up. Previously the device was only
    // opened from set_frequency, so if the game never changed the rate, nothing
    // was ever queued and the game ran silent.
    reset_audio(sample_rate);
}


static void update_gfx(void*) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            ultramodern::quit();
        }
        else if (e.type == SDL_CONTROLLERDEVICEADDED && game_controller == nullptr) {
            game_controller = SDL_GameControllerOpen(e.cdevice.which);
            if (game_controller != nullptr) {
                fprintf(stdout, "Controller connected: %s\n", SDL_GameControllerName(game_controller));
            }
        }
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED && game_controller != nullptr) {
            SDL_GameControllerClose(game_controller);
            game_controller = nullptr;
        }
    }

    // Deferred game start. This replaces the launcher UI, but it cannot simply be
    // done before start(): ultramodern's VI thread only calls set_dummy_vi() while
    // the game is NOT started, and that is what populates ViState::mode. Starting
    // the game before the VI thread's first tick means update_vi() dereferences a
    // null mode and segfaults. In the MM app the UI makes this a non-issue, since
    // the user cannot pick a rom until long after the VI thread is ticking.
    //
    // So: wait for the renderer, then let the VI thread run a few frames (it ticks
    // at 60Hz, this loop sleeps 1ms per iteration) before starting.
    static int frames_since_gfx_ready = 0;
    static bool game_start_requested = false;

    if (!game_start_requested && gfx_ready.load()) {
        frames_since_gfx_ready++;
        if (frames_since_gfx_ready > 200) {
            game_start_requested = true;
            fprintf(stderr, "[chk] start_game\n");
            recomp::start_game(pending_game_id);
        }
    }
}

// ---------------------------------------------------------------- audio

static SDL_AudioDeviceID audio_device = 0;
uint32_t sample_rate = 48000;
constexpr uint32_t input_channels = 2;

static void queue_samples(int16_t* audio_data, size_t sample_count) {
    // The N64 hands over signed 16-bit stereo with the channels swapped relative
    // to what SDL expects, so unswap while converting to float.
    static std::vector<float> buffer;
    if (buffer.size() < sample_count) {
        buffer.resize(sample_count);
    }

    for (size_t i = 0; i < sample_count; i += input_channels) {
        buffer[i + 0] = audio_data[i + 1] * (0.5f / 32768.0f);
        buffer[i + 1] = audio_data[i + 0] * (0.5f / 32768.0f);
    }

    if (audio_device != 0) {
        SDL_QueueAudio(audio_device, buffer.data(), sample_count * sizeof(float));
    }
}

static size_t get_frames_remaining() {
    if (audio_device == 0) {
        return 0;
    }
    constexpr float bytes_per_frame = input_channels * sizeof(float);
    return (size_t)(SDL_GetQueuedAudioSize(audio_device) / bytes_per_frame);
}

static void reset_audio(uint32_t output_freq) {
    SDL_AudioSpec spec_desired{};
    spec_desired.freq = (int)output_freq;
    spec_desired.format = AUDIO_F32;
    spec_desired.channels = (Uint8)input_channels;
    spec_desired.samples = 0x100;

    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
    }
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &spec_desired, nullptr, 0);
    if (audio_device != 0) {
        SDL_PauseAudioDevice(audio_device, 0);
    }
}

static void set_frequency(uint32_t freq) {
    sample_rate = freq;
    reset_audio(freq);
}

// ---------------------------------------------------------------- input

// N64 controller button bits, matching include/recomp_input.h.
enum N64Button : uint16_t {
    N64_A        = 0x8000,
    N64_B        = 0x4000,
    N64_Z        = 0x2000,
    N64_START    = 0x1000,
    N64_DPAD_UP  = 0x0800,
    N64_DPAD_DN  = 0x0400,
    N64_DPAD_LT  = 0x0200,
    N64_DPAD_RT  = 0x0100,
    N64_L        = 0x0020,
    N64_R        = 0x0010,
    N64_C_UP     = 0x0008,
    N64_C_DOWN   = 0x0004,
    N64_C_LEFT   = 0x0002,
    N64_C_RIGHT  = 0x0001,
};

// Fixed keyboard and gamepad bindings. The MM app has a full remapping and config
// UI for this; a fixed layout is enough to actually play here.
struct KeyBinding { SDL_Scancode key; uint16_t button; };
// WASD is the analog stick and nothing else, so none of these may use those keys.
static constexpr KeyBinding keyboard_bindings[] = {
    { SDL_SCANCODE_SPACE,  N64_A       },
    { SDL_SCANCODE_X,      N64_B       },
    { SDL_SCANCODE_LSHIFT, N64_Z       },  // Z-targeting
    { SDL_SCANCODE_RETURN, N64_START   },
    { SDL_SCANCODE_Q,      N64_L       },
    { SDL_SCANCODE_E,      N64_R       },
    { SDL_SCANCODE_I,      N64_C_UP    },
    { SDL_SCANCODE_K,      N64_C_DOWN  },
    { SDL_SCANCODE_J,      N64_C_LEFT  },
    { SDL_SCANCODE_L,      N64_C_RIGHT },
    { SDL_SCANCODE_UP,     N64_DPAD_UP },
    { SDL_SCANCODE_DOWN,   N64_DPAD_DN },
    { SDL_SCANCODE_LEFT,   N64_DPAD_LT },
    { SDL_SCANCODE_RIGHT,  N64_DPAD_RT },
};

struct PadBinding { SDL_GameControllerButton button; uint16_t n64; };
static constexpr PadBinding controller_bindings[] = {
    { SDL_CONTROLLER_BUTTON_A,             N64_A       },
    { SDL_CONTROLLER_BUTTON_X,             N64_B       },
    { SDL_CONTROLLER_BUTTON_B,             N64_B       },
    { SDL_CONTROLLER_BUTTON_START,         N64_START   },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  N64_L       },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, N64_R       },
    { SDL_CONTROLLER_BUTTON_DPAD_UP,       N64_DPAD_UP },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     N64_DPAD_DN },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     N64_DPAD_LT },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    N64_DPAD_RT },
};

static void poll_inputs() {
    // SDL events are pumped by update_gfx on the main thread; SDL_GameController
    // state is read directly below, so nothing extra is needed here.
}

static float apply_deadzone(float v) {
    constexpr float deadzone = 0.15f;
    if (v > -deadzone && v < deadzone) {
        return 0.0f;
    }
    // Rescale so the stick still reaches full range outside the deadzone.
    float sign = v < 0.0f ? -1.0f : 1.0f;
    return sign * ((v * sign) - deadzone) / (1.0f - deadzone);
}

static bool get_n64_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    if (controller_num != 0) {
        return false;
    }

    uint16_t buttons = 0;
    float x = 0.0f;
    float y = 0.0f;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys != nullptr) {
        for (const auto& binding : keyboard_bindings) {
            if (keys[binding.key]) {
                buttons |= binding.button;
            }
        }
        // Analog stick on WASD. These four keys are deliberately not in
        // keyboard_bindings, so movement never doubles as a button press.
        if (keys[SDL_SCANCODE_W]) y += 1.0f;
        if (keys[SDL_SCANCODE_S]) y -= 1.0f;
        if (keys[SDL_SCANCODE_A]) x -= 1.0f;
        if (keys[SDL_SCANCODE_D]) x += 1.0f;
    }

    if (game_controller != nullptr) {
        for (const auto& binding : controller_bindings) {
            if (SDL_GameControllerGetButton(game_controller, binding.button)) {
                buttons |= binding.n64;
            }
        }
        // Triggers: left trigger is Z, which is where N64 players expect it.
        if (SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000) {
            buttons |= N64_Z;
        }
        if (SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) {
            buttons |= N64_R;
        }
        // Right stick drives the C buttons.
        float cx = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        float cy = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
        if (cx < -0.5f) buttons |= N64_C_LEFT;
        if (cx >  0.5f) buttons |= N64_C_RIGHT;
        if (cy < -0.5f) buttons |= N64_C_UP;
        if (cy >  0.5f) buttons |= N64_C_DOWN;

        x += apply_deadzone(SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f);
        // SDL reports +Y as down, the N64 stick reports +Y as up.
        y += -apply_deadzone(SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f);
    }

    *buttons_out = buttons;
    *x_out = std::clamp(x, -1.0f, 1.0f);
    *y_out = std::clamp(y, -1.0f, 1.0f);
    return true;
}

static void set_rumble(int controller_num, bool on) {
    (void)controller_num;
    (void)on;
}

static ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    // Only port 1 is populated, and claiming otherwise breaks the port mask.
    // ultramodern's __osContGetInitData builds the "which ports are present"
    // bitpattern with `*pattern = 1 << controller` inside its loop over all four
    // ports - an assignment, not an or - so the last connected port overwrites
    // every earlier one. Reporting a controller on all four ports therefore left
    // the pattern at 0x08 (port 4 only), port 1 read as empty, and the game drew
    // "NO CONTROLLER". MM's app never hits this because it reports None for the
    // unused ports, which is what we do here.
    if (controller_num != 0) {
        return ultramodern::input::connected_device_info_t{
            .connected_device = ultramodern::input::Device::None,
            .connected_pak = ultramodern::input::Pak::None,
        };
    }
    return ultramodern::input::connected_device_info_t{
        .connected_device = ultramodern::input::Device::Controller,
        .connected_pak = ultramodern::input::Pak::None,
    };
}

// ---------------------------------------------------------------- rsp

static RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    switch (task->t.type) {
        case M_AUDTASK:
            return oot_aspMain;
        case M_NJPEGTASK:
            return oot_njpgdspMain;
        default:
            fprintf(stderr, "Unknown RSP task: %" PRIu32 "\n", task->t.type);
            return nullptr;
    }
}

static gpr get_entrypoint_address() {
    // OoT NTSC 1.0 boots at 0x80000400. MM boots at 0x80080000.
    return (gpr)(int32_t)0x80000400;
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    // Install first: a fault in recompiled code otherwise kills the process with no
    // output at all, since it never reaches ultramodern's reporting paths.
    register_crash_handler();

    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s <path to Ocarina of Time NTSC 1.0 US rom>\n"
                "The rom may be compressed or uncompressed; librecomp validates it by hash.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    std::filesystem::path rom_path{ argv[1] };
    if (!std::filesystem::exists(rom_path)) {
        fprintf(stderr, "Rom not found: %s\n", rom_path.string().c_str());
        return EXIT_FAILURE;
    }

    fprintf(stderr, "[chk] register_config_path\n");
    recomp::register_config_path(std::filesystem::current_path() / "oot_recomp_data");

    recomp::GameEntry oot_entry{};
    // XXH3_64 of the UNCOMPRESSED rom produced by the decomp
    // (build/ntsc-1.0/oot-ntsc-1.0.z64, 55046144 bytes, md5 6829a16d...).
    // Retail OoT roms are compressed; supporting those directly would need a
    // decompression_routine equivalent to MM's decompress_mm, which does not
    // exist yet. For now the decomp's uncompressed output is the expected input.
    oot_entry.rom_hash = 0x8F46AF6C198CC94CULL;
    oot_entry.internal_name = "THE LEGEND OF ZELDA";
    oot_entry.game_id = u8"oot.n64.us.1.0";
    oot_entry.mod_game_id = "oot";
    oot_entry.save_type = recomp::SaveType::Sram;
    oot_entry.is_enabled = true;
    oot_entry.has_compressed_code = false;
    oot_entry.entrypoint_address = get_entrypoint_address();
    oot_entry.entrypoint = oot_recomp_entrypoint;

    fprintf(stderr, "[chk] register_game\n");
    recomp::register_game(oot_entry);

    fprintf(stderr, "[chk] register_overlays\n");
    oot::register_overlays();
    fprintf(stderr, "[chk] register_patches\n");
    oot::register_patches();

    // The transform tagging patches register actor extensions during boot, and
    // the store refuses registrations until it has been initialized. The main
    // app does this in src/main/main.cpp; this launcher has to do it itself.
    recomputil::init_extended_actor_data();

    fprintf(stderr, "[chk] select_rom\n");
    std::u8string game_id = oot_entry.game_id;
    recomp::RomValidationError rom_error = recomp::select_rom(rom_path, game_id);
    if (rom_error != recomp::RomValidationError::Good) {
        fprintf(stderr, "Rom validation failed (error %d)\n", (int)rom_error);
        return EXIT_FAILURE;
    }

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = get_rsp_microcode,
    };

    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context = zelda64::renderer::create_render_context,
    };

    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
    };

    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = queue_samples,
        .get_frames_remaining = get_frames_remaining,
        .set_frequency = set_frequency,
    };

    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = poll_inputs,
        .get_input = get_n64_input,
        .set_rumble = set_rumble,
        .get_connected_device_info = get_connected_device_info,
    };

    // Establish a graphics config before starting. GraphicsConfig has no default
    // member initialisers and nothing else sets it, so leaving it alone means the
    // renderer runs zero-initialised: Resolution::Original (native 240p) stretched
    // to the window, which looks heavily blurred. The MM app sets this up in
    // zelda64::load_config(), which this launcher does not have.
    {
        ultramodern::renderer::GraphicsConfig gfx_config{};
        gfx_config.developer_mode = false;
        gfx_config.res_option = ultramodern::renderer::Resolution::Auto;
        gfx_config.wm_option = ultramodern::renderer::WindowMode::Windowed;
        gfx_config.hr_option = ultramodern::renderer::HUDRatioMode::Clamp16x9;
        gfx_config.api_option = ultramodern::renderer::GraphicsApi::Auto;
        gfx_config.ar_option = ultramodern::renderer::AspectRatio::Expand;
        gfx_config.msaa_option = ultramodern::renderer::Antialiasing::None;
        gfx_config.rr_option = ultramodern::renderer::RefreshRate::Original;
        gfx_config.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Auto;
        gfx_config.rr_manual_value = 60;
        gfx_config.ds_option = 1;
        ultramodern::renderer::set_graphics_config(gfx_config);
    }

    pending_game_id = game_id;

    ultramodern::events::callbacks_t thread_callbacks{
        .vi_callback = nullptr,
        .gfx_init_callback = on_gfx_init,
    };
    ultramodern::error_handling::callbacks_t error_handling_callbacks{};
    ultramodern::threads::callbacks_t threads_callbacks{};

    fprintf(stderr, "[chk] start\n");
    recomp::start(recomp::Version{ 1, 0, 0 }, {}, rsp_callbacks, renderer_callbacks, audio_callbacks, input_callbacks,
                  gfx_callbacks, thread_callbacks, error_handling_callbacks, threads_callbacks);

    return EXIT_SUCCESS;
}
