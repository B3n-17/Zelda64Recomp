# Standalone CMake project for the Ocarina of Time milestone target.
#
# Kept separate from the root CMakeLists.txt on purpose: the main project's targets
# need MM-specific generated artifacts (rsp/aspMain.cpp from the MM rom,
# RecompiledFuncs/) that do not exist in an OoT-only checkout, so configuring it
# would fail before reaching anything OoT related.
#
# Configure and build with:
#   cmake -S oot_app -B build-oot -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-oot
#
# See docs/OOT_SUPPORT.md.

cmake_minimum_required(VERSION 3.20)

project(OoTRecompiled LANGUAGES C CXX)

# This project lives in oot_app/, one level below the repo root.
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Clang is REQUIRED, not merely preferred. N64Recomp's RECOMP_FUNC expands to
# `extern inline __attribute__((weak,noinline))` under Clang, and the weak linkage
# is what lets a patch in PatchesLibOoT override the recompiled function of the
# same name. Under GCC it expands to `__attribute__((noipa, ...))`, which emits a
# strong symbol, so linking the patches produces "multiple definition" errors.
# See include/recomp.h in N64Recomp.
if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "This project must be built with Clang (found C: '${CMAKE_C_COMPILER_ID}', CXX: '${CMAKE_CXX_COMPILER_ID}').\n"
        "The function patching mechanism depends on Clang's weak symbol emission for RECOMP_FUNC.\n"
        "Reconfigure with: -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++")
endif()
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(HLSL_CPU)
# Only on non-Windows. On Windows RT64 drives a native HWND with D3D12, and
# defining this makes rt64_application_window.cpp take the SDL path, where it
# assigns a RenderWindow (HWND) to an SDL_Window* and fails to compile. The main
# project scopes this define the same way.
if (NOT WIN32)
    add_compile_definitions(RT64_SDL_WINDOW_VULKAN)
endif()

if (WIN32)
    # SDL_endian.h carries a workaround, written for Clang 11, that defines
    # _m_prefetch itself under _MSC_VER and guards it with __PRFCHWINTRIN_H.
    # Clang 22 makes _m_prefetch a real builtin, so that definition is now a hard
    # error ("definition of builtin function"), not a warning we could demote.
    # Predefining the guard makes SDL skip its version; the builtin stays callable
    # because Clang implicitly declares builtins without the header. This hits both
    # SDL copies in the build - rt64's bundled 2.26.3 and the fetched 2.30.3 - so
    # it has to be global rather than scoped to one target.
    add_compile_definitions(__PRFCHWINTRIN_H)

    # windows.h defines min/max as macros, which breaks every std::min/std::max
    # call in rt64_render_context.cpp. rt64 already sets this on itself; the main
    # project sets it globally on Windows, and so do we.
    add_compile_definitions(NOMINMAX)
endif()

add_subdirectory(${REPO_ROOT}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)
add_subdirectory(${REPO_ROOT}/lib/N64ModernRuntime ${CMAKE_BINARY_DIR}/n64mr)

target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)

# RT64 builds with -Werror, and we are on a newer Clang than it is tested against
# (Clang 21 flags -Wuninitialized-const-pointer in its vendored imgui, for one).
# Demote those to warnings rather than patching vendored third-party sources or
# chasing each new diagnostic as the compiler moves. Appending -Wno-error works
# because the later flag wins over the -Werror rt64 sets on itself.
if (MSVC)
    target_compile_options(rt64 PRIVATE /clang:-Wno-error)
else()
    target_compile_options(rt64 PRIVATE -Wno-error)
endif()

set(RECOMP_INCLUDES
    ${REPO_ROOT}/include
    ${REPO_ROOT}/lib/N64ModernRuntime/ultramodern/include
    ${REPO_ROOT}/lib/N64ModernRuntime/librecomp/include
    ${REPO_ROOT}/lib/N64ModernRuntime/N64Recomp/include
)

# Primary recompiler output for OoT.
add_library(RecompiledFuncsOoT STATIC)
if (MSVC)
    target_compile_options(RecompiledFuncsOoT PRIVATE
        /clang:-fno-strict-aliasing
        /clang:-Wno-unused-variable
        /clang:-Wno-implicit-function-declaration
    )
else()
    target_compile_options(RecompiledFuncsOoT PRIVATE
        -fno-strict-aliasing
        -Wno-unused-variable
        -Wno-implicit-function-declaration
    )
