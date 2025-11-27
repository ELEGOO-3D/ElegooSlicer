find_package(wxWidgets QUIET COMPONENTS Core)

if (USE_SYSTEM_DEPS AND wxWidgets_FOUND)
    message(STATUS "Using system wxWidgets")
    set(WXWIDGETS_PKG "")
else()
    message(STATUS "Building wxWidgets as external project")
    set(_wx_toolkit "")
    set(_wx_private_font "-DwxUSE_PRIVATE_FONTS=1")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_gtk_ver 2)

        if (DEP_WX_GTK3)
            set(_gtk_ver 3)
        endif ()

        set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
    endif()

    if (MSVC)
        set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=ON")
    else ()
        set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=OFF")
    endif ()

    elegooslicer_add_cmake_project(
        wxWidgets SHARED_LIBS_BOOL TRUE
        GIT_REPOSITORY "https://github.com/SoftFever/Orca-deps-wxWidgets"
        GIT_SHALLOW ON
        DEPENDS ${PNG_PKG} ${ZLIB_PKG} ${EXPAT_PKG} ${TIFF_PKG} ${JPEG_PKG}
        CMAKE_ARGS
            -DwxBUILD_PRECOMP=ON
            ${_wx_toolkit}
            "-DCMAKE_DEBUG_POSTFIX:STRING="
            -DwxBUILD_DEBUG_LEVEL=0
            -DwxBUILD_SAMPLES=OFF
            -DwxBUILD_SHARED=ON
            -DwxUSE_MEDIACTRL=ON
            -DwxUSE_DETECT_SM=OFF
            -DwxUSE_UNICODE=ON
            ${_wx_private_font}
            -DwxUSE_OPENGL=ON
            -DwxUSE_WEBREQUEST=ON
            -DwxUSE_WEBVIEW=ON
            ${_wx_edge}
            -DwxUSE_WEBVIEW_IE=OFF
            -DwxUSE_REGEX=builtin
            -DwxUSE_LIBXPM=builtin
            -DwxUSE_LIBSDL=OFF
            -DwxUSE_XTEST=OFF
            -DwxUSE_STC=OFF
            -DwxUSE_AUI=ON
            -DwxUSE_LIBPNG=sys
            -DwxUSE_ZLIB=sys
            -DwxUSE_LIBJPEG=sys
            -DwxUSE_LIBTIFF=sys
            -DwxUSE_NANOSVG=OFF
            -DwxUSE_EXPAT=sys
    )

    if (MSVC)
        add_debug_dep(dep_wxWidgets)
    endif ()
    set(WXWIDGETS_PKG dep_wxWidgets)
endif()