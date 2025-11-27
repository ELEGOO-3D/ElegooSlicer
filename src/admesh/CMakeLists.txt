cmake_minimum_required(VERSION 3.5)
project(admesh)

add_library(admesh STATIC
    connect.cpp
    normals.cpp
    shared.cpp
    stl.h
    stl_io.cpp
    stlinit.cpp
    util.cpp
)

# boost_headeronly removed as Boost dependencies are handled at a higher level
target_link_libraries(admesh PRIVATE )
