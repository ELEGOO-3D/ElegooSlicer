#!/bin/bash

set -e
set -o pipefail
SECONDS=0

while getopts ":dpa:snt:xbc:i:1Tuhewg" opt; do
  case "${opt}" in
    d )
        export BUILD_TARGET="deps"
        ;;
    p )
        export PACK_DEPS="1"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="slicer"
        ;;
    n )
        export NIGHTLY_BUILD="1"
        ;;
    t )
        export OSX_DEPLOYMENT_TARGET="$OPTARG"
        ;;
    x )
        export SLICER_CMAKE_GENERATOR="Ninja Multi-Config"
        export SLICER_BUILD_TARGET="all"
        export DEPS_CMAKE_GENERATOR="Ninja"
        ;;
    b )
        export BUILD_ONLY="1"
        ;;
    c )
        export BUILD_CONFIG="$OPTARG"
        ;;
    i )
        export CMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH:+$CMAKE_IGNORE_PREFIX_PATH;}$OPTARG"
        ;;
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    T )
        export BUILD_TESTS="1"
        ;;
    u )
        export BUILD_TARGET="universal"
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64 or universal)"
        echo "   -s: Build slicer only"
        echo "   -u: Build universal app only (requires existing arm64 and x86_64 app bundles)"
        echo "   -n: Nightly build"
        echo "   -t: Specify minimum version of the target platform, default is 11.3"
        echo "   -x: Use Ninja Multi-Config CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -i: Add a prefix to ignore during CMake dependency discovery (repeatable), defaults to /opt/local:/usr/local:/opt/homebrew"
        echo "   -1: Use single job for building"
        echo "   -T: Build and run tests"
        echo "   -e: Elegoo internal testing mode"
        echo "   -w: Download web dependencies"
        echo "   -g: Upload debug symbols (dSYM) to Sentry"
        exit 0
        ;;
    * )
        ;;
  esac
done

# Set defaults

if [ -z "$ARCH" ]; then
    ARCH="$(uname -m)"
    export ARCH
fi

if [ -z "$BUILD_CONFIG" ]; then
  export BUILD_CONFIG="Release"
fi

if [ -z "$BUILD_TARGET" ]; then
  export BUILD_TARGET="all"
fi

if [ -z "$SLICER_CMAKE_GENERATOR" ]; then
  export SLICER_CMAKE_GENERATOR="Xcode"
fi

if [ -z "$SLICER_BUILD_TARGET" ]; then
  export SLICER_BUILD_TARGET="ALL_BUILD"
fi

if [ -z "$DEPS_CMAKE_GENERATOR" ]; then
  export DEPS_CMAKE_GENERATOR="Unix Makefiles"
fi

if [ -z "$OSX_DEPLOYMENT_TARGET" ]; then
  export OSX_DEPLOYMENT_TARGET="11.3"
fi

if [ -z "$ELEGOO_INTERNAL_TESTING" ]; then
  export ELEGOO_INTERNAL_TESTING="0"
fi

if [ -z "$CMAKE_IGNORE_PREFIX_PATH" ]; then
  export CMAKE_IGNORE_PREFIX_PATH="/opt/local:/usr/local:/opt/homebrew"
fi

CMAKE_VERSION=$(cmake --version | head -1 | sed 's/[^0-9]*\([0-9]*\).*/\1/')
if [ "$CMAKE_VERSION" -ge 4 ] 2>/dev/null; then
  export CMAKE_POLICY_VERSION_MINIMUM=3.5
  export CMAKE_POLICY_COMPAT="-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
  echo "Detected CMake 4.x, adding compatibility flag (env + cmake arg)"
else
  export CMAKE_POLICY_COMPAT=""
fi

echo "Build params:"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo " - ELEGOO_INTERNAL_TESTING: $ELEGOO_INTERNAL_TESTING"
echo " - DOWNLOAD_WEB: ${DOWNLOAD_WEB:-0}"
echo " - SENTRY_UPLOAD: ${SENTRY_UPLOAD:-0}"
echo " - CMAKE_IGNORE_PREFIX_PATH: $CMAKE_IGNORE_PREFIX_PATH"
echo

