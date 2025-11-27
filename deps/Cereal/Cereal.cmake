find_package(cereal QUIET)

if (USE_SYSTEM_DEPS AND cereal_FOUND)
    message(STATUS "Using system Cereal")
    set(Cereal_PKG "")
else()
    message(STATUS "Building Cereal as external project")
    elegooslicer_add_cmake_project(Cereal SHARED_LIBS_BOOL TRUE
        URL "https://github.com/USCiLab/cereal/archive/refs/tags/v1.3.0.zip"
        URL_HASH SHA256=71642cb54658e98c8f07a0f0d08bf9766f1c3771496936f6014169d3726d9657
        CMAKE_ARGS
            -DJUST_INSTALL_CEREAL=ON
            -DSKIP_PERFORMANCE_COMPARISON=ON
            -DBUILD_TESTS=OFF
    )
    set(Cereal_PKG dep_Cereal)
endif()