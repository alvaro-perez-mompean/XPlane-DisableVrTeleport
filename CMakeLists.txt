# DisableVrTeleport -- native X-Plane plugin
#
# Builds the full VR-teleport toggle plugin (see PLAN.md for the design
# history): hold-to-toggle gesture, disablevrteleport/toggle command, and
# Plugins-menu item, all driving sim/joystick/joystick_axis_assignments. No
# ImGui/window, no core/ pure-logic split -- this plugin is a single thin
# SDK-glue behavior.

cmake_minimum_required(VERSION 3.16)

project(DisableVrTeleport VERSION 0.1.0 DESCRIPTION "Disable VR Teleport")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/SDK")

include_directories(
    "${SDK_DIR}/CHeaders/XPLM"
    "${SDK_DIR}/CHeaders/Wrappers"
)

if(WIN32)
    list(APPEND CMAKE_LIBRARY_PATH "${SDK_DIR}/Libraries/Win")
endif()

# Enable X-Plane SDK APIs through the version this SDK download (4.3.0) provides.
add_definitions(
    -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM302=1 -DXPLM303=1
    -DXPLM400=1 -DXPLM410=1 -DXPLM420=1 -DXPLM430=1
)
add_definitions(-DAPL=$<BOOL:${APPLE}> -DIBM=$<BOOL:${WIN32}> -DLIN=$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>)

if(MSVC)
    add_compile_options(/wd4996 /wd4068)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -fexceptions -fvisibility=hidden)
endif()

add_library(DisableVrTeleport SHARED
    src/DisableVrTeleport.cpp
)

if(WIN32)
    find_library(XPLM_LIBRARY NAMES XPLM XPLM_64.lib)
    target_link_libraries(DisableVrTeleport ${XPLM_LIBRARY})
endif()

if(UNIX AND NOT APPLE)
    find_library(DL_LIBRARY dl)
    target_link_libraries(DisableVrTeleport ${DL_LIBRARY})
endif()

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
    # The vendored XPLM.framework lives under third_party/SDK, not any default
    # framework search path, so it needs an explicit -F to be found by -framework.
    target_link_libraries(DisableVrTeleport "-F${SDK_DIR}/Libraries/Mac" "-framework XPLM" ${CORE_FOUNDATION_LIBRARY})
endif()

if(WIN32)
    if(MINGW)
        target_link_libraries(DisableVrTeleport -static-libgcc -static-libstdc++ -static)
    endif()
    # Multi-config generators (Visual Studio) append a per-config subfolder
    # to *_OUTPUT_DIRECTORY unless the per-config property is also set --
    # pin every config to the same win_x64/ folder X-Plane expects.
    foreach(OUTDIR_PROP RUNTIME_OUTPUT_DIRECTORY LIBRARY_OUTPUT_DIRECTORY)
        set_target_properties(DisableVrTeleport PROPERTIES ${OUTDIR_PROP} "${CMAKE_BINARY_DIR}/win_x64")
        foreach(CFG Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER ${CFG} CFG_UPPER)
            set_target_properties(DisableVrTeleport PROPERTIES ${OUTDIR_PROP}_${CFG_UPPER} "${CMAKE_BINARY_DIR}/win_x64")
        endforeach()
    endforeach()
elseif(APPLE)
    set_target_properties(DisableVrTeleport PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mac_x64")
elseif(UNIX)
    set_target_properties(DisableVrTeleport PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lin_x64")
endif()

set_target_properties(DisableVrTeleport PROPERTIES
    PREFIX ""
    OUTPUT_NAME "DisableVrTeleport"
    SUFFIX ".xpl"
)
