# FindCrashpad - Find Crashpad crash-reporting library (Windows, macOS, Linux)
#
# All Crashpad libs (client, util, mini_chromium) are installed to lib/crashpad
# via 0001-Crashpad-install.patch. Headers → include/crashpad.
#
# Defines:
#   Crashpad_FOUND
#   Crashpad_INCLUDE_DIR   - include/crashpad (use #include <client/...> in code)
#   Crashpad::client       - IMPORTED target (client + util + mini_chromium)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs_SLIC3R.cmake)

# Avoid stale cache pointing to lib/ instead of lib/crashpad (e.g. LNK1104 for mini_chromium.lib).
foreach(_v Crashpad_client_LIBRARY Crashpad_util_LIBRARY Crashpad_mini_chromium_LIBRARY)
    unset(${_v} CACHE)
endforeach()
unset(_v)

find_path(Crashpad_INCLUDE_DIR NAMES client/crashpad_client.h
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/crashpad
    NO_DEFAULT_PATH
)

# Search only under lib/crashpad (and lib64/crashpad). Never search lib/ to avoid
# resolving util/mini_chromium to wrong paths.
set(_crashpad_lib_dirs "")
foreach(_p ${CMAKE_PREFIX_PATH})
    list(APPEND _crashpad_lib_dirs "${_p}/lib/crashpad" "${_p}/lib64/crashpad")
    if(MSVC OR CMAKE_CONFIGURATION_TYPES)
        list(APPEND _crashpad_lib_dirs
            "${_p}/lib/crashpad/Release" "${_p}/lib/crashpad/RelWithDebInfo" "${_p}/lib/crashpad/Debug"
            "${_p}/lib64/crashpad/Release" "${_p}/lib64/crashpad/RelWithDebInfo" "${_p}/lib64/crashpad/Debug")
    endif()
endforeach()

find_library(Crashpad_client_LIBRARY NAMES client crashpad_client
    PATHS ${_crashpad_lib_dirs}
    NO_DEFAULT_PATH
)
find_library(Crashpad_util_LIBRARY NAMES util crashpad_util utild crashpad_utild
    PATHS ${_crashpad_lib_dirs}
    NO_DEFAULT_PATH
)
find_library(Crashpad_mini_chromium_LIBRARY NAMES
    mini_chromium crashpad_mini_chromium mini_chromiumd crashpad_mini_chromiumd
    libmini_chromium libmini_chromiumd
    PATHS ${_crashpad_lib_dirs}
    NO_DEFAULT_PATH
)
unset(_crashpad_lib_dirs)

find_package_handle_standard_args_SLIC3R(Crashpad
    REQUIRED_VARS Crashpad_INCLUDE_DIR Crashpad_client_LIBRARY
                  Crashpad_util_LIBRARY Crashpad_mini_chromium_LIBRARY
)

if(Crashpad_FOUND AND NOT TARGET Crashpad::client)
    set(_link "${Crashpad_util_LIBRARY};${Crashpad_mini_chromium_LIBRARY}")
    if(LINUX)
        find_package(Threads QUIET)
        if(Threads_FOUND)
            list(APPEND _link Threads::Threads)
        endif()
    endif()
    add_library(Crashpad::client UNKNOWN IMPORTED)
    set_target_properties(Crashpad::client PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${Crashpad_INCLUDE_DIR}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        IMPORTED_LOCATION "${Crashpad_client_LIBRARY}"
        INTERFACE_LINK_LIBRARIES "${_link}"
    )
    unset(_link)
endif()
