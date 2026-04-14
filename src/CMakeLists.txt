cmake_minimum_required(VERSION 3.13)
project(ElegooSlicer-native)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Crashpad before dev-utils (which needs it) - Windows only
if (WIN32)
    find_package(Crashpad REQUIRED MODULE)
endif ()

# Include dev-utils for encoding check and other utilities
add_subdirectory(dev-utils)
# add_subdirectory(avrdude)
# Note: semver and hints are now included from deps_src/CMakeLists.txt

add_subdirectory(libslic3r)

if (SLIC3R_GUI)
    # imgui, imguizmo, and hidapi are now included from deps_src

    if(WIN32)
        message(STATUS "WXWIN environment set to: $ENV{WXWIN}")
    elseif(UNIX)
        set(wxWidgets_USE_UNICODE ON)
        if(SLIC3R_STATIC)
            set(wxWidgets_USE_STATIC ON)
        else()
            set(wxWidgets_USE_STATIC OFF)
        endif()
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set (wxWidgets_CONFIG_OPTIONS "--toolkit=gtk${SLIC3R_GTK}")
        if (SLIC3R_WX_STABLE)
            find_package(wxWidgets 3.0 REQUIRED COMPONENTS base core adv html gl aui net media webview)
        else ()
            find_package(wxWidgets 3.1 REQUIRED COMPONENTS base core adv html gl aui net media webview)
        endif ()
    else ()
        find_package(wxWidgets 3.1 REQUIRED COMPONENTS html adv gl core base webview aui net media)
    endif ()

    if(UNIX)
        message(STATUS "wx-config path: ${wxWidgets_CONFIG_EXECUTABLE}")
    endif()

    include(${wxWidgets_USE_FILE})

    find_package(JPEG QUIET)

    string(REGEX MATCH "wxpng" WX_PNG_BUILTIN ${wxWidgets_LIBRARIES})
    if (PNG_FOUND AND NOT WX_PNG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX png)
        list(APPEND wxWidgets_LIBRARIES ${PNG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxjpeg" WX_JPEG_BUILTIN ${wxWidgets_LIBRARIES})
    if (JPEG_FOUND AND NOT WX_JPEG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX jpeg)
        list(APPEND wxWidgets_LIBRARIES ${JPEG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxexpat" WX_EXPAT_BUILTIN ${wxWidgets_LIBRARIES})
    if (EXPAT_FOUND AND NOT WX_EXPAT_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX expat)
        list(APPEND wxWidgets_LIBRARIES ${EXPAT_LIBRARIES})
    endif ()
    
    # This is an issue in the new wxWidgets cmake build, doesn't deal with librt
    find_library(LIBRT rt)
    if(LIBRT)
        list(APPEND wxWidgets_LIBRARIES ${LIBRT})
    endif()

    # This fixes a OpenGL linking issue on OSX. wxWidgets cmake build includes
    # wrong libs for opengl in the link line and it does not link to it by himself.
    # libslic3r_gui will link to opengl anyway, so lets override wx
    list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX OpenGL)

#    list(REMOVE_ITEM wxWidgets_LIBRARIES oleacc)
    message(STATUS "wx libs: ${wxWidgets_LIBRARIES}")

    add_subdirectory(slic3r)
endif()

if(ORCA_TOOLS)
    # ElegooSlicer_profile_validator
    if(APPLE)
        add_executable(ElegooSlicer_profile_validator MACOSX_BUNDLE dev-utils/ElegooSlicer_profile_validator.cpp)
        set_target_properties(ElegooSlicer_profile_validator PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "ElegooSlicer Profile Validator"
            MACOSX_BUNDLE_BUNDLE_VERSION "${ELEGOOSLICER_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${ELEGOOSLICER_VERSION}"
            MACOSX_BUNDLE_IDENTIFIER "com.softfever.elegooslicer.profile-validator"
            MACOSX_BUNDLE_COPYRIGHT "© 2024 SoftFever"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.softfever.elegooslicer.profile-validator"
        )
    else()
        add_executable(ElegooSlicer_profile_validator dev-utils/ElegooSlicer_profile_validator.cpp)
    endif()
    target_link_libraries(ElegooSlicer_profile_validator libslic3r boost_headeronly libcurl OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(ElegooSlicer_profile_validator PRIVATE -DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x602 -DBOOST_SYSTEM_USE_UTF8)
    if(WIN32)
        target_link_libraries(ElegooSlicer_profile_validator bcrypt.lib)
    elseif(APPLE)
        # Link SystemConfiguration framework needed for libcurl's SCDynamicStoreCopyProxies
        target_link_libraries(ElegooSlicer_profile_validator "-framework SystemConfiguration")
    endif()
endif()

# Create a slic3r executable
# Process mainfests for various platforms.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dev-utils/platform/msw/ElegooSlicer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/ElegooSlicer.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dev-utils/platform/msw/ElegooSlicer.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/ElegooSlicer.manifest @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dev-utils/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
if (WIN32)
    add_library(ElegooSlicer SHARED ElegooSlicer.cpp ElegooSlicer.hpp dev-utils/CrashReporter.cpp dev-utils/CrashReporter.h)
else ()
    add_executable(ElegooSlicer ElegooSlicer.cpp ElegooSlicer.hpp)
endif ()

if (MINGW)
    target_link_options(ElegooSlicer PUBLIC "-Wl,-allow-multiple-definition")
    set_target_properties(ElegooSlicer PROPERTIES PREFIX "")
endif (MINGW)

if (NOT WIN32 AND NOT APPLE)
    # Binary name on unix like systems (Linux, Unix)
    set_target_properties(ElegooSlicer PROPERTIES OUTPUT_NAME "elegoo-slicer")
    set(SLIC3R_APP_CMD "elegoo-slicer")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dev-utils/platform/unix/build_linux_image.sh.in ${CMAKE_CURRENT_BINARY_DIR}/build_linux_image.sh USE_SOURCE_PERMISSIONS @ONLY)
endif ()

target_link_libraries(ElegooSlicer libslic3r cereal::cereal)
if (WIN32)
    target_include_directories(ElegooSlicer PRIVATE ${Crashpad_INCLUDE_DIR})
    target_link_libraries(ElegooSlicer Crashpad::client)
    # Setup crashpad_handler for Windows
    elegooslicer_setup_crashpad_handler(ElegooSlicer)
endif ()

# target_include_directories(ElegooSlicer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/slic3r)
if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    # -liconv: boost links to libiconv by default
    # -framework SystemConfiguration: needed for libcurl's SCDynamicStoreCopyProxies
    target_link_libraries(ElegooSlicer "-liconv -framework IOKit" "-framework CoreFoundation" "-framework SystemConfiguration" "-framework AVFoundation" "-framework AVKit" "-framework CoreMedia" "-framework VideoToolbox" -lc++)
elseif (MSVC)
    # Manifest is provided through ElegooSlicer.rc, don't generate your own.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
else ()
    # Boost on Raspberry-Pi does not link to pthreads explicitely.
    target_link_libraries(ElegooSlicer ${CMAKE_DL_LIBS} -lstdc++ Threads::Threads pangoft2-1.0)
endif ()

# Add the Slic3r GUI library, libcurl, OpenGL and GLU libraries.
if (SLIC3R_GUI)
#    target_link_libraries(ElegooSlicer ws2_32 uxtheme setupapi libslic3r_gui ${wxWidgets_LIBRARIES})
target_link_libraries(ElegooSlicer libslic3r_gui)
    if (MSVC)
        # Generate debug symbols even in release mode.
        target_link_options(ElegooSlicer PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
        target_link_libraries(ElegooSlicer user32.lib Setupapi.lib)
    elseif (MINGW)
        target_link_libraries(ElegooSlicer ws2_32 uxtheme setupapi)
    elseif (APPLE)
        target_link_libraries(ElegooSlicer "-framework OpenGL")
    else ()
        target_link_libraries(ElegooSlicer -ldl)
    endif ()
    #if (WIN32)
    #    find_library(PSAPI_LIB NAMES Psapi)
    #    target_link_libraries(ElegooSlicer ${PSAPI_LIB})
    #endif ()
endif ()

# On Windows, a shim application is required to produce a console / non console version of the Slic3r application.
# Also the shim may load the Mesa software OpenGL renderer if the default renderer does not support OpenGL 2.0 and higher.
if (WIN32)
    if (MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode")
    endif()

    add_executable(ElegooSlicer_app_gui WIN32 ElegooSlicer_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/ElegooSlicer.rc)
    # Generate debug symbols even in release mode.
    if(MSVC)
        target_link_options(ElegooSlicer_app_gui PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif()
    target_compile_definitions(ElegooSlicer_app_gui PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE)
    add_dependencies(ElegooSlicer_app_gui ElegooSlicer)
    set_target_properties(ElegooSlicer_app_gui PROPERTIES OUTPUT_NAME "elegoo-slicer")
    target_link_libraries(ElegooSlicer_app_gui PRIVATE boost_headeronly)
endif ()

# Link the resources dir to where Slic3r GUI expects it
set(output_dlls_Release "")
set(output_dlls_Debug "")
set(output_dlls_RelWithDebInfo "")
if (WIN32)
    # This has to be a separate target due to the windows command line lenght limits
    add_custom_target(COPY_DLLS ALL DEPENDS ElegooSlicer)

    if (CMAKE_CONFIGURATION_TYPES)
        foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}" WIN_CONF_OUTPUT_DIR)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/resources" WIN_RESOURCES_SYMLINK)
            add_custom_command(TARGET ElegooSlicer POST_BUILD
                COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
                COMMENT "Symlinking the resources directory into the build tree"
                VERBATIM
            )
        endforeach ()

        if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
            elegooslicer_copy_dlls(COPY_DLLS "Debug" "d" output_dlls_Debug)
        elseif("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
            elegooslicer_copy_dlls(COPY_DLLS "RelWithDebInfo" "" output_dlls_Release)
        else()
            elegooslicer_copy_dlls(COPY_DLLS "Release" "" output_dlls_Release)
        endif()
    else ()
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/resources" WIN_RESOURCES_SYMLINK)
        add_custom_command(TARGET ElegooSlicer POST_BUILD
            COMMAND if not exist "${WIN_RESOURCES_SYMLINK}" "(" mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}" ")"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM
        )
    endif ()

else ()
    if (APPLE AND NOT CMAKE_MACOSX_BUNDLE)
        # On OSX, the name of the binary matches the name of the Application.
        add_custom_command(TARGET ElegooSlicer POST_BUILD
            COMMAND ln -sf ElegooSlicer elegoo-slicer
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:ElegooSlicer>"
            VERBATIM)
    else ()
        add_custom_command(TARGET ElegooSlicer POST_BUILD
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:ElegooSlicer>"
            VERBATIM)
    endif ()
    if (XCODE)
        # Because of Debug/Release/etc. configurations (similar to MSVC) the slic3r binary is located in an extra level
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
        set(BIN_CONF_DIR "Debug")
    else ()
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/../resources")
    endif ()
    
    # Copy Agora libraries for Linux
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_custom_command(TARGET ElegooSlicer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_PREFIX_PATH}/bin/libaosl.so"
                "$<TARGET_FILE_DIR:ElegooSlicer>/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_PREFIX_PATH}/bin/libagora_rtm_sdk.so"
                "$<TARGET_FILE_DIR:ElegooSlicer>/"
            COMMENT "Copying Agora shared libraries to build directory"
            VERBATIM
        )
    endif()
    
    if (CMAKE_MACOSX_BUNDLE)
        if (CMAKE_CONFIGURATION_TYPES)
            set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/ElegooSlicer.app/Contents/Resources")
        else()
            set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/ElegooSlicer.app/Contents/Resources")
        endif()
        set(MACOSX_BUNDLE_ICON_FILE Icon.icns)
        set(MACOSX_BUNDLE_BUNDLE_NAME "ElegooSlicer")
        set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${ELEGOOSLICER_VERSION})
        set(MACOSX_BUNDLE_COPYRIGHT " ")
        # set(MACOSX_BUNDLE_COPYRIGHT "Copyright(C) 2022-2024 Li Jiang All Rights Reserved")
        
        # Copy Agora frameworks to app bundle
        add_custom_command(TARGET ElegooSlicer POST_BUILD
            COMMAND rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/Frameworks/AgoraRtmKit.framework"
            COMMAND rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/Frameworks/aosl.framework"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/Frameworks"
            COMMAND rsync -a "${CMAKE_PREFIX_PATH}/bin/AgoraRtmKit.framework" "$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/Frameworks/"
            COMMAND rsync -a "${CMAKE_PREFIX_PATH}/bin/aosl.framework" "$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/Frameworks/"
            COMMENT "Copying Agora frameworks to app bundle with symlinks preserved"
            VERBATIM
        )
        
        # # copy elegoo-link-service to MacOS folder
        # add_custom_command(TARGET ElegooSlicer POST_BUILD
        #     COMMAND rm -f "$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/MacOS/elegoo-link-service"
        #     COMMAND cp "${CMAKE_PREFIX_PATH}/bin/elegoo-link-service" "$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/MacOS/elegoo-link-service"
        #     COMMAND bash -c "if ! otool -l '$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/MacOS/elegoo-link-service' | grep -q '@executable_path/../Frameworks'; then install_name_tool -add_rpath @executable_path/../Frameworks '$<TARGET_BUNDLE_CONTENT_DIR:ElegooSlicer>/MacOS/elegoo-link-service'; echo 'Added rpath to elegoo-link-service'; else echo 'Rpath already exists in elegoo-link-service'; fi"
        #     COMMENT "Copying elegoo-link-service to app bundle and fixing rpath"
        #     VERBATIM
        # )

    endif()
    add_custom_command(TARGET ElegooSlicer POST_BUILD
        COMMAND ln -sfn "${SLIC3R_RESOURCES_DIR}" "${BIN_RESOURCES_DIR}"
        COMMENT "Symlinking the resources directory into the build tree"
        VERBATIM)
endif ()

# Slic3r binary install target. Default build type is release in case no CMAKE_BUILD_TYPE is provided.
if( ("${CMAKE_BUILD_TYPE}" STREQUAL "Release") OR ("${CMAKE_BUILD_TYPE}" STREQUAL "") )
    set (build_type "Release")
    set(CMAKE_BUILD_POSTFIX "")
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    set (build_type "RelWithDebInfo")
    set(CMAKE_BUILD_POSTFIX "")
else()
    set (build_type "Debug")
    set(CMAKE_BUILD_POSTFIX "d")
endif()
message(STATUS "libslic3r-CMAKE_BUILD_TYPE: ${build_type}")
message(STATUS "CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")
if (WIN32)
    install(TARGETS ElegooSlicer RUNTIME DESTINATION ".")
    if (MSVC)
        install(TARGETS ElegooSlicer_app_gui RUNTIME DESTINATION ".")
    endif ()
    install(FILES ${output_dlls_${build_type}} DESTINATION ".")
    # Install crashpad_handler
else ()
    install(TARGETS ElegooSlicer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
    
    # Install Agora shared libraries for Linux
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if (EXISTS "${CMAKE_PREFIX_PATH}/bin/libaosl.so")
            install(FILES "${CMAKE_PREFIX_PATH}/bin/libaosl.so" DESTINATION "${CMAKE_INSTALL_BINDIR}")
        endif()
        if (EXISTS "${CMAKE_PREFIX_PATH}/bin/libagora_rtm_sdk.so")
            install(FILES "${CMAKE_PREFIX_PATH}/bin/libagora_rtm_sdk.so" DESTINATION "${CMAKE_INSTALL_BINDIR}")
        endif()
    endif()
endif ()
