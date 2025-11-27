find_package(JPEG QUIET)

if (USE_SYSTEM_DEPS AND JPEG_FOUND)
    message(STATUS "Using system JPEG")
    set(JPEG_PKG "")
else()
    message(STATUS "Building JPEG as external project")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if (JPEG_VERSION STREQUAL "6")
            message("Using Jpeg Lib 62")
            set(jpeg_flag "")
        elseif (JPEG_VERSION STREQUAL "7")
            message("Using Jpeg Lib 70")
            set(jpeg_flag "-DWITH_JPEG7=ON")
        else ()
            message("Using Jpeg Lib 80")
            set(jpeg_flag "-DWITH_JPEG8=ON")
        endif ()
    endif()

    elegooslicer_add_cmake_project(JPEG SHARED_LIBS_BOOL TRUE
        URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.0.1.zip
        URL_HASH SHA256=d6d99e693366bc03897677650e8b2dfa76b5d6c54e2c9e70c03f0af821b0a52f
        DEPENDS ${ZLIB_PKG}
        CMAKE_ARGS
            ${jpeg_flag}
    )
    set(JPEG_PKG dep_JPEG)
endif()