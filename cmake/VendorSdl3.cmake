# Vendored SDL 3.4.18. See third_party/sdl3/PROVENANCE.md.
# Options are forced so a preset cannot turn SDL back into a download
# or a shared library. SDL's own warnings are not promoted to errors.

function(lykoware_add_sdl3)
    set(SDL_SHARED OFF CACHE BOOL "Lykoware vendors SDL as a static library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Lykoware vendors SDL as a static library" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build SDL's test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Do not build SDL's tests" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Do not build SDL's examples" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Do not install SDL with Lykoware" FORCE)
    set(SDL_INSTALL_CPACK OFF CACHE BOOL "Do not package SDL" FORCE)
    set(SDL_UNINSTALL OFF CACHE BOOL "Do not add an SDL uninstall target" FORCE)
    set(SDL_AUDIO OFF CACHE BOOL "Window and input do not use SDL audio" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "Window and input do not use SDL camera" FORCE)
    set(SDL_GPU OFF CACHE BOOL "The Vulkan RHI is not SDL's GPU API" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "Window and input do not use SDL's renderer" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "Window and input do not use SDL haptic" FORCE)
    set(SDL_OPENGL OFF CACHE BOOL "OpenGL is not the proposed graphics API" FORCE)
    set(SDL_OPENGLES OFF CACHE BOOL "OpenGL ES is not the proposed graphics API" FORCE)
    set(SDL_DUMMYVIDEO ON CACHE BOOL "Headless tests use the dummy video driver" FORCE)
    set(SDL_OFFSCREEN ON CACHE BOOL "Keep the offscreen video driver available" FORCE)
    set(SDL_VULKAN ON CACHE BOOL "Compile SDL Vulkan surface helpers without linking Vulkan" FORCE)
    # A machine without X11/Wayland headers still configures. The dummy driver
    # is enough for tests. Optional X11 extras are off so a partial X11 install
    # cannot fail the configure.
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "Allow configure without a desktop video driver" FORCE)
    set(SDL_X11_XCURSOR OFF CACHE BOOL "Skip optional Xcursor" FORCE)
    set(SDL_X11_XDBE OFF CACHE BOOL "Skip optional Xdbe" FORCE)
    set(SDL_X11_XINPUT OFF CACHE BOOL "Skip optional XInput" FORCE)
    set(SDL_X11_XFIXES OFF CACHE BOOL "Skip optional Xfixes" FORCE)
    set(SDL_X11_XRANDR OFF CACHE BOOL "Skip optional Xrandr" FORCE)
    set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "Skip optional Xscrnsaver" FORCE)
    set(SDL_X11_XSHAPE OFF CACHE BOOL "Skip optional Xshape" FORCE)
    set(SDL_X11_XSYNC OFF CACHE BOOL "Skip optional Xsync" FORCE)
    set(SDL_X11_XTEST OFF CACHE BOOL "Skip optional XTest" FORCE)
    set(SDL_FRIBIDI OFF CACHE BOOL "Skip optional fribidi" FORCE)
    set(SDL_LIBTHAI OFF CACHE BOOL "Skip optional libthai" FORCE)

    add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/sdl3" EXCLUDE_FROM_ALL)

    if(NOT TARGET SDL3-static)
        message(FATAL_ERROR "Vendored SDL3 did not produce the static library target")
    endif()

    get_target_property(_lykoware_sdl_includes SDL3-static INTERFACE_INCLUDE_DIRECTORIES)
    if(_lykoware_sdl_includes)
        set_property(TARGET SDL3-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES "")
        set_property(TARGET SDL3-static PROPERTY
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_lykoware_sdl_includes}")
    endif()

    if(LYKOWARE_SANITIZERS)
        target_compile_options(SDL3-static PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(SDL3-static PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
