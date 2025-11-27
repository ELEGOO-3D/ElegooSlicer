find_package(EXPAT QUIET)

if (USE_SYSTEM_DEPS AND EXPAT_FOUND)
    message(STATUS "Using system EXPAT")
    set(EXPAT_PKG "")
else()
    message(STATUS "Building EXPAT as external project")
    elegooslicer_add_cmake_project(EXPAT SHARED_LIBS_BOOL TRUE
      SOURCE_DIR          ${CMAKE_CURRENT_LIST_DIR}/expat
    )

    if (MSVC)
        add_debug_dep(dep_EXPAT)
    endif ()
    set(EXPAT_PKG dep_EXPAT)
endif()