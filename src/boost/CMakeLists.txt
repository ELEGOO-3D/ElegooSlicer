cmake_minimum_required(VERSION 3.5)
project(nowide)

add_library(nowide STATIC
    nowide/args.hpp
    nowide/cenv.hpp
    nowide/config.hpp
    nowide/convert.hpp
    nowide/cstdio.hpp
    nowide/cstdlib.hpp
    nowide/filebuf.hpp
    nowide/fstream.hpp
    nowide/integration/filesystem.hpp
    nowide/iostream.cpp
    nowide/iostream.hpp
    nowide/stackstring.hpp
    nowide/system.hpp
    nowide/utf8_codecvt.hpp
    nowide/windows.hpp
)

# boost_headeronly removed as Boost dependencies are handled at a higher level
target_link_libraries(nowide PUBLIC )


