#!/bin/bash
set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: $0 [--clean] [-j N] <build_mode>"
    echo ""
    echo "Runs cmake and builds AtomVM for specified platform and mode. Copies artifacts to out/ directory."
    echo "Wasm artifacts are additionally compressed."
    echo "You can pass CMake flags via 'AVM_CMAKE_OPTS', example below."
    echo ""
    echo "Options:"
    echo "  --clean       - Run ninja clean before building"
    echo "  -j N          - Number of parallel jobs for ninja (default: auto)"
    echo "Build modes:"
    echo "  debug-unix    - Debug build for Unix platforms"
    echo "  debug-wasm    - Debug build for WebAssembly"
    echo "  release-unix  - Release build for Unix platforms"
    echo "  release-wasm  - Release build for WebAssembly"
    echo ""
    echo "Examples:"
    echo "Run debug unix build:"
    echo "  ./build-fission debug-unix"
    echo "Run wasm debug build from scratch:"
    echo "  ./build-fission debug-wasm --clean"
    echo "Run single-threaded build (useful for memory-constrained environments):"
    echo "  ./build-fission -j 1 debug-wasm"
    echo "Pass additional flags to build:"
    echo "  AVM_CMAKE_OPTS='-DAVM_ENABLE_TRACE_RAISE=1' ./build-fission debug-wasm"
    exit 1
}

log() {
    echo -e "${GREEN}[BUILD]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

print_args() {
    local args="$*"
    echo -e "${args// /\\n\\t}"
}

check_tools() {
    local missing_tools=()

    if ! command -v cmake &> /dev/null; then
        missing_tools+=("cmake")
    fi
    if ! command -v ninja &> /dev/null; then
        missing_tools+=("ninja")
    fi
    if ! command -v emcmake &> /dev/null; then
        missing_tools+=("emcmake")
    fi
    if ! command -v emmake &> /dev/null; then
        missing_tools+=("emmake")
    fi

    # Only log/error if tools are missing
    if [ ${#missing_tools[@]} -gt 0 ]; then
        local tools_list=$(IFS=", "; echo "${missing_tools[*]}")
        error "Missing required tools: $tools_list. Please install them first."
    fi
}

build_unix() {
    local build_type=$1
    local clean_flag=$2
    local jobs_flag=$3
    local repo_root="$(pwd)"

    local build_dir="${repo_root}/out/cmake-unix"
    local cmake_args="-DAVM_BUILD_RUNTIME_ONLY=ON -GNinja"
    if [[ "${build_type}" = "release" ]]; then
        cmake_args="${cmake_args} -DCMAKE_BUILD_TYPE=Release -DAVM_RELEASE=ON"
    else
        cmake_args="${cmake_args} -DCMAKE_BUILD_TYPE=Debug"
    fi

    mkdir -p "${build_dir}"
    pushd "${build_dir}" >/dev/null

    [ "${clean_flag}" ] && (rm CmakeCache.txt || true)

    log "Running cmake for unix with args: \n\t$(print_args ${cmake_args})\nin '$(pwd)'."
    local cmake_out="$(cmake ${repo_root} ${cmake_args} 2>&1)"
    [[ $? -ne 0 ]] && error "cmake failed:\n${cmake_out}"

    [ "${clean_flag}" ] && ninja ${jobs_flag} clean
    ninja ${jobs_flag} AtomVM
    popd >/dev/null

    cp "${build_dir}/src/AtomVM" out/
}

build_wasm() {
    local build_type=$1
    local clean_flag=$2
    local jobs_flag=$3
    local repo_root="$(pwd)"

    local build_dir="${repo_root}/out/cmake-wasm"
    local cmake_args="-DAVM_EMSCRIPTEN_ENV=web -GNinja"
    if [[ "${build_type}" = "release" ]]; then
        cmake_args="${cmake_args} -DCMAKE_BUILD_TYPE=Release -DAVM_RELEASE=ON"
    else
        cmake_args="${cmake_args} -DCMAKE_BUILD_TYPE=Debug"
    fi

    mkdir -p "${build_dir}"
    pushd "${build_dir}" >/dev/null

    [ "${clean_flag}" ] && (rm CmakeCache.txt || true)
    log "Running cmake for wasm with args: \n\t$(print_args ${cmake_args})\nin '$(pwd)'."
    local cmake_out="$(emcmake cmake ${repo_root}/src/platforms/emscripten ${cmake_args} 2>&1)"
    [[ $? -ne 0 ]] && error "emcmake failed:\n${cmake_out}"

    [ "${clean_flag}" ] && ninja ${jobs_flag} clean
    log "Running 'ninja ${jobs_flag} AtomVM'."
    emmake ninja ${jobs_flag} AtomVM
    popd >/dev/null

    log "Compressing."
    for artifact in "AtomVM.mjs" "AtomVM.wasm"; do
        [[ ! -f "${build_dir}/src/${artifact}" ]] && error "Required WASM artifact ${artifact} not found in out/"
        cp "${build_dir}/src/${artifact}" "${repo_root}/out/"
        gzip -c "${repo_root}/out/${artifact}" > "${repo_root}/out/${artifact}.gz" || error "Failed to compress ${artifact}."
    done
}

main() {
    local CLEAN_FLAG=""
    local BUILD_MODE=""
    local JOBS_FLAG=""

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --clean)
                CLEAN_FLAG="true"
                shift
                ;;
            -j)
                if [[ -z "$2" || "$2" == -* ]]; then
                    error "-j requires a number argument"
                fi
                JOBS_FLAG="-j$2"
                shift 2
                ;;
            *)
                if [[ -z "${BUILD_MODE}" ]]; then
                    BUILD_MODE=$1
                    shift
                else
                    usage
                fi
                ;;
        esac
    done

    [[ -z "${BUILD_MODE}" ]] && usage

    # Get script directory and change to project root
    local SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    cd "${SCRIPT_DIR}"

    check_tools
    case ${BUILD_MODE} in
        debug-unix)
            build_unix "debug" "${CLEAN_FLAG}" "${JOBS_FLAG}"
            ;;
        release-unix)
            build_unix "release" "${CLEAN_FLAG}" "${JOBS_FLAG}"
            ;;
        debug-wasm)
            build_wasm "debug" "${CLEAN_FLAG}" "${JOBS_FLAG}"
            ;;
        release-wasm)
            build_wasm "release" "${CLEAN_FLAG}" "${JOBS_FLAG}"
            ;;
        *)
            usage
            ;;
    esac

    log "Finished."
}

main "$@"
