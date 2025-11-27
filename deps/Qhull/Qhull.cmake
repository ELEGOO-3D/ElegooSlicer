find_package(Qhull QUIET)

if (USE_SYSTEM_DEPS AND Qhull_FOUND)
    message(STATUS "Using system Qhull")
    set(Qhull_PKG "")
else()
    message(STATUS "Building Qhull as external project")
    include(GNUInstallDirs)
    elegooslicer_add_cmake_project(Qhull SHARED_LIBS_BOOL TRUE
        URL "https://github.com/qhull/qhull/archive/v8.0.1.zip"
        URL_HASH SHA256=5287f5edd6a0372588f5d6640799086a4033d89d19711023ef8229dd9301d69b
        CMAKE_ARGS
            -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
    )

    if (MSVC)
        add_debug_dep(dep_Qhull)
    endif ()
    set(Qhull_PKG dep_Qhull)
endif()