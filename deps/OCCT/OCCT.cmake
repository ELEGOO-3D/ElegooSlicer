find_package(OpenCASCADE QUIET COMPONENTS TKernel TKBRep TKGeomBase)

if (USE_SYSTEM_DEPS AND OpenCASCADE_FOUND)
    message(STATUS "Using system OCCT")
    set(OCCT_PKG "")
else()
    message(STATUS "Building OCCT as external project")
    set(library_build_type "Shared") # Always build shared when building externally

    file(RELATIVE_PATH BINARY_DIR_REL  ${CMAKE_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR})

    elegooslicer_add_cmake_project(OCCT SHARED_LIBS_BOOL TRUE
        URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_6_0.zip
        URL_HASH SHA256=28334f0e98f1b1629799783e9b4d21e05349d89e695809d7e6dfa45ea43e1dbc
        PATCH_COMMAND git apply --directory ${BINARY_DIR_REL}/dep_OCCT-prefix/src/dep_OCCT --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-OCCT-fix.patch
        DEPENDS ${FREETYPE_PKG}
        CMAKE_ARGS
            -DBUILD_LIBRARY_TYPE=${library_build_type}
            -DUSE_TK=OFF
            -DUSE_TBB=OFF
            -DUSE_FFMPEG=OFF
            -DUSE_VTK=OFF
            -DBUILD_DOC_Overview=OFF
            -DBUILD_MODULE_ApplicationFramework=OFF
            -DBUILD_MODULE_Draw=OFF
            -DBUILD_MODULE_FoundationClasses=OFF
            -DBUILD_MODULE_ModelingAlgorithms=OFF
            -DBUILD_MODULE_ModelingData=OFF
            -DBUILD_MODULE_Visualization=OFF
    )
    set(OCCT_PKG dep_OCCT)
endif()