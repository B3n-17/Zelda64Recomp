/*
 * Minimal stand-ins for the two recompui symbols that rt64_render_context.cpp
 * needs. The full UI (RmlUi launcher, config menus, mod manager) is not built
 * for the OoT milestone target, but the render context is worth reusing as-is
 * rather than forking, and it only reaches for these two.
 *
 * The declarations are repeated here rather than pulled from include/recomp_ui.h,
 * because that header includes RmlUi/Core.h and would drag the whole UI stack
 * into a target that deliberately does not build it.
 */
#include <cstdio>

namespace recompui {
    void message_box(const char* msg);
    void set_render_hooks();
}

void recompui::message_box(const char* msg) {
    fprintf(stderr, "[message_box] %s\n", msg);
}

void recompui::set_render_hooks() {
    // The UI draws nothing here, so there are no hooks to install.
}