# Download web dependencies if requested
if [ "1" == "$DOWNLOAD_WEB" ]; then
    echo "============================================================================"
    echo "                     Downloading Web Dependencies"
    echo "============================================================================"
    if [ "$ELEGOO_INTERNAL_TESTING" == "1" ]; then
        TEST_PARAM="test"
        echo "[INFO] Downloading INTERNAL TESTING web dependencies..."
    else
        TEST_PARAM=""
        echo "[INFO] Downloading RELEASE web dependencies..."
    fi
    echo

    ./scripts/download_web_dep.sh $TEST_PARAM
    if [ $? -ne 0 ]; then
        echo
        echo "[ERROR] Download web dependencies failed. Exiting."
        exit 1
    fi
    echo
    echo "[OK] Web dependencies downloaded successfully"
    echo "============================================================================"
    echo
else
    echo
    echo "[INFO] Skipping web dependencies download, use '-w' parameter to enable"
    echo
fi

# if which -s brew; then
# 	brew --prefix libiconv
# 	brew --prefix zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:$(brew --prefix zstd)/lib/
# elif which -s port; then
# 	port install libiconv
# 	port install zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:/opt/local/lib
# else
# 	echo "Need either brew or macports to successfully build deps"
# 	exit 1
# fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
DEPS_DIR="$PROJECT_DIR/deps"

# For Multi-config generators like Ninja and Xcode
export BUILD_DIR_CONFIG_SUBDIR="/$BUILD_CONFIG"

function build_deps() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/ElegooSlicer_dep"

            echo "Building deps..."
            (
                set -x
                mkdir -p "$DEPS"
                cd "$DEPS_BUILD_DIR"
                if [ "1." != "$BUILD_ONLY". ]; then
                    cmake "${DEPS_DIR}" \
                        -G "${DEPS_CMAKE_GENERATOR}" \
                        -DELEGOO_INTERNAL_TESTING="${ELEGOO_INTERNAL_TESTING}" \
                        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                        -DCMAKE_OSX_ARCHITECTURES:STRING="${_ARCH}" \
                        -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}" \
                        -DCMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH}" \
                        ${CMAKE_POLICY_COMPAT}
                fi
                cmake --build . --config "$BUILD_CONFIG" --target deps --parallel
            )
        fi
    done
}

function upload_pdb() {
    echo "============================================================================"
    echo "                     Uploading Debug Symbols to Sentry"
    echo "============================================================================"
    for _ARCH in x86_64 arm64; do
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then
            _BUILD_DIR="$PROJECT_DIR/build/$_ARCH/src/$BUILD_CONFIG"
            _INSTALL_INI="$PROJECT_DIR/build/$_ARCH/install.ini"
            _SENTRY_VER="unknown"
            if [ -f "$_INSTALL_INI" ]; then
                _SENTRY_VER="$(grep '^ELEGOOSLICER_VERSION=' "$_INSTALL_INI" | cut -d= -f2- | tr -d '\r')"
            fi
            if [ -d "$_BUILD_DIR" ]; then
                echo "[INFO] Uploading symbols from: $_BUILD_DIR (release: elegoo-slicer@${_SENTRY_VER})"
                python3 "$PROJECT_DIR/scripts/upload_sentry_pdbs.py" "$_BUILD_DIR" "$_SENTRY_VER"
            else
                echo "[WARNING] Build directory not found: $_BUILD_DIR"
            fi
        fi
    done
    echo "============================================================================"
    echo
}

function pack_deps() {
    echo "Packing deps..."
    (
        set -x
        cd "$DEPS_DIR"
        tar -zcvf "ElegooSlicer_dep_mac_${ARCH}_$(date +"%Y%m%d").tar.gz" "build"
    )
}

