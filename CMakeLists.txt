cmake_minimum_required(VERSION 3.20)
# C is needed for the wayland-scanner generated protocol glue.
project(vkbd VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(VKBD_WAYLAND "Build the Wayland input-method (KWin) integration" ON)
option(VKBD_INPUT_PANEL "Build the zwp_input_panel_v1 surface role (needs Qt WaylandClient private headers)" ON)
option(VKBD_LAYER_SHELL "Build LayerShellQt support for standalone Wayland use" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(PkgConfig)

add_executable(vkbd
    src/main.cpp
    src/layout.cpp
    src/keymap.cpp
    src/keyboardwidget.cpp
    src/togglebutton.cpp
    src/injector.cpp
    src/uinputinjector.cpp
    src/controller.cpp
    src/doctor.cpp
)
target_include_directories(vkbd PRIVATE src)
target_link_libraries(vkbd PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus)
target_compile_definitions(vkbd PRIVATE VKBD_VERSION="${PROJECT_VERSION}")

# ---- xkbcommon: key labels that follow the real keyboard layout ----------------
if(PkgConfig_FOUND)
    pkg_check_modules(XKBCOMMON IMPORTED_TARGET xkbcommon)
endif()
if(XKBCOMMON_FOUND)
    target_link_libraries(vkbd PRIVATE PkgConfig::XKBCOMMON)
    target_compile_definitions(vkbd PRIVATE VKBD_HAVE_XKBCOMMON=1)
    message(STATUS "vkbd: xkbcommon found, layout-aware key labels enabled")
else()
    message(WARNING "vkbd: xkbcommon not found, key labels will always be US-English")
endif()

# ---- Wayland: input-method-v1 (KWin launches us and hands us a context) --------
set(VKBD_WAYLAND_ENABLED OFF)
if(VKBD_WAYLAND)
    find_package(Qt6 COMPONENTS WaylandClient)
    if(PkgConfig_FOUND)
        pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
    endif()
    if(Qt6WaylandClient_FOUND AND WAYLAND_CLIENT_FOUND)
        set(VKBD_WAYLAND_ENABLED ON)
        set(_vkbd_scanner_opts "")
        if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
            set(_vkbd_scanner_opts PRIVATE_CODE)
        endif()
        # NO_INCLUDE_CORE_ONLY: the protocol references wl_keyboard/wl_surface/wl_output,
        # whose interface symbols only come with the full wayland-client.h.
        qt6_generate_wayland_protocol_client_sources(vkbd
            ${_vkbd_scanner_opts}
            NO_INCLUDE_CORE_ONLY
            FILES ${CMAKE_CURRENT_SOURCE_DIR}/protocols/input-method-unstable-v1.xml
                  ${CMAKE_CURRENT_SOURCE_DIR}/protocols/slide.xml
                  ${CMAKE_CURRENT_SOURCE_DIR}/protocols/plasma-window-management.xml
        )
        target_sources(vkbd PRIVATE src/wayland/inputmethod.cpp src/wayland/windowtracker.cpp)
        target_link_libraries(vkbd PRIVATE Qt6::WaylandClient PkgConfig::WAYLAND_CLIENT)
        target_compile_definitions(vkbd PRIVATE VKBD_HAVE_WAYLAND=1)
        message(STATUS "vkbd: Wayland input-method-v1 integration enabled")
    else()
        message(WARNING "vkbd: Qt6 WaylandClient or wayland-client not found; building without KWin input-method integration")
    endif()
endif()

# ---- zwp_input_panel_v1 role: KWin treats the window as the on-screen keyboard --
if(VKBD_WAYLAND_ENABLED AND VKBD_INPUT_PANEL)
    find_package(Qt6 QUIET COMPONENTS WaylandClientPrivate GuiPrivate)
    # Qt exposes the private include dirs behind generator expressions, which
    # find_file cannot evaluate; derive the plain paths instead.
    set(_vkbd_priv_dirs ${Qt6WaylandClient_PRIVATE_INCLUDE_DIRS})
    foreach(_d IN LISTS Qt6WaylandClient_INCLUDE_DIRS)
        list(APPEND _vkbd_priv_dirs "${_d}/${Qt6WaylandClient_VERSION}")
    endforeach()
    if(TARGET Qt6::WaylandClientPrivate)
        get_target_property(_vkbd_target_incs Qt6::WaylandClientPrivate INTERFACE_INCLUDE_DIRECTORIES)
        if(_vkbd_target_incs)
            string(REGEX MATCHALL "/[^;>$<]+" _vkbd_plain_incs "${_vkbd_target_incs}")
            list(APPEND _vkbd_priv_dirs ${_vkbd_plain_incs})
        endif()
    endif()
    list(REMOVE_DUPLICATES _vkbd_priv_dirs)
    find_file(_vkbd_panel_hdr
        NAMES QtWaylandClient/private/qwaylandshellintegration_p.h
        PATHS ${_vkbd_priv_dirs}
        NO_DEFAULT_PATH
    )
    if(_vkbd_panel_hdr)
        target_sources(vkbd PRIVATE src/wayland/inputpanelshell.cpp src/wayland/slide.cpp)
        if(TARGET Qt6::WaylandClientPrivate)
            target_link_libraries(vkbd PRIVATE Qt6::WaylandClientPrivate)
        else()
            target_include_directories(vkbd PRIVATE ${_vkbd_priv_dirs})
        endif()
        if(TARGET Qt6::GuiPrivate)
            target_link_libraries(vkbd PRIVATE Qt6::GuiPrivate)
        else()
            target_include_directories(vkbd PRIVATE ${Qt6Gui_PRIVATE_INCLUDE_DIRS})
        endif()
        target_compile_definitions(vkbd PRIVATE VKBD_HAVE_INPUT_PANEL=1 VKBD_HAVE_SLIDE=1)
        message(STATUS "vkbd: input-panel surface role enabled (Qt private API, ${_vkbd_panel_hdr})")
    else()
        message(WARNING "vkbd: Qt WaylandClient private headers not found (install qt6-wayland private/devel package); "
                        "falling back to layer-shell for the panel window")
    endif()
endif()

# ---- LayerShellQt: focus-less bottom panel when not launched by KWin -----------
if(VKBD_WAYLAND_ENABLED AND VKBD_LAYER_SHELL)
    find_package(LayerShellQt)
    if(LayerShellQt_FOUND)
        target_link_libraries(vkbd PRIVATE LayerShellQt::Interface)
        target_compile_definitions(vkbd PRIVATE VKBD_HAVE_LAYER_SHELL=1)
        message(STATUS "vkbd: LayerShellQt found")
    else()
        message(WARNING "vkbd: LayerShellQt not found; standalone Wayland mode will use a plain window")
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS vkbd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/vkbd.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/vkbd-toggle.desktop data/vkbd-copy.desktop data/vkbd-paste.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
