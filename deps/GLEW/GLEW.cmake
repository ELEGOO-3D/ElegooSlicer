find_package(GLEW QUIET)

if (USE_SYSTEM_DEPS AND GLEW_FOUND)
    message(STATUS "Using system GLEW")
    set(GLEW_PKG "")
else()
    message(STATUS "Building GLEW as external project")
    # We have to check for OpenGL to compile GLEW
    set(OpenGL_GL_PREFERENCE "LEGACY") # to prevent a nasty warning by cmake
    find_package(OpenGL QUIET REQUIRED)

    elegooslicer_add_cmake_project(
      GLEW SHARED_LIBS_BOOL TRUE
      SOURCE_DIR  ${CMAKE_CURRENT_LIST_DIR}/glew
    )

    if (MSVC)
        add_debug_dep(dep_GLEW)
    endif ()
    set(GLEW_PKG dep_GLEW)
endif()