function build_slicer() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/ElegooSlicer_dep"

            echo "Building slicer for $_ARCH..."
            (
                set -x
            mkdir -p "$PROJECT_BUILD_DIR"
            cd "$PROJECT_BUILD_DIR"
            if [ "1." != "$BUILD_ONLY". ]; then
                cmake "${PROJECT_DIR}" \
                    -G "${SLICER_CMAKE_GENERATOR}" \
                    -DORCA_TOOLS=ON \
                    -DCMAKE_PREFIX_PATH="$DEPS/usr/local" \
                    -DCMAKE_INSTALL_PREFIX="$PWD/ElegooSlicer" \
                    ${ORCA_UPDATER_SIG_KEY:+-DORCA_UPDATER_SIG_KEY="$ORCA_UPDATER_SIG_KEY"} \
                    ${BUILD_TESTS:+-DBUILD_TESTS=ON} \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_OSX_ARCHITECTURES="${_ARCH}" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}" \
                    -DELEGOO_INTERNAL_TESTING="${ELEGOO_INTERNAL_TESTING}" \
                    -DELEGOO_SENTRY_SYMBOLS="$([ "${SENTRY_UPLOAD:-0}" = "1" ] && echo ON || echo OFF)" \
                    -DCMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH}" \
                    ${CMAKE_POLICY_COMPAT}
            fi
            cmake --build . --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET" --parallel
        )

        if [ "1." == "$BUILD_TESTS". ]; then
            echo "Running tests for $_ARCH..."
            (
                set -x
                cd "$PROJECT_BUILD_DIR"
                ctest --build-config "$BUILD_CONFIG" --output-on-failure
            )
        fi

        echo "Verify localization with gettext..."
        (
            cd "$PROJECT_DIR"
            ./scripts/run_gettext.sh
        )

        echo "Fix macOS app package..."
        (
            cd "$PROJECT_BUILD_DIR"
            mkdir -p ElegooSlicer
            cd ElegooSlicer
            # remove previously built app
            rm -rf ./ElegooSlicer.app
            # fully copy newly built app
            cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/ElegooSlicer.app" ./ElegooSlicer.app
            # crashpad_handler must live beside the main binary (Contents/MacOS/crashpad/)
            if [ -f "../src$BUILD_DIR_CONFIG_SUBDIR/crashpad/crashpad_handler" ]; then
                mkdir -p ./ElegooSlicer.app/Contents/MacOS/crashpad
                cp -f "../src$BUILD_DIR_CONFIG_SUBDIR/crashpad/crashpad_handler" \
                    ./ElegooSlicer.app/Contents/MacOS/crashpad/crashpad_handler
                chmod +x ./ElegooSlicer.app/Contents/MacOS/crashpad/crashpad_handler
            fi
            # fix resources
            resources_path=$(readlink ./ElegooSlicer.app/Contents/Resources)
            rm ./ElegooSlicer.app/Contents/Resources
            cp -R "$resources_path" ./ElegooSlicer.app/Contents/Resources
            # delete .DS_Store file
            find ./ElegooSlicer.app/ -name '.DS_Store' -delete

            # Copy ElegooSlicer_profile_validator.app if it exists
            if [ -f "../src$BUILD_DIR_CONFIG_SUBDIR/ElegooSlicer_profile_validator.app/Contents/MacOS/ElegooSlicer_profile_validator" ]; then
                echo "Copying ElegooSlicer_profile_validator.app..."
                rm -rf ./ElegooSlicer_profile_validator.app
                cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/ElegooSlicer_profile_validator.app" ./ElegooSlicer_profile_validator.app
                # delete .DS_Store file
                find ./ElegooSlicer_profile_validator.app/ -name '.DS_Store' -delete
            fi
        )

        # extract version
        # export ver=$(grep '^#define SoftFever_VERSION' ../src/libslic3r/libslic3r_version.h | cut -d ' ' -f3)
        # ver="_V${ver//\"}"
        # echo $PWD
        # if [ "1." != "$NIGHTLY_BUILD". ];
        # then
        #     ver=${ver}_dev
        # fi

        # zip -FSr ElegooSlicer${ver}_Mac_${_ARCH}.zip ElegooSlicer.app

    fi
    done
}

