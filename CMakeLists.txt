cmake_minimum_required(VERSION 3.20)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
endif()

project(Zelda64Recompiled LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()

# Opt out of constexpr mutex constructor on windows to prevent vcredist issues
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")

    # SDL_endian.h carries a workaround, written for Clang 11, that defines
    # _m_prefetch itself under _MSC_VER and guards it with __PRFCHWINTRIN_H.
    # Recent Clang makes _m_prefetch a real builtin, so that definition is now a
    # hard error ("definition of builtin function"), not a warning we could demote.
    # Predefining the guard makes SDL skip its version; the builtin stays callable
    # because Clang implicitly declares builtins without the header. This hits both
    # SDL copies in the build - rt64's bundled one and the fetched 2.30.3 - so it
    # has to be global rather than scoped to one target.
    add_compile_definitions(__PRFCHWINTRIN_H)
endif()

if (APPLE)
    enable_language(OBJC OBJCXX)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    option(RECOMP_FLATPAK "Configure the build for Flatpak compatibility." OFF)
endif()

# Avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24:
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (WIN32)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/lib/")
endif()

set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(HLSL_CPU)

if (RECOMP_FLATPAK)
    add_compile_definitions(RECOMP_FLATPAK)
endif()

# Disable Apple Retina display support in plume (Zelda doesn't support retina yet)
if (APPLE)
    set(APPLE_RETINA_ENABLED OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)

# set(BUILD_SHARED_LIBS_SAVED "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
SET(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/lunasvg)
# set(BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS_SAVED}")
SET(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)
SET(RMLUI_TESTS_ENABLED OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/RmlUi)
target_compile_definitions(rmlui_core PRIVATE LUNASVG_BUILD_STATIC)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)

target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)

# RecompiledFuncs - Library containing the primary recompiler output
add_library(RecompiledFuncs STATIC)

target_compile_options(RecompiledFuncs PRIVATE
    # -Wno-unused-but-set-variable
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-implicit-function-declaration
)

