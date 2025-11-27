find_package(OpenVDB QUIET COMPONENTS openvdb)

if (USE_SYSTEM_DEPS AND OpenVDB_FOUND)
    message(STATUS "Using system OpenVDB")
    set(OpenVDB_PKG "")
else()
    message(STATUS "Building OpenVDB as external project")
    elegooslicer_add_cmake_project(OpenVDB SHARED_LIBS_BOOL TRUE
        URL https://github.com/tamasmeszaros/openvdb/archive/a68fd58d0e2b85f01adeb8b13d7555183ab10aa5.zip
        URL_HASH SHA256=f353e7b99bd0cbfc27ac9082de51acf32a8bc0b3e21ff9661ecca6f205ec1d81
        DEPENDS dep_TBB dep_Blosc dep_OpenEXR dep_Boost
        CMAKE_ARGS
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DOPENVDB_BUILD_PYTHON_MODULE=OFF
            -DUSE_BLOSC=ON
            -DOPENVDB_ENABLE_RPATH:BOOL=OFF
            -DTBB_STATIC=OFF # Assuming TBB is also built shared or found as shared
            -DOPENVDB_BUILD_VDB_PRINT=ON
            -DDISABLE_DEPENDENCY_VERSION_CHECKS=ON
    )

    if (MSVC)
        if (${DEP_DEBUG})
            ExternalProject_Get_Property(dep_OpenVDB BINARY_DIR)
            ExternalProject_Add_Step(dep_OpenVDB build_debug
                DEPENDEES build
                DEPENDERS install
                COMMAND ${CMAKE_COMMAND} ../dep_OpenVDB -DOPENVDB_BUILD_VDB_PRINT=OFF
                COMMAND msbuild /m /P:Configuration=Debug INSTALL.vcxproj
                WORKING_DIRECTORY "${BINARY_DIR}"
            )
        endif ()
    endif ()
    set(OpenVDB_PKG dep_OpenVDB)
endif()