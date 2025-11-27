find_package(Freetype QUIET)

if (USE_SYSTEM_DEPS AND Freetype_FOUND)
    message(STATUS "Using system FREETYPE")
    set(FREETYPE_PKG "")
else()
    message(STATUS "Building FREETYPE as external project")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_ft_disable_zlib "-D FT_DISABLE_ZLIB=FALSE")
    else()
        set(_ft_disable_zlib "-D FT_DISABLE_ZLIB=TRUE")
    endif()

    elegooslicer_add_cmake_project(FREETYPE SHARED_LIBS_BOOL TRUE
        URL https://mirror.ossplanet.net/nongnu/freetype/freetype-2.12.1.tar.gz
        URL_HASH SHA256=efe71fd4b8246f1b0b1b9bfca13cfff1c9ad85930340c27df469733bbb620938
        CMAKE_ARGS
            ${_ft_disable_zlib}
            -D FT_DISABLE_BZIP2=TRUE
            -D FT_DISABLE_PNG=TRUE
            -D FT_DISABLE_HARFBUZZ=TRUE
            -D FT_DISABLE_BROTLI=TRUE
    )

    if(MSVC)
        add_debug_dep(dep_FREETYPE)
    endif()
    set(FREETYPE_PKG dep_FREETYPE)
endif()