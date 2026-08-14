#include "recomp_ui.h"
#include "zelda_config.h"
#include "zelda_support.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"
#include "RmlUi/Core.h"
#include "nfd.h"
#include <filesystem>

static std::string version_string;

Rml::DataModelHandle model_handle;

// Validity of the ROM for the game the launcher currently has selected, not for
// Majora's Mask specifically. Both halves of the launcher bind to it, and it is
// re-evaluated whenever the selection changes.
bool rom_valid = false;
bool oot_selected = false;

extern std::vector<recomp::GameEntry> supported_games;

// Index into supported_games for the half of the launcher that is selected.
// Majora's Mask is always entry 0; Ocarina of Time is entry 1 when the build has
// it (see RECOMP_OOT in the root CMakeLists), and absent otherwise.
static size_t selected_game_index() {
    if (oot_selected && supported_games.size() > 1) {
        return 1;
    }
    return 0;
}

static recomp::GameEntry& selected_game() {
    return supported_games[selected_game_index()];
}

std::string recompui::get_selected_mod_game_id() {
    return selected_game().mod_game_id;
}

// Re-reads whether the selected game has a valid stored ROM and pushes it to the
// UI. Called on startup and on every switch between the two halves.
static void refresh_rom_valid() {
    rom_valid = recomp::is_rom_valid(selected_game().game_id);
    if (model_handle) {
        model_handle.DirtyVariable("rom_valid");
    }
}

void select_rom() {
    nfdnchar_t* native_path = nullptr;
    zelda64::open_file_dialog([](bool success, const std::filesystem::path& path) {
        if (success) {
            recomp::RomValidationError rom_error = recomp::select_rom(path, selected_game().game_id);
            switch (rom_error) {
                case recomp::RomValidationError::Good:
                    rom_valid = true;
                    model_handle.DirtyVariable("rom_valid");
                    break;
                case recomp::RomValidationError::FailedToOpen:
                    recompui::message_box("Failed to open ROM file.");
                    break;
                case recomp::RomValidationError::NotARom:
                    recompui::message_box("This is not a valid ROM file.");
                    break;
                case recomp::RomValidationError::IncorrectRom:
                    recompui::message_box("This ROM is not the correct game.");
                    break;
                case recomp::RomValidationError::NotYet:
                    recompui::message_box("This game isn't supported yet.");
                    break;
                case recomp::RomValidationError::IncorrectVersion:
                    recompui::message_box(
                            "This ROM is the correct game, but the wrong version.\nThis project requires the NTSC-U N64 version of the game.");
                    break;
                case recomp::RomValidationError::OtherError:
                    recompui::message_box("An unknown error has occurred.");
                    break;
            }
        }
    });
}

recompui::ContextId launcher_context;

recompui::ContextId recompui::get_launcher_context_id() {
	return launcher_context;
}

class LauncherMenu : public recompui::MenuController {
public:
    LauncherMenu() {
        refresh_rom_valid();
    }
    ~LauncherMenu() override {

    }
    void load_document() override {
		launcher_context = recompui::create_context(zelda64::get_asset_path("launcher.rml"));
    }
    void register_events(recompui::UiEventListenerInstancer& listener) override {
        recompui::register_event(listener, "select_rom",
            [](const std::string& param, Rml::Event& event) {
                select_rom();
            }
        );
        recompui::register_event(listener, "rom_selected",
            [](const std::string& param, Rml::Event& event) {
                rom_valid = true;
                model_handle.DirtyVariable("rom_valid");
            }
        );
        recompui::register_event(listener, "start_game",
            [](const std::string& param, Rml::Event& event) {
                const recomp::GameEntry& game = selected_game();
                printf("Starting %s\n", reinterpret_cast<const char*>(game.game_id.c_str()));
                recomp::start_game(game.game_id);
                recompui::hide_all_contexts();
            }
        );
        // The two title buttons act as a switch between the halves of the
        // launcher. Each half's menu is bound to oot_selected, so flipping it
        // moves the menu and the highlight together.
        recompui::register_event(listener, "select_oot",
            [](const std::string& param, Rml::Event& event) {
                if (oot_selected) {
                    return;
                }
                oot_selected = true;
                model_handle.DirtyVariable("oot_selected");
                refresh_rom_valid();
            }
        );
        recompui::register_event(listener, "select_mm",
            [](const std::string& param, Rml::Event& event) {
                if (!oot_selected) {
                    return;
                }
                oot_selected = false;
                model_handle.DirtyVariable("oot_selected");
                refresh_rom_valid();
            }
        );
        recompui::register_event(listener, "open_controls",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::Controls);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_settings",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::General);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_mods",
            [](const std::string &param, Rml::Event &event) {
                recompui::set_config_tab(recompui::ConfigTab::Mods);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "exit_game",
            [](const std::string& param, Rml::Event& event) {
                ultramodern::quit();
            }
        );
    }
    void make_bindings(Rml::Context* context) override {
        Rml::DataModelConstructor constructor = context->CreateDataModel("launcher_model");

        constructor.Bind("rom_valid", &rom_valid);
        constructor.Bind("oot_selected", &oot_selected);

        // False in an MM-only build, where the Ocarina of Time title keeps its
        // original disabled "Coming Soon" treatment rather than becoming a switch.
        static bool oot_available = supported_games.size() > 1;
        constructor.Bind("oot_available", &oot_available);

        version_string = recomp::get_project_version().to_string();
        constructor.Bind("version_number", &version_string);

        model_handle = constructor.GetModelHandle();
    }
};

std::unique_ptr<recompui::MenuController> recompui::create_launcher_menu() {
    return std::make_unique<LauncherMenu>();
}
