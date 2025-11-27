cmake_minimum_required(VERSION 3.13)
project(ElegooSlicer-native)

add_subdirectory(build-utils)
add_subdirectory(admesh)
# add_subdirectory(avrdude)
# boost/nowide
add_subdirectory(boost)
add_subdirectory(clipper)
add_subdirectory(clipper2)
add_subdirectory(miniz)
add_subdirectory(minilzo)
add_subdirectory(glu-libtess)
add_subdirectory(qhull)
add_subdirectory(Shiny)
add_subdirectory(semver)
add_subdirectory(libigl)
add_subdirectory(hints)
add_subdirectory(mcut)
add_subdirectory(qoi)

# Adding libnest2d project for bin packing...
add_subdirectory(libnest2d)

add_subdirectory(libslic3r)

if (SLIC3R_GUI)
    add_subdirectory(imgui)
    add_subdirectory(imguizmo)
    add_subdirectory(hidapi)
    include_directories(hidapi/include)

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
        # Find and configure wxWidgets
        if(USE_SYSTEM_DEPS)
            if (SLIC3R_WX_STABLE)
                find_package(wxWidgets 3.0 COMPONENTS base core adv html gl aui net media webview)
            else ()
                find_package(wxWidgets 3.1 COMPONENTS base core adv html gl aui net media webview)
            endif ()
            if(NOT wxWidgets_FOUND)
                message(STATUS "wxWidgets not found in system, falling back to external project.")
            endif()
        else()
            message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external wxWidgets project.")
        endif()
    else ()
        if(USE_SYSTEM_DEPS)
            find_package(wxWidgets 3.1 COMPONENTS html adv gl core base webview aui net media)
            if(NOT wxWidgets_FOUND)
                message(STATUS "wxWidgets not found in system, falling back to external project.")
            endif()
        else()
            message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external wxWidgets project.")
        endif()
    endif ()

    if(NOT USE_SYSTEM_DEPS OR NOT wxWidgets_FOUND)
        add_dependencies(ElegooSlicer dep_wxWidgets)
        # Assuming external wxWidgets populates appropriate variables
        # This will need to be properly addressed during linking
    endif()

    if(UNIX)
        message(STATUS "wx-config path: ${wxWidgets_CONFIG_EXECUTABLE}")
    endif()

    include(${wxWidgets_USE_FILE})

    # Find and configure JPEG
    if(USE_SYSTEM_DEPS)
        find_package(JPEG)
        if(NOT JPEG_FOUND)
            message(STATUS "JPEG not found in system, falling back to external project.")
        endif()
    else()
        message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external JPEG project.")
    endif()

    if(NOT USE_SYSTEM_DEPS OR NOT JPEG_FOUND)
        add_dependencies(ElegooSlicer dep_JPEG)
    endif()

    # Find and configure TIFF
    if(USE_SYSTEM_DEPS)
        find_package(TIFF)
        if(NOT TIFF_FOUND)
            message(STATUS "TIFF not found in system, falling back to external project.")
        endif()
    else()
        message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external TIFF project.")
    endif()

    if(NOT USE_SYSTEM_DEPS OR NOT TIFF_FOUND)
        add_dependencies(ElegooSlicer dep_TIFF)
    endif()

    # Remove the old logic for wxWidgets_LIBRARIES filtering. Assume proper linking.
    # The dep targets are already added by the main CMakeLists.txt and individual dep .cmake files.
    # No need to explicitly manipulate wxWidgets_LIBRARIES here.

    
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

# Find OpenCSG (always external)
if(NOT USE_SYSTEM_DEPS OR NOT OpenCSG_FOUND) # OpenCSG will never be found as system lib
    add_dependencies(ElegooSlicer dep_OpenCSG)
endif()

