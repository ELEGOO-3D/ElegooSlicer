if (IN_GIT_REPO)
    set(CRASHPAD_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_Crashpad-prefix/src/dep_Crashpad)
endif ()

elegooslicer_add_cmake_project(Crashpad
    GIT_REPOSITORY https://github.com/backtrace-labs/crashpad.git
    GIT_TAG v0.2.0
    GIT_SUBMODULES_RECURSE TRUE
    DEPENDS ${ZLIB_PKG} ${CURL_PKG}
    PATCH_COMMAND ${GIT_EXECUTABLE} checkout -f -- . && ${GIT_EXECUTABLE} clean -df &&
                  git apply ${CRASHPAD_DIRECTORY_FLAG} --unidiff-zero --ignore-whitespace --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-Crashpad-install.patch
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
)

if (MSVC)
    add_debug_dep(dep_Crashpad)
endif ()

# util/mini_chromium are OBJECT libs; install(TARGETS ...) does not install their .lib.
# client is a static lib; install(TARGETS ...) may not install correctly in MSVC multi-config.
# Copy built client.lib, util.lib and mini_chromium.lib from build dir to DESTDIR/lib/crashpad after build.
if (MSVC)
    set(_crashpad_cfg "Release")
    if (DEP_DEBUG)
        set(_crashpad_cfg "Debug")
    elseif (ORCA_INCLUDE_DEBUG_INFO)
        set(_crashpad_cfg "RelWithDebInfo")
    endif ()
    ExternalProject_Get_Property(dep_Crashpad BINARY_DIR)
    set(_client_lib "${BINARY_DIR}/client/${_crashpad_cfg}/client.lib")
    set(_util_lib "${BINARY_DIR}/util/util.dir/${_crashpad_cfg}/util.lib")
    set(_mc_lib "${BINARY_DIR}/third_party/mini_chromium/mini_chromium.dir/${_crashpad_cfg}/mini_chromium.lib")
    set(_handler_exe "${BINARY_DIR}/handler/${_crashpad_cfg}/handler.exe")
    ExternalProject_Add_Step(dep_Crashpad copy_crashpad_libs
        DEPENDEES build
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DESTDIR}lib/crashpad"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_client_lib}" "${DESTDIR}lib/crashpad/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_util_lib}" "${DESTDIR}lib/crashpad/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_mc_lib}" "${DESTDIR}lib/crashpad/"
    )
    ExternalProject_Add_Step(dep_Crashpad copy_crashpad_handler
        DEPENDEES build
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DESTDIR}bin/crashpad"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_handler_exe}" "${DESTDIR}bin/crashpad/"
    )
    unset(_crashpad_cfg)
    unset(_client_lib)
    unset(_util_lib)
    unset(_mc_lib)
    unset(_handler_exe)
endif ()
