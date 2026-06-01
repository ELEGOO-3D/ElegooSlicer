elegooslicer_add_cmake_project(Sentry
    GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
    GIT_TAG 0.14.2
    GIT_SUBMODULES_RECURSE TRUE
    DEPENDS ${ZLIB_PKG} ${CURL_PKG}
    CMAKE_ARGS
        -DSENTRY_BUILD_SHARED_LIBS=OFF
        -DSENTRY_BUILD_EXAMPLES=OFF
        -DSENTRY_BUILD_TESTS=OFF
        -DSENTRY_BACKEND=crashpad
        -DCMAKE_INSTALL_LIBDIR=lib
)

if (MSVC)
    add_debug_dep(dep_Sentry)
endif ()
