find_package(ZLIB QUIET)

if (USE_SYSTEM_DEPS AND ZLIB_FOUND)
    message(STATUS "Using system ZLIB")
    set(ZLIB_PKG "")
else()
    message(STATUS "Building ZLIB as external project")
    set(patch_command git init && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-Respect-BUILD_SHARED_LIBS.patch)

    elegooslicer_add_cmake_project(ZLIB SHARED_LIBS_BOOL TRUE
      URL https://github.com/madler/zlib/archive/refs/tags/v1.2.13.zip
      URL_HASH SHA256=c2856951bbf30e30861ace3765595d86ba13f2cf01279d901f6c62258c57f4ff
      PATCH_COMMAND ${patch_command}
      CMAKE_ARGS
        -DSKIP_INSTALL_FILES=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )
    set(ZLIB_PKG dep_ZLIB)
endif()