function lipo_dir() {
    local universal_dir="$1"
    local x86_64_dir="$2"

    # Find all Mach-O files in the universal (arm64-based) copy and lipo them
    while IFS= read -r -d '' f; do
        local rel="${f#"$universal_dir"/}"
        local x86="$x86_64_dir/$rel"
        if [ -f "$x86" ]; then
            echo "  lipo: $rel"
            lipo -create "$f" "$x86" -output "$f.tmp"
            mv "$f.tmp" "$f"
        else
            echo "  warning: no x86_64 counterpart for $rel, keeping arm64 only"
        fi
    done < <(find "$universal_dir" -type f -print0 | while IFS= read -r -d '' candidate; do
        if file "$candidate" | grep -q "Mach-O"; then
            printf '%s\0' "$candidate"
        fi
    done)
}

function build_universal() {
    echo "Building universal binary..."

    PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
    ARM64_APP="$PROJECT_DIR/build/arm64/ElegooSlicer/ElegooSlicer.app"
    X86_64_APP="$PROJECT_DIR/build/x86_64/ElegooSlicer/ElegooSlicer.app"

    mkdir -p "$PROJECT_BUILD_DIR/ElegooSlicer"
    UNIVERSAL_APP="$PROJECT_BUILD_DIR/ElegooSlicer/ElegooSlicer.app"
    rm -rf "$UNIVERSAL_APP"
    cp -R "$ARM64_APP" "$UNIVERSAL_APP"

    echo "Creating universal binaries for ElegooSlicer.app..."
    lipo_dir "$UNIVERSAL_APP" "$X86_64_APP"
    echo "Universal ElegooSlicer.app created at $UNIVERSAL_APP"

    # Create universal binary for profile validator if it exists
    ARM64_VALIDATOR="$PROJECT_DIR/build/arm64/ElegooSlicer/ElegooSlicer_profile_validator.app"
    X86_64_VALIDATOR="$PROJECT_DIR/build/x86_64/ElegooSlicer/ElegooSlicer_profile_validator.app"
    if [ -d "$ARM64_VALIDATOR" ] && [ -d "$X86_64_VALIDATOR" ]; then
        echo "Creating universal binaries for ElegooSlicer_profile_validator.app..."
        UNIVERSAL_VALIDATOR_APP="$PROJECT_BUILD_DIR/ElegooSlicer/ElegooSlicer_profile_validator.app"
        rm -rf "$UNIVERSAL_VALIDATOR_APP"
        cp -R "$ARM64_VALIDATOR" "$UNIVERSAL_VALIDATOR_APP"
        lipo_dir "$UNIVERSAL_VALIDATOR_APP" "$X86_64_VALIDATOR"
        echo "Universal ElegooSlicer_profile_validator.app created at $UNIVERSAL_VALIDATOR_APP"
    fi
}

case "${BUILD_TARGET}" in
    all)
        build_deps
        build_slicer
        ;;
    deps)
        build_deps
        ;;
    slicer)
        build_slicer
        ;;
    universal)
        build_universal
        ;;
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, universal, all."
        exit 1
        ;;
esac

if [ "1" == "${SENTRY_UPLOAD}" ] && [ "$BUILD_TARGET" != "deps" ]; then
    upload_pdb
fi

if [ "$ARCH" = "universal" ] && { [ "$BUILD_TARGET" = "all" ] || [ "$BUILD_TARGET" = "slicer" ]; }; then
    build_universal
fi

if [ "1." == "$PACK_DEPS". ]; then
    pack_deps
fi

elapsed=$SECONDS
printf "\nBuild completed in %dh %dm %ds\n" $((elapsed/3600)) $((elapsed%3600/60)) $((elapsed%60))
