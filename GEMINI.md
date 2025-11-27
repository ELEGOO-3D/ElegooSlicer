# ElegooSlicer

## Project Overview

ElegooSlicer is an open-source slicer for FDM printers, forked from OrcaSlicer. It is written in C++ and uses the `libslic3r` library for its core slicing functionality. The graphical user interface (GUI) is built with the wxWidgets toolkit. The project uses CMake as its build system and manages a large number of dependencies using `ExternalProject_Add`.

The application supports a wide range of features, including:

*   Multi-material printing
*   Support material generation (including tree supports)
*   Wipe tower generation
*   Sequential printing
*   Variable layer height
*   G-code post-processing

## Building and Running

The project can be built on Windows, macOS, and Linux. The following instructions are based on the information found in the `README.md` and `CMakeLists.txt` files.

### Windows

1.  **Prerequisites:**
    *   Visual Studio 2019
    *   CMake
    *   Git
    *   git-lfs
    *   Strawberry Perl

2.  **Build:**
    *   Run `git lfs pull` after cloning the repository.
    *   Run `build_release.bat` in the `x64 Native Tools Command Prompt for VS 2019`.

### macOS

1.  **Prerequisites:**
    *   Xcode
    *   CMake
    *   git
    *   gettext
    *   libtool
    *   automake
    *   autoconf
    *   texinfo
    *   Install most of the prerequisites by running `brew install cmake gettext libtool automake autoconf texinfo`.

2.  **Build:**
    *   Run `build_release_macos.sh`.

### Linux

The `README.md` file does not provide specific instructions for building on Linux. However, the project includes a `BuildLinux.sh` script, which suggests that it is possible to build the project on Linux. The following instructions are based on the information found in the `CMakeLists.txt` and `deps/CMakeLists.txt` files.

1.  **Prerequisites:**
    *   A C++17 compiler (GCC or Clang)
    *   CMake
    *   Git
    *   All the dependencies listed in the `deps/CMakeLists.txt` file.

2.  **Build:**
    *   Run the `BuildLinux.sh` script.

## Development Conventions

*   **Coding Style:** The project uses the `.clang-format` file to enforce a consistent coding style.
*   **Testing:** The project includes a `tests` directory, which suggests that it has a suite of unit and integration tests. The `SLIC3R_BUILD_TESTS` CMake option can be used to enable the tests.
*   **Continuous Integration:** The project uses GitHub Actions for continuous integration. The workflow is defined in the `.github/workflows/` directory.

## Key Files

*   `README.md`: Provides a general overview of the project, including instructions on how to install and compile the software.
*   `CMakeLists.txt`: The main CMake file for the project. It defines the project's structure, dependencies, and build options.
*   `src/ElegooSlicer.cpp`: The main entry point for the application. It handles both command-line and GUI modes.
*   `src/libslic3r/Print.cpp`: Implements the core slicing logic of the application.
*   `deps/CMakeLists.txt`: Manages the project's dependencies using CMake's `ExternalProject_Add` function.
*   `version.inc`: Defines the version of the application.
