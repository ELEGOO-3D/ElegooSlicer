find_package(Blosc QUIET)

if (USE_SYSTEM_DEPS AND Blosc_FOUND)
    message(STATUS "Using system Blosc")
    set(Blosc_PKG "")
else()
    message(STATUS "Building Blosc as external project")
    if(IS_CROSS_COMPILE AND APPLE)
        elegooslicer_add_cmake_project(Blosc SHARED_LIBS_BOOL TRUE
            URL https://github.com/tamasmeszaros/c-blosc/archive/refs/heads/v1.17.0_tm.zip
            URL_HASH SHA256=dcb48bf43a672fa3de6a4b1de2c4c238709dad5893d1e097b8374ad84b1fc3b3
            DEPENDS ${ZLIB_PKG}
            CMAKE_ARGS
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_TESTS=OFF
                -DBUILD_BENCHMARKS=OFF
                -DPREFER_EXTERNAL_ZLIB=ON
                -DDEACTIVATE_SSE2=ON
                -DDEACTIVATE_AVX2=ON
        )
    else()
        elegooslicer_add_cmake_project(Blosc SHARED_LIBS_BOOL TRUE
            URL https://github.com/tamasmeszaros/c-blosc/archive/refs/heads/v1.17.0_tm.zip
            URL_HASH SHA256=dcb48bf43a672fa3de6a4b1de2c4c238709dad5893d1e097b8374ad84b1fc3b3
            DEPENDS ${ZLIB_PKG}
            CMAKE_ARGS
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_TESTS=OFF
                -DBUILD_BENCHMARKS=OFF
                -DPREFER_EXTERNAL_ZLIB=ON
        )
    endif()
    if (MSVC)
        add_debug_dep(dep_Blosc)
    endif ()
    set(Blosc_PKG dep_Blosc)
endif()