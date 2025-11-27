message(STATUS "OpenCSG not found in system, building as external project.")
elegooslicer_add_cmake_project(OpenCSG SHARED_LIBS_BOOL TRUE
    URL https://github.com/floriankirsch/OpenCSG/archive/refs/tags/opencsg-1-4-2-release.zip
    URL_HASH SHA256=51afe0db79af8386e2027d56d685177135581e0ee82ade9d7f2caff8deab5ec5
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in ./CMakeLists.txt
    DEPENDS dep_GLEW
)

if (TARGET ${ZLIB_PKG})
    add_dependencies(dep_OpenCSG ${ZLIB_PKG})
endif()

if (MSVC)
    add_debug_dep(dep_OpenCSG)
endif ()
set(OpenCSG_PKG dep_OpenCSG)