# Find GMP
if(USE_SYSTEM_DEPS)
    find_package(GMP)
    if(NOT GMP_FOUND)
        message(STATUS "GMP not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external GMP project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT GMP_FOUND)
    add_dependencies(ElegooSlicer dep_GMP)
endif()

# Find MPFR
if(USE_SYSTEM_DEPS)
    find_package(MPFR)
    if(NOT MPFR_FOUND)
        message(STATUS "MPFR not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external MPFR project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT MPFR_FOUND)
    add_dependencies(ElegooSlicer dep_MPFR)
endif()

# Find CGAL
if(USE_SYSTEM_DEPS)
    find_package(CGAL)
    if(NOT CGAL_FOUND)
        message(STATUS "CGAL not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external CGAL project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT CGAL_FOUND)
    add_dependencies(ElegooSlicer dep_CGAL)
endif()

# Find OCCT
if(USE_SYSTEM_DEPS)
    find_package(OpenCASCADE COMPONENTS TKernel TKBrep TKGeomBase)
    if(NOT OpenCASCADE_FOUND)
        message(STATUS "OCCT not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external OCCT project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT OpenCASCADE_FOUND)
    add_dependencies(ElegooSlicer dep_OCCT)
endif()

# Find OpenCV
if(USE_SYSTEM_DEPS)
    find_package(OpenCV COMPONENTS core imgcodecs imgproc world)
    if(NOT OpenCV_FOUND)
        message(STATUS "OpenCV not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external OpenCV project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT OpenCV_FOUND)
    add_dependencies(ElegooSlicer dep_OpenCV)
endif()

# Find FREETYPE
if(USE_SYSTEM_DEPS)
    find_package(Freetype)
    if(NOT Freetype_FOUND)
        message(STATUS "FREETYPE not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external FREETYPE project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT Freetype_FOUND)
    add_dependencies(ElegooSlicer dep_FREETYPE)
endif()

if(ORCA_TOOLS)
    # ElegooSlicer_profile_validator
    add_executable(ElegooSlicer_profile_validator ElegooSlicer_profile_validator.cpp)

    set(_profile_validator_LIBS "")
    # Add Boost
    if(Boost_FOUND)
        list(APPEND _profile_validator_LIBS Boost::system Boost::filesystem Boost::thread Boost::log Boost::locale Boost::regex Boost::chrono Boost::atomic Boost::date_time Boost::iostreams Boost::program_options)
    else()
        list(APPEND _profile_validator_LIBS dep_Boost)
    endif()
    # Add CURL
    if(CURL_FOUND)
        list(APPEND _profile_validator_LIBS CURL::libcurl)
    else()
        list(APPEND _profile_validator_LIBS dep_CURL)
    endif()
    # Add OpenSSL
    if(OpenSSL_FOUND)
        list(APPEND _profile_validator_LIBS OpenSSL::SSL OpenSSL::Crypto)
    else()
        list(APPEND _profile_validator_LIBS dep_OpenSSL)
    endif()
    # Add other common dependencies if needed for the validator

    target_link_libraries(ElegooSlicer_profile_validator libslic3r ${_profile_validator_LIBS})
    target_compile_definitions(ElegooSlicer_profile_validator PRIVATE -DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x602 -DBOOST_SYSTEM_USE_UTF8)
    if(WIN32)
        target_link_libraries(ElegooSlicer_profile_validator bcrypt.lib)
    endif()
endif()

# Create a slic3r executable
# Process mainfests for various platforms.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/ElegooSlicer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/ElegooSlicer.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/ElegooSlicer.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/ElegooSlicer.manifest @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
if (WIN32)
    add_library(ElegooSlicer SHARED ElegooSlicer.cpp ElegooSlicer.hpp BaseException.cpp BaseException.h StackWalker.cpp StackWalker.h)
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
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/unix/BuildLinuxImage.sh.in ${CMAKE_CURRENT_BINARY_DIR}/BuildLinuxImage.sh @ONLY)
endif ()

target_link_libraries(ElegooSlicer libslic3r ) # Removed cereal::cereal, will add generically

set(_elegooslicer_LIBS "")

# Core dependencies that ElegooSlicer (and libslic3r) use
if(Boost_FOUND)
    list(APPEND _elegooslicer_LIBS Boost::system Boost::filesystem Boost::thread Boost::log Boost::locale Boost::regex Boost::chrono Boost::atomic Boost::date_time Boost::iostreams Boost::program_options)
endif()
if(TBB_FOUND)
    list(APPEND _elegooslicer_LIBS TBB::tbb TBB::tbbmalloc)
endif()
if(OpenSSL_FOUND)
    list(APPEND _elegooslicer_LIBS OpenSSL::SSL OpenSSL::Crypto)
endif()
if(CURL_FOUND)
    list(APPEND _elegooslicer_LIBS CURL::libcurl)
endif()
if(ZLIB_FOUND)
    list(APPEND _elegooslicer_LIBS ZLIB::ZLIB)
endif()
if(PNG_FOUND)
    list(APPEND _elegooslicer_LIBS PNG::PNG)
endif()
if(EXPAT_FOUND)
    list(APPEND _elegooslicer_LIBS ${EXPAT_LIBRARIES})
endif()
if(GLEW_FOUND)
    list(APPEND _elegooslicer_LIBS GLEW::GLEW)
endif()
if(glfw3_FOUND)
    list(APPEND _elegooslicer_LIBS glfw)
endif()
if(cereal_FOUND)
    list(APPEND _elegooslicer_LIBS cereal::cereal)
endif()
if(NLopt_FOUND)
    list(APPEND _elegooslicer_LIBS NLopt::nlopt)
endif()
if(OpenVDB_FOUND)
    list(APPEND _elegooslicer_LIBS OpenVDB::openvdb)
endif()
if(GMP_FOUND)
    list(APPEND _elegooslicer_LIBS GMP::GMP)
endif()
if(MPFR_FOUND)
    list(APPEND _elegooslicer_LIBS MPFR::MPFR)
endif()
if(CGAL_FOUND)
    list(APPEND _elegooslicer_LIBS CGAL::CGAL)
endif()
if(OpenCASCADE_FOUND)
    list(APPEND _elegooslicer_LIBS ${OCCT_LIBS})
endif()
if(OpenCV_FOUND)
    list(APPEND _elegooslicer_LIBS OpenCV::core OpenCV::imgcodecs OpenCV::imgproc OpenCV::world)
endif()
if(Freetype_FOUND)
    list(APPEND _elegooslicer_LIBS Freetype::Freetype)
endif()

# Add OpenCSG if it's external (always)
list(APPEND _elegooslicer_LIBS dep_OpenCSG) # OpenCSG will always be external

# General linking for ElegooSlicer
target_link_libraries(ElegooSlicer PUBLIC
    libslic3r
    ${_elegooslicer_LIBS}
)

if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    # -liconv: boost links to libiconv by default
    target_link_libraries(ElegooSlicer "-liconv -framework IOKit" "-framework CoreFoundation" "-framework AVFoundation" "-framework AVKit" "-framework CoreMedia" "-framework VideoToolbox" -lc++)
elseif (MSVC)
    # Manifest is provided through ElegooSlicer.rc, don't generate your own.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
else ()
    # Boost on Raspberry-Pi does not link to pthreads explicitely.
    target_link_libraries(ElegooSlicer ${CMAKE_DL_LIBS} -lstdc++ Threads::Threads pangoft2-1.0)
endif ()

# Add the Slic3r GUI library, libcurl, OpenGL and GLU libraries.
if (SLIC3R_GUI)
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
else ()
    install(TARGETS ElegooSlicer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()