endif()
target_include_directories(RecompiledFuncsOoT PRIVATE ${RECOMP_INCLUDES})
file(GLOB OOT_FUNC_SOURCES ${REPO_ROOT}/RecompiledFuncsOoT/*.c)
target_sources(RecompiledFuncsOoT PRIVATE ${OOT_FUNC_SOURCES})

# Recompiled output for the OoT patches.
add_library(PatchesLibOoT STATIC)
if (MSVC)
    target_compile_options(PatchesLibOoT PRIVATE
        /clang:-fno-strict-aliasing
        /clang:-Wno-unused-variable
        /clang:-Wno-implicit-function-declaration
    )
else()
    target_compile_options(PatchesLibOoT PRIVATE
        -fno-strict-aliasing
        -Wno-unused-variable
        -Wno-implicit-function-declaration
    )
endif()
target_include_directories(PatchesLibOoT PRIVATE ${RECOMP_INCLUDES})
target_sources(PatchesLibOoT PRIVATE
    ${REPO_ROOT}/RecompiledPatchesOoT/patches.c
    ${REPO_ROOT}/RecompiledPatchesOoT/patches_bin.c
)

add_executable(OoTRecompiled
    ${REPO_ROOT}/src/main_oot/main_oot.cpp
    ${REPO_ROOT}/src/main_oot/register_overlays_oot.cpp
    ${REPO_ROOT}/src/main_oot/register_patches_oot.cpp
    ${REPO_ROOT}/src/main_oot/ui_stubs_oot.cpp
    ${REPO_ROOT}/src/main_oot/runtime_funcs_oot.cpp
    # Only for this target. The combined launcher gets the identical MM version
    # from src/game/recomp_api.cpp instead.
    ${REPO_ROOT}/src/main_oot/recomp_load_overlays_oot.cpp
    ${REPO_ROOT}/src/main_oot/host_stubs_oot.cpp
    ${REPO_ROOT}/src/main_oot/crash_handler_oot.cpp
    # The actor extension store the transform tagging patches are built on.
    # Game agnostic; its one UI call (message_box) is satisfied by ui_stubs_oot.
    ${REPO_ROOT}/src/game/recomp_actor_api.cpp
    # Reused unchanged from the MM app - it is game agnostic apart from two
    # recompui symbols, which ui_stubs_oot.cpp provides.
    ${REPO_ROOT}/src/main/rt64_render_context.cpp
    # Recompiled RSP microcode for OoT.
    ${REPO_ROOT}/rsp_oot/aspMain.cpp
    ${REPO_ROOT}/rsp_oot/njpgdspMain.cpp
)

target_include_directories(OoTRecompiled PRIVATE
    ${RECOMP_INCLUDES}
    ${REPO_ROOT}/lib/rt64/src
    ${REPO_ROOT}/lib/rt64/include
    ${REPO_ROOT}/lib/rt64/src/contrib
    # rt64_hlslpp.h includes hlsl++.h from here.
    ${REPO_ROOT}/lib/rt64/src/contrib/hlslpp/include
    ${REPO_ROOT}/lib/rt64/src/contrib/dxc/inc
    # rt64_render_context.cpp includes rt64_render_hooks.h from rhi/.
    ${REPO_ROOT}/lib/rt64/src/rhi
    ${REPO_ROOT}/lib/rt64/src/render
    ${REPO_ROOT}/lib/concurrentqueue
    ${REPO_ROOT}/lib/SlotMap
    # rt64_render_context.cpp includes recomp_ui.h, which includes RmlUi/Core.h.
    # Headers only - the RmlUi library itself is not linked, since the only two
    # recompui symbols reached from the render context are stubbed in
    # src/main_oot/ui_stubs_oot.cpp.
    ${REPO_ROOT}/lib/RmlUi/Include
)

# The recompiled RSP microcode uses SSE intrinsics (_mm_shuffle_epi8 and friends
# via librecomp/rsp_vu_impl.hpp), so the baseline ISA has to be raised or the
# always_inline intrinsics fail to inline. Mirrors the main project's settings.
if (MSVC)
    # clang-cl takes MSVC-style flags; pass the clang ones through with /clang:.
    target_compile_options(OoTRecompiled PRIVATE
        /clang:-march=nehalem
        /clang:-fno-strict-aliasing
        -fms-extensions
    )
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(OoTRecompiled PRIVATE
        -march=nehalem
        -fno-strict-aliasing
        -fms-extensions
    )
else()
    target_compile_options(OoTRecompiled PRIVATE
        -fno-strict-aliasing
        -fms-extensions
    )
endif()

if (WIN32)
    # Windows has no system SDL2, so fetch the prebuilt VC development libraries,
    # the same way the main project does.
    include(FetchContent)
    if (DEFINED ENV{SDL2_VERSION})
        set(SDL2_VERSION $ENV{SDL2_VERSION})
    else()
        set(SDL2_VERSION "2.30.3")
    endif()
    FetchContent_Declare(
        sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
    )
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(OoTRecompiled PRIVATE ${sdl2_SOURCE_DIR}/include)
    target_link_directories(OoTRecompiled PRIVATE ${sdl2_SOURCE_DIR}/lib/x64)

    # SDL2 and the DXC runtime have to sit next to the executable.
    add_custom_command(TARGET OoTRecompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${REPO_ROOT}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${REPO_ROOT}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:OoTRecompiled>)

    # Keep a console on all configurations: this launcher is driven from the
    # command line (the rom path is argv[1]) and its diagnostics go to stdout.
    # /DEBUG writes the PDB that Dbghelp needs to turn backtrace addresses into
    # recompiled OoT function names.
    set_target_properties(OoTRecompiled PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE /DEBUG")

    # Dbghelp symbolizes the crash backtrace. It needs a PDB to name anything, so
    # emit debug info even in Release - this build exists to be diagnosed. /Zi and
    # /DEBUG do not affect optimization, only whether symbols are written out.
    target_compile_options(OoTRecompiled PRIVATE /Zi)
    target_link_libraries(OoTRecompiled PRIVATE SDL2 Winmm.lib Dbghelp.lib)
else()
    find_package(SDL2 REQUIRED)
    target_include_directories(OoTRecompiled PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

find_package(Threads REQUIRED)

target_link_libraries(OoTRecompiled PRIVATE
    # ORDER MATTERS. Patches must come first. N64Recomp's single-file-output mode
    # relies on the linker only pulling a symbol from a static library if it is
    # not already defined: listing PatchesLibOoT first makes its Main_ThreadEntry
    # and Overlay_Load win over the unpatched versions in RecompiledFuncsOoT.
    # Reversed, the patches link cleanly but are silently never used, and the
    # first symptom is "Failed to find function" once the game makes an indirect
    # call into a segment that recomp_load_overlays was never told about.
    PatchesLibOoT
    RecompiledFuncsOoT
    librecomp
    ultramodern
    rt64
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

if (NOT WIN32)
    target_link_libraries(OoTRecompiled PRIVATE SDL2::SDL2)
endif()
