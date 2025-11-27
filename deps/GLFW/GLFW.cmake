find_package(glfw3 QUIET)

if (USE_SYSTEM_DEPS AND glfw3_FOUND)
    message(STATUS "Using system GLFW")
    set(GLFW_PKG "")
else()
    message(STATUS "Building GLFW as external project")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_glfw_use_wayland "-DGLFW_USE_WAYLAND=ON")
    else()
        set(_glfw_use_wayland "-DGLFW_USE_WAYLAND=OFF")
    endif()

    elegooslicer_add_cmake_project(GLFW SHARED_LIBS_BOOL TRUE
        URL https://github.com/glfw/glfw/archive/refs/tags/3.3.7.zip
        URL_HASH SHA256=e02d956935e5b9fb4abf90e2c2e07c9a0526d7eacae8ee5353484c69a2a76cd0
        CMAKE_ARGS
            -DGLFW_BUILD_DOCS=OFF
            -DGLFW_BUILD_EXAMPLES=OFF
            -DGLFW_BUILD_TESTS=OFF
            ${_glfw_use_wayland}
    )

    if (MSVC)
        add_debug_dep(dep_GLFW)
    endif ()
    set(GLFW_PKG dep_GLFW)
endif()