target_include_directories(RecompiledFuncs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

file(GLOB FUNC_C_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.cpp)

target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

# PatchesLib - Library containing the recompiled output for any custom function patches
add_library(PatchesLib STATIC)

target_compile_options(PatchesLib PRIVATE
    # -Wno-unused-but-set-variable
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-implicit-function-declaration
)

target_include_directories(PatchesLib PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

target_sources(PatchesLib PRIVATE
    ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c
    ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c PROPERTIES COMPILE_FLAGS -fno-strict-aliasing)

# Build patches elf
if(NOT DEFINED PATCHES_C_COMPILER)
    set(PATCHES_C_COMPILER clang)
endif()

if(NOT DEFINED PATCHES_LD)
    set(PATCHES_LD ld.lld)
endif()

# The patch payloads are built by a Makefile with a mips clang toolchain, which a
# Windows checkout generally does not have. Without make the step is skipped and
# the already generated output in RecompiledPatches/ is used as-is.
find_program(PATCHES_MAKE make)

# Likewise for the recompiler itself. The name is spelled out per platform rather
# than looked up with find_program: on Windows that happily matches the
# extensionless Linux N64Recomp sitting at the repo root, which then fails as
# "not recognized as an internal or external command" only once ninja runs it.
if (WIN32)
    set(N64RECOMP_NAME N64Recomp.exe)
else()
    set(N64RECOMP_NAME N64Recomp)
endif()

if (EXISTS ${CMAKE_SOURCE_DIR}/${N64RECOMP_NAME})
    set(N64RECOMP_EXECUTABLE ${CMAKE_SOURCE_DIR}/${N64RECOMP_NAME})
else()
    set(N64RECOMP_EXECUTABLE "")
    message(STATUS "${N64RECOMP_NAME} not found at the repo root: using the existing recompiled patch output")
endif()

if (PATCHES_MAKE)
    add_custom_target(PatchesBin
        COMMAND ${CMAKE_COMMAND} -E env CC=${PATCHES_C_COMPILER} LD=${PATCHES_LD} ${PATCHES_MAKE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/patches
        BYPRODUCTS ${CMAKE_SOURCE_DIR}/patches/patches.elf
    )
else()
    message(STATUS "make not found: using the existing patch binaries instead of rebuilding them")
endif()

# Generate patches_bin.c from patches.bin
add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c
    COMMAND file_to_c ${CMAKE_SOURCE_DIR}/patches/patches.bin mm_patches_bin ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.h
    DEPENDS ${CMAKE_SOURCE_DIR}/patches/patches.bin
)

# Recompile patches elf into patches.c and patches.bin
if (N64RECOMP_EXECUTABLE)
    add_custom_command(OUTPUT
                           ${CMAKE_SOURCE_DIR}/patches/patches.bin
                           ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c
                           ${CMAKE_SOURCE_DIR}/RecompiledPatches/recomp_overlays.inl
                           ${CMAKE_SOURCE_DIR}/RecompiledPatches/funcs.h
                       # TODO: Look into why modifying patches requires two builds to take
                       COMMAND ${N64RECOMP_EXECUTABLE} patches.toml
                       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                       DEPENDS ${CMAKE_SOURCE_DIR}/patches/patches.elf
    )
endif()

# ---------------------------------------------------------------------------
# Ocarina of Time
#
# Linked into the same executable as Majora's Mask. Every recompiled OoT symbol
# carries an `oot_` prefix (func_prefix in oot.us.ntsc-1.0.toml), so the two
# games' shared decomp function names no longer collide at link time.
#
# Defaults to on when the recompiler output is present. A checkout without it
# still configures, it just builds the MM-only launcher.
if (EXISTS ${CMAKE_SOURCE_DIR}/RecompiledFuncsOoT/funcs.h)
    option(RECOMP_OOT "Build Ocarina of Time support into the launcher" ON)
else()
    option(RECOMP_OOT "Build Ocarina of Time support into the launcher" OFF)
endif()

if (RECOMP_OOT)
    # Primary recompiler output for OoT.
    add_library(RecompiledFuncsOoT STATIC)

    target_compile_options(RecompiledFuncsOoT PRIVATE
        -fno-strict-aliasing
        -Wno-unused-variable
        -Wno-implicit-function-declaration
    )

    target_include_directories(RecompiledFuncsOoT PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    )

    file(GLOB OOT_FUNC_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncsOoT/*.c)
    target_sources(RecompiledFuncsOoT PRIVATE ${OOT_FUNC_SOURCES})

    # Recompiled output for the OoT patches.
    add_library(PatchesLibOoT STATIC)

    target_compile_options(PatchesLibOoT PRIVATE
        -fno-strict-aliasing
        -Wno-unused-variable
        -Wno-implicit-function-declaration
    )

    target_include_directories(PatchesLibOoT PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    )

    target_sources(PatchesLibOoT PRIVATE
        ${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/patches.c
        ${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/patches_bin.c
    )

    set_source_files_properties(${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/patches.c PROPERTIES COMPILE_FLAGS -fno-strict-aliasing)

    # Build the OoT patches elf, mirroring the MM steps above, and skipped without
    # make for the same reason.
    #
    # Also skipped without the decomp. Unlike Majora's Mask, whose lib/mm-decomp is
    # a submodule and always present, the OoT patches need a decomp that has been
    # *built* - they include generated headers from its build/ and extracted/ dirs
    # - so it cannot simply be vendored. See OOT_DIR in patches_oot/Makefile.
    set(OOT_DECOMP_DIR ${CMAKE_SOURCE_DIR}/lib/oot-decomp)
    if (PATCHES_MAKE AND EXISTS ${OOT_DECOMP_DIR}/build/ntsc-1.0)
        add_custom_target(PatchesBinOoT
            COMMAND ${CMAKE_COMMAND} -E env CC=${PATCHES_C_COMPILER} LD=${PATCHES_LD} ${PATCHES_MAKE}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/patches_oot
            BYPRODUCTS ${CMAKE_SOURCE_DIR}/patches_oot/patches.elf
        )
    else()
        message(STATUS "OoT decomp not built at ${OOT_DECOMP_DIR}: using the existing patches_oot output")
    endif()

    add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/patches_bin.c
        COMMAND file_to_c ${CMAKE_SOURCE_DIR}/patches_oot/patches.bin oot_patches_bin ${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/patches_bin.c ${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/patches_bin.h
        DEPENDS ${CMAKE_SOURCE_DIR}/patches_oot/patches.bin
    )

    if (N64RECOMP_EXECUTABLE)
        add_custom_command(OUTPUT
                               ${CMAKE_SOURCE_DIR}/patches_oot/patches.bin
                               ${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/patches.c
                               ${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/recomp_overlays.inl
                               ${CMAKE_SOURCE_DIR}/RecompiledPatchesOoT/funcs.h
                           COMMAND ${N64RECOMP_EXECUTABLE} patches_oot.toml
                           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                           DEPENDS ${CMAKE_SOURCE_DIR}/patches_oot/patches.elf
        )
    endif()
endif()

# Main executable
add_executable(Zelda64Recompiled)

set (SOURCES
    ${CMAKE_SOURCE_DIR}/src/main/main.cpp
    ${CMAKE_SOURCE_DIR}/src/main/support.cpp
    ${CMAKE_SOURCE_DIR}/src/main/register_overlays.cpp
    ${CMAKE_SOURCE_DIR}/src/main/register_patches.cpp
    ${CMAKE_SOURCE_DIR}/src/main/rt64_render_context.cpp

    ${CMAKE_SOURCE_DIR}/src/game/input.cpp
    ${CMAKE_SOURCE_DIR}/src/game/controls.cpp
    ${CMAKE_SOURCE_DIR}/src/game/config.cpp
    ${CMAKE_SOURCE_DIR}/src/game/scene_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/scene_table_oot.cpp
    ${CMAKE_SOURCE_DIR}/src/game/debug.cpp
    ${CMAKE_SOURCE_DIR}/src/game/quicksaving.cpp
    ${CMAKE_SOURCE_DIR}/src/game/recomp_api.cpp
    ${CMAKE_SOURCE_DIR}/src/game/recomp_actor_api.cpp
    ${CMAKE_SOURCE_DIR}/src/game/recomp_data_api.cpp
    ${CMAKE_SOURCE_DIR}/src/game/rom_decompression.cpp

    ${CMAKE_SOURCE_DIR}/src/ui/ui_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_state.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_launcher.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_config.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_prompt.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_config_sub_menu.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_color_hack.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_rml_hacks.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_elements.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_mod_details_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_mod_installer.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_mod_menu.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_api.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_api_events.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_api_images.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/util/hsv.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/core/ui_context.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_button.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_clickable.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_container.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_element.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_image.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_label.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_radio.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_scroll_container.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_slider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_span.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_style.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_text_input.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_toggle.cpp

    ${CMAKE_SOURCE_DIR}/rsp/aspMain.cpp
    ${CMAKE_SOURCE_DIR}/rsp/njpgdspMain.cpp

    ${CMAKE_SOURCE_DIR}/lib/RmlUi/Backends/RmlUi_Platform_SDL.cpp
)

if (APPLE)
    list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/main/support_apple.mm)
endif()

if (RECOMP_OOT)
    list(APPEND SOURCES
        ${CMAKE_SOURCE_DIR}/src/main_oot/register_overlays_oot.cpp
        ${CMAKE_SOURCE_DIR}/src/main_oot/register_patches_oot.cpp
        # The libultra functions OoT calls that MM never does, plus the patch
        # trace hook. recomp_load_overlays is deliberately not here: MM's
        # src/game/recomp_api.cpp already defines it.
        ${CMAKE_SOURCE_DIR}/src/main_oot/runtime_funcs_oot.cpp

        ${CMAKE_SOURCE_DIR}/rsp_oot/aspMain.cpp
        ${CMAKE_SOURCE_DIR}/rsp_oot/njpgdspMain.cpp
    )
endif()

target_include_directories(Zelda64Recompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    ${CMAKE_SOURCE_DIR}/lib/concurrentqueue
    ${CMAKE_SOURCE_DIR}/lib/GamepadMotionHelpers
    ${CMAKE_SOURCE_DIR}/lib/RmlUi/Include
    ${CMAKE_SOURCE_DIR}/lib/RmlUi/Backends
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/inc
    ${CMAKE_SOURCE_DIR}/lib/rt64/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/rhi
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/render
    ${CMAKE_SOURCE_DIR}/lib/freetype-windows-binaries/include
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/nativefiledialog-extended/src/include
    ${CMAKE_SOURCE_DIR}/lib/SlotMap
    ${CMAKE_BINARY_DIR}/shaders
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(Zelda64Recompiled PRIVATE
        -march=nehalem
        -fno-strict-aliasing
        -fms-extensions
    )
else()
    target_compile_options(Zelda64Recompiled PRIVATE
        -fno-strict-aliasing
        -fms-extensions
    )
endif()

if (MSVC)
    # Disable identical code folding, since this breaks mod function patching as multiple functions can get merged into one.
    target_link_options(Zelda64Recompiled PRIVATE /OPT:NOICF)
elseif (APPLE)
    # Use a wrapper around ld64 that respects segprot's `max_prot` value in order
    # to make our executable memory writable (required for mod function patching)
    target_link_options(Zelda64Recompiled PRIVATE
        "-fuse-ld=${CMAKE_SOURCE_DIR}/.github/macos/ld64"
    )
endif()

if (WIN32)
    include(FetchContent)

    if (DEFINED ENV{SDL2_VERSION})
        set(SDL2_VERSION $ENV{SDL2_VERSION})
    else()
        set(SDL2_VERSION "2.30.3")
    endif()

    # Fetch SDL2 on windows
    FetchContent_Declare(
        sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
    )
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(Zelda64Recompiled PRIVATE
        ${sdl2_SOURCE_DIR}/include
    )
    target_link_directories(Zelda64Recompiled PRIVATE
        ${sdl2_SOURCE_DIR}/lib/x64
    )

    # Copy SDL2 and dxc DLLs to output folder as post build step
    add_custom_command(TARGET Zelda64Recompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:Zelda64Recompiled>)

    # The UI loads its documents and fonts through get_asset_path, which is
    # relative to the *working directory* on Windows (get_program_path() returns
    # an empty path there), and the controller database is read the same way. An
    # exe started from the build directory - which is what double-clicking it does
    # - therefore finds no assets and dies during UI init with no window and no
    # message. Copying both next to the exe makes it run from either place.
    add_custom_command(TARGET Zelda64Recompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:Zelda64Recompiled>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/recompcontrollerdb.txt"
            $<TARGET_FILE_DIR:Zelda64Recompiled>)

    # A Release build normally links /SUBSYSTEM:WINDOWS, which discards stdout and
    # stderr entirely - a startup failure shows as a black window and nothing else.
    # RECOMP_CONSOLE keeps the console attached in Release so those diagnostics are
    # readable. It only changes where output goes, not optimization; /DEBUG writes
    # the PDB that turns a backtrace into recompiled function names.
    option(RECOMP_CONSOLE "Keep a console attached in Release builds for diagnostics" OFF)

    if (RECOMP_CONSOLE)
        set_target_properties(
            Zelda64Recompiled
            PROPERTIES
                LINK_FLAGS "/SUBSYSTEM:CONSOLE /DEBUG"
        )
        target_compile_options(Zelda64Recompiled PRIVATE /Zi)
        # Symbolized backtrace on a fatal fault. Without it a fault in recompiled
        # code kills the process silently, since it never reaches ultramodern's
        # reporting paths. Dbghelp needs the PDB that /DEBUG writes above.
        target_sources(Zelda64Recompiled PRIVATE ${CMAKE_SOURCE_DIR}/src/main_oot/crash_handler_oot.cpp)
        target_compile_definitions(Zelda64Recompiled PRIVATE RECOMP_CRASH_HANDLER)
        target_link_libraries(Zelda64Recompiled PRIVATE Dbghelp.lib)
    else()
        set_target_properties(
            Zelda64Recompiled
            PROPERTIES
                LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
                LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
                LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
                LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
        )
    endif()

    target_sources(Zelda64Recompiled PRIVATE ${CMAKE_SOURCE_DIR}/icons/app.rc)
    target_link_libraries(Zelda64Recompiled PRIVATE SDL2 Winmm.lib)
endif()

if (APPLE)
    find_package(SDL2 REQUIRED)
    target_include_directories(Zelda64Recompiled PRIVATE ${SDL2_INCLUDE_DIRS})

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)

    target_link_libraries(Zelda64Recompiled PRIVATE ${CMAKE_DL_LIBS} Threads::Threads SDL2::SDL2)

    include(${CMAKE_SOURCE_DIR}/.github/macos/apple_bundle.cmake)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED)
    add_compile_definitions("RT64_SDL_WINDOW_VULKAN")

    # Generate icon_bytes.c from the app icon PNG.
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
        COMMAND file_to_c ${CMAKE_SOURCE_DIR}/icons/512.png icon_bytes ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
        DEPENDS ${CMAKE_SOURCE_DIR}/icons/512.png
    )
    target_sources(Zelda64Recompiled PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c)

    message(STATUS "SDL2_FOUND = ${SDL2_FOUND}")
    message(STATUS "SDL2_INCLUDE_DIRS = ${SDL2_INCLUDE_DIRS}")

    target_include_directories(Zelda64Recompiled PRIVATE ${SDL2_INCLUDE_DIRS})

    find_package(Freetype REQUIRED)

    message(STATUS "FREETYPE_FOUND = ${FREETYPE_FOUND}")
    message(STATUS "FREETYPE_INCLUDE_DIRS = ${FREETYPE_INCLUDE_DIRS}")
    message(STATUS "FREETYPE_LIBRARIES = ${FREETYPE_LIBRARIES}")

    include_directories(${FREETYPE_LIBRARIES})
    target_link_libraries(Zelda64Recompiled PRIVATE ${FREETYPE_LIBRARIES} SDL2::SDL2)

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)

    target_link_libraries(Zelda64Recompiled PRIVATE "-latomic -static-libstdc++" ${CMAKE_DL_LIBS} Threads::Threads)
endif()

target_link_libraries(Zelda64Recompiled PRIVATE
    PatchesLib
    RecompiledFuncs
    librecomp
    ultramodern
    rt64
    RmlUi::Core
    RmlUi::Debugger
    nfd
    lunasvg
)

if (RECOMP_OOT)
    target_compile_definitions(Zelda64Recompiled PRIVATE RECOMP_OOT)

    # ORDER MATTERS, for the same reason it does for the MM pair above.
    # N64Recomp's single-file-output mode relies on the linker only pulling a
    # symbol from a static library if it is not already defined, so the patches
    # have to be listed before the recompiled functions they override. Reversed,
    # the patches link cleanly but are silently never used.
    target_link_libraries(Zelda64Recompiled PRIVATE
        PatchesLibOoT
        RecompiledFuncsOoT
    )
endif()

# TODO fix the rt64 CMake script so that this doesn't need to be duplicated here
# For DXC
set (DXC_COMMON_OPTS "-I${PROJECT_SOURCE_DIR}/src")
set (DXC_DXIL_OPTS "-Wno-ignored-attributes")
set (DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set (DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_0" "-D DYNAMIC_RENDER_PARAMS")
set (DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_0" "-D DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
set (DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T cs_6_0")
set (DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T gs_6_0")
set (DXC_RT_OPTS "${DXC_COMMON_OPTS}" "-D" "RT_SHADER" "-T" "lib_6_3" "-fspv-target-env=vulkan1.1spirv1.4" "-fspv-extension=SPV_KHR_ray_tracing" "-fspv-extension=SPV_EXT_descriptor_indexing")

if (${WIN32})
    set (DXC "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe")
    add_compile_definitions(NOMINMAX)
else()
    if (APPLE)
        # Apple's binary is universal, so it'll work on both x86_64 and arm64
        set (DXC "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/arm64/dxc-macos")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(SPIRVCROSS "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/spirv-cross/lib/x64" "${PROJECT_SOURCE_DIR}/lib/rt64//src/contrib/spirv-cross/bin/x64/spirv-cross")
        else()
            set(SPIRVCROSS "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/spirv-cross/lib/arm64" "${PROJECT_SOURCE_DIR}/lib/rt64//src/contrib/spirv-cross/bin/x64/spirv-cross")
        endif()
    else()
        if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set (DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/x64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc-linux")
        else()
            set (DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/arm64/dxc-linux")
        endif()
    endif()
endif()

build_vertex_shader(Zelda64Recompiled "shaders/InterfaceVS.hlsl" "shaders/InterfaceVS.hlsl")
build_pixel_shader(Zelda64Recompiled "shaders/InterfacePS.hlsl" "shaders/InterfacePS.hlsl")

# Embed all .nrm files in the "mods" directory
file(GLOB NRM_FILES "${CMAKE_SOURCE_DIR}/mods/*.nrm")

set(GENERATED_NRM_SOURCES "")

foreach(NRM_FILE ${NRM_FILES})
    get_filename_component(NRM_NAME ${NRM_FILE} NAME_WE)
    set(OUT_C "${CMAKE_CURRENT_BINARY_DIR}/mods/${NRM_NAME}.c")
    set(OUT_H "${CMAKE_CURRENT_BINARY_DIR}/mods/${NRM_NAME}.h")

    add_custom_command(
        OUTPUT ${OUT_C} ${OUT_H}
        COMMAND file_to_c ${NRM_FILE} ${NRM_NAME} ${OUT_C} ${OUT_H}
        DEPENDS ${NRM_FILE}
    )

    list(APPEND GENERATED_NRM_SOURCES ${OUT_C})
endforeach()

target_sources(Zelda64Recompiled PRIVATE ${GENERATED_NRM_SOURCES})

target_sources(Zelda64Recompiled PRIVATE ${SOURCES})

set_property(TARGET Zelda64Recompiled PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
