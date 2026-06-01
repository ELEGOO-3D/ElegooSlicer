#!/bin/bash

set -e
set -o pipefail

while getopts ":dpa:snt:xbc:1ehwg" opt; do
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
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    e )
        export ELEGOO_INTERNAL_TESTING="1"
        ;;
    w )
        export DOWNLOAD_WEB="1"
        ;;
    g )
        export SENTRY_UPLOAD="1"
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64 or universal)"
        echo "   -s: Build slicer only"
        echo "   -n: Nightly build"
        echo "   -t: Specify minimum version of the target platform, default is 11.3"
        echo "   -x: Use Ninja Multi-Config CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -1: Use single job for building"
        echo "   -e: Test environment"
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
echo "Build params:"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo " - ELEGOO_INTERNAL_TESTING: $ELEGOO_INTERNAL_TESTING"
echo " - DOWNLOAD_WEB: ${DOWNLOAD_WEB:-0}"
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
DEPS_BUILD_DIR="$DEPS_DIR/build/$ARCH"
DEPS="$DEPS_BUILD_DIR/ElegooSlicer_deps"

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
                        -DDESTDIR="$DEPS" \
                        -DELEGOO_INTERNAL_TESTING="${ELEGOO_INTERNAL_TESTING}" \
                        -DOPENSSL_ARCH="darwin64-${_ARCH}-cc" \
                        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                        -DCMAKE_OSX_ARCHITECTURES:STRING="${_ARCH}" \
                        -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
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
            if [ -d "$_BUILD_DIR" ]; then
                echo "[INFO] Uploading symbols from: $_BUILD_DIR"
                python3 "$PROJECT_DIR/scripts/upload_sentry_pdbs.py" "$_BUILD_DIR"
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
                    -DBBL_RELEASE_TO_PUBLIC=1 \
                    -DORCA_TOOLS=ON \
                    -DCMAKE_PREFIX_PATH="$DEPS/usr/local" \
                    -DCMAKE_INSTALL_PREFIX="$PWD/ElegooSlicer" \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_MACOSX_RPATH=ON \
                    -DCMAKE_INSTALL_RPATH="${DEPS}/usr/local" \
                    -DCMAKE_MACOSX_BUNDLE=ON \
                    -DCMAKE_OSX_ARCHITECTURES="${_ARCH}" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}" \
                    -DELEGOO_INTERNAL_TESTING="${ELEGOO_INTERNAL_TESTING}"
            fi
            cmake --build . --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET" --parallel 
        )

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

function build_universal() {
    echo "Building universal binary..."

    PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
    
    # Create universal binary
    echo "Creating universal binary..."
    # PROJECT_BUILD_DIR="$PROJECT_DIR/build_Universal"
    mkdir -p "$PROJECT_BUILD_DIR/ElegooSlicer"
    UNIVERSAL_APP="$PROJECT_BUILD_DIR/ElegooSlicer/ElegooSlicer.app"
    rm -rf "$UNIVERSAL_APP"
    cp -R "$PROJECT_DIR/build/arm64/ElegooSlicer/ElegooSlicer.app" "$UNIVERSAL_APP"
    
    # Get the binary path inside the .app bundle
    BINARY_PATH="Contents/MacOS/ElegooSlicer"
    
    # Create universal binary using lipo
    lipo -create \
        "$PROJECT_DIR/build/x86_64/ElegooSlicer/ElegooSlicer.app/$BINARY_PATH" \
        "$PROJECT_DIR/build/arm64/ElegooSlicer/ElegooSlicer.app/$BINARY_PATH" \
        -output "$UNIVERSAL_APP/$BINARY_PATH"
        
    echo "Universal binary created at $UNIVERSAL_APP"
    
    # Create universal binary for profile validator if it exists
    if [ -f "$PROJECT_DIR/build/arm64/ElegooSlicer/ElegooSlicer_profile_validator.app/Contents/MacOS/ElegooSlicer_profile_validator" ] && \
       [ -f "$PROJECT_DIR/build/x86_64/ElegooSlicer/ElegooSlicer_profile_validator.app/Contents/MacOS/ElegooSlicer_profile_validator" ]; then
        echo "Creating universal binary for ElegooSlicer_profile_validator..."
        UNIVERSAL_VALIDATOR_APP="$PROJECT_BUILD_DIR/ElegooSlicer/ElegooSlicer_profile_validator.app"
        rm -rf "$UNIVERSAL_VALIDATOR_APP"
        cp -R "$PROJECT_DIR/build/arm64/ElegooSlicer/ElegooSlicer_profile_validator.app" "$UNIVERSAL_VALIDATOR_APP"
        
        # Get the binary path inside the profile validator .app bundle
        VALIDATOR_BINARY_PATH="Contents/MacOS/ElegooSlicer_profile_validator"
        
        # Create universal binary using lipo
        lipo -create \
            "$PROJECT_DIR/build/x86_64/ElegooSlicer/ElegooSlicer_profile_validator.app/$VALIDATOR_BINARY_PATH" \
            "$PROJECT_DIR/build/arm64/ElegooSlicer/ElegooSlicer_profile_validator.app/$VALIDATOR_BINARY_PATH" \
            -output "$UNIVERSAL_VALIDATOR_APP/$VALIDATOR_BINARY_PATH"
            
        echo "Universal binary for ElegooSlicer_profile_validator created at $UNIVERSAL_VALIDATOR_APP"
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
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, all."
        exit 1
        ;;
esac

if [ "1" == "${SENTRY_UPLOAD}" ] && [ "$BUILD_TARGET" != "deps" ]; then
    upload_pdb
fi

if [ "$ARCH" = "universal" ] && [ "$BUILD_TARGET" != "deps" ]; then
    build_universal
fi

if [ "1." == "$PACK_DEPS". ]; then
    pack_deps
fi
