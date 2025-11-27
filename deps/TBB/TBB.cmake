find_package(TBB QUIET)

if (USE_SYSTEM_DEPS AND TBB_FOUND)
    message(STATUS "Using system TBB")
    set(TBB_PKG "")
else()
    message(STATUS "Building TBB as external project")
    if (FLATPAK)
        set(_patch_command ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/GNU.cmake ./cmake/compilers/GNU.cmake)
    else()
        set(_patch_command "")
    endif()

    elegooslicer_add_cmake_project(
        TBB SHARED_LIBS_BOOL TRUE
        URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.5.0.zip"
        URL_HASH SHA256=83ea786c964a384dd72534f9854b419716f412f9d43c0be88d41874763e7bb47
        PATCH_COMMAND ${_patch_command}
        CMAKE_ARGS
            -DTBB_BUILD_TESTS=OFF
            -DTBB_TEST=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_DEBUG_POSTFIX=_debug
    )

    if (MSVC)
        add_debug_dep(dep_TBB)
    endif ()
    set(TBB_PKG dep_TBB)
endif()