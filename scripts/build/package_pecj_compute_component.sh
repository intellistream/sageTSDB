#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAGE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERSION="$(sed -n -E 's/^project\(sageTSDB VERSION ([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' "${SAGE_ROOT}/CMakeLists.txt" | head -n 1)"
PECJ_DIR="$(cd "${SAGE_ROOT}/.." && pwd)/PECJ"
PECJ_LIBRARY=""
TORCH_CMAKE_DIR="${Torch_DIR:-}"
BUILD_ROOT="${SAGE_ROOT}/build/pecj_component"
OUTPUT_ROOT="${SAGE_ROOT}/dist"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
BUNDLE_RUNTIME=1
FORCE=0

usage() {
    printf '%s\n' \
        "Usage: package_pecj_compute_component.sh [options]" \
        "" \
        "Options:" \
        "  --pecj-dir DIR       PECJ source tree (default: ../PECJ)" \
        "  --pecj-library FILE  Reuse a pre-hardened libIntelliStreamOoOJoin.so" \
        "  --torch-dir DIR      Directory containing TorchConfig.cmake" \
        "  --build-dir DIR      Out-of-tree build workspace" \
        "  --output-dir DIR     Parent directory for package and tarball" \
        "  --jobs N             Parallel build jobs" \
        "  --thin               Do not bundle Torch runtime libraries" \
        "  --force              Replace an existing package of the same name" \
        "  --help               Show this help"
}

# Build a relocatable PECJ + sageTSDB compute component delivery package.
#
# Usage:
#   package_pecj_compute_component.sh [options]
#
# Options:
#   --pecj-dir DIR       PECJ source tree (default: ../PECJ)
#   --pecj-library FILE  Reuse a pre-hardened libIntelliStreamOoOJoin.so
#   --torch-dir DIR      Directory containing TorchConfig.cmake
#   --build-dir DIR      Out-of-tree build workspace
#   --output-dir DIR     Parent directory for the package and tarball
#   --jobs N             Parallel build jobs
#   --thin               Do not bundle the Torch runtime libraries
#   --force              Replace an existing package with the same name
#   --help               Show this help
#
# The default package contains bin/, include/, lib/, share/, SHA256SUMS, and a
# compressed tarball beside the package directory. The script runs the installed
# CLI self-test and builds external C and C++ consumers before declaring success.

while [[ $# -gt 0 ]]; do
    case "$1" in
        --pecj-dir)
            PECJ_DIR="$2"
            shift 2
            ;;
        --pecj-library)
            PECJ_LIBRARY="$2"
            shift 2
            ;;
        --torch-dir)
            TORCH_CMAKE_DIR="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_ROOT="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_ROOT="$2"
            shift 2
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --thin)
            BUNDLE_RUNTIME=0
            shift
            ;;
        --force)
            FORCE=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 64
            ;;
    esac
done

for command in cc cmake c++ getconf ldd lscpu nm readelf sha256sum strings tar uname; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Required command not found: ${command}" >&2
        exit 69
    fi
done

if [[ -z "${VERSION}" ]]; then
    echo "Could not determine component version from CMakeLists.txt" >&2
    exit 65
fi

if [[ ! -d "${PECJ_DIR}/include" ]]; then
    echo "PECJ source tree not found: ${PECJ_DIR}" >&2
    exit 66
fi

if [[ -z "${TORCH_CMAKE_DIR}" ]]; then
    if [[ -f "${PECJ_DIR}/build/CMakeCache.txt" ]]; then
        TORCH_CMAKE_DIR="$(sed -n 's/^Torch_DIR:PATH=//p' "${PECJ_DIR}/build/CMakeCache.txt" | head -n 1)"
    fi
    if [[ -z "${TORCH_CMAKE_DIR}" ]]; then
        if ! TORCH_CMAKE_DIR="$(python3 -c 'import torch; print(torch.utils.cmake_prefix_path)' 2>/dev/null)"; then
            echo "Torch was not found. Pass --torch-dir /path/to/torch/share/cmake/Torch." >&2
            exit 69
        fi
    fi
fi
if [[ ! -f "${TORCH_CMAKE_DIR}/TorchConfig.cmake" ]]; then
    echo "TorchConfig.cmake not found in: ${TORCH_CMAKE_DIR}" >&2
    exit 66
fi

TORCH_ROOT="$(cd "${TORCH_CMAKE_DIR}/../../.." && pwd)"
TORCH_LIB_DIR="${TORCH_ROOT}/lib"
PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
PACKAGE_NAME="sage-tsdb-pecj-component-${VERSION}-${PLATFORM}"
STAGE_DIR="${OUTPUT_ROOT}/${PACKAGE_NAME}"
ARCHIVE="${OUTPUT_ROOT}/${PACKAGE_NAME}.tar.gz"

if [[ -e "${STAGE_DIR}" || -e "${ARCHIVE}" ]]; then
    if [[ ${FORCE} -ne 1 ]]; then
        echo "Package already exists; pass --force to replace it: ${STAGE_DIR}" >&2
        exit 73
    fi
    cmake -E remove_directory "${STAGE_DIR}"
    cmake -E rm -f "${ARCHIVE}"
fi

mkdir -p "${BUILD_ROOT}" "${OUTPUT_ROOT}"

if [[ -z "${PECJ_LIBRARY}" ]]; then
    PECJ_BUILD_DIR="${BUILD_ROOT}/pecj"
    cmake -E remove_directory "${PECJ_BUILD_DIR}"
    cmake -S "${PECJ_DIR}" -B "${PECJ_BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
        -DCMAKE_INSTALL_RPATH="\$ORIGIN" \
        -DCMAKE_PROJECT_INCLUDE="${SAGE_ROOT}/cmake/PECJDeliveryHardening.cmake" \
        -DPECJ_DELIVERY_TORCH_ROOT="${TORCH_ROOT}" \
        -DENABLE_UNIT_TESTS=OFF \
        -DTorch_DIR="${TORCH_CMAKE_DIR}"
    cmake --build "${PECJ_BUILD_DIR}" \
        --target IntelliStreamOoOJoin --parallel "${JOBS}"
    PECJ_LIBRARY="${PECJ_BUILD_DIR}/libIntelliStreamOoOJoin.so"
fi
if [[ ! -f "${PECJ_LIBRARY}" ]]; then
    echo "PECJ library not found: ${PECJ_LIBRARY}" >&2
    exit 66
fi

SAGE_BUILD_DIR="${BUILD_ROOT}/sageTSDB"
cmake -S "${SAGE_ROOT}" -B "${SAGE_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${STAGE_DIR}" \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_PLUGINS=OFF \
    -DSAGE_TSDB_ENABLE_SRTFD=OFF \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DSAGE_TSDB_BUILD_COMPUTE_CLI=ON \
    -DSAGE_TSDB_BUILD_PECJ_C_API=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_FULL_INTEGRATION=ON \
    -DPECJ_DIR="${PECJ_DIR}" \
    -DPECJ_LIBRARY="${PECJ_LIBRARY}" \
    -DCMAKE_CXX_FLAGS="-ffile-prefix-map=${SAGE_ROOT}=sageTSDB -ffile-prefix-map=${PECJ_DIR}=PECJ -ffile-prefix-map=${TORCH_ROOT}=torch -ffile-prefix-map=${BUILD_ROOT}=build" \
    -DTorch_DIR="${TORCH_CMAKE_DIR}"
cmake --build "${SAGE_BUILD_DIR}" --parallel "${JOBS}"
cmake --install "${SAGE_BUILD_DIR}"

mkdir -p "${STAGE_DIR}/lib" "${STAGE_DIR}/share/licenses"
cp "${SAGE_ROOT}/docs/compute/PECJ_COMPUTE_COMPONENT_README.md" "${STAGE_DIR}/README.md"
cp -L "${PECJ_LIBRARY}" "${STAGE_DIR}/lib/$(basename "${PECJ_LIBRARY}")"

if [[ ${BUNDLE_RUNTIME} -eq 1 ]]; then
    declare -a dependency_queue=(
        "${STAGE_DIR}/bin/sage_tsdb_pecj"
        "${STAGE_DIR}/lib/libsage_tsdb_pecj_c.so"
        "${STAGE_DIR}/lib/libsage_tsdb_compute.so"
        "${STAGE_DIR}/lib/$(basename "${PECJ_LIBRARY}")"
    )
    declare -A scanned=()
    queue_index=0
    while [[ ${queue_index} -lt ${#dependency_queue[@]} ]]; do
        current="${dependency_queue[${queue_index}]}"
        queue_index=$((queue_index + 1))
        [[ -n "${scanned[${current}]:-}" ]] && continue
        scanned["${current}"]=1

        while read -r dependency; do
            [[ -z "${dependency}" ]] && continue
            case "${dependency}" in
                "${TORCH_ROOT}"/*)
                    destination="${STAGE_DIR}/lib/$(basename "${dependency}")"
                    if [[ ! -f "${destination}" ]]; then
                        cp -L "${dependency}" "${destination}"
                    fi
                    dependency_queue+=("${destination}")
                    ;;
            esac
        done < <(
            LD_LIBRARY_PATH="${STAGE_DIR}/lib:${TORCH_LIB_DIR}:$(dirname "${PECJ_LIBRARY}"):${LD_LIBRARY_PATH:-}" \
                ldd "${current}" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// {print $3}'
        )
    done
fi

for license in "${PECJ_DIR}/LICENSE" "${PECJ_DIR}/LICENSE.txt"; do
    if [[ -f "${license}" ]]; then
        cp "${license}" "${STAGE_DIR}/share/licenses/PECJ-$(basename "${license}")"
    fi
done
if [[ ${BUNDLE_RUNTIME} -eq 1 ]]; then
    for license in \
        "${TORCH_ROOT}/LICENSE" \
        "${TORCH_ROOT}/../"torch-*.dist-info/LICENSE; do
        if [[ -f "${license}" ]]; then
            cp "${license}" "${STAGE_DIR}/share/licenses/Torch-LICENSE"
            break
        fi
    done
fi

collect_elf_versions() {
    for binary in "${STAGE_DIR}/bin/"* "${STAGE_DIR}/lib/"*; do
        if [[ -f "${binary}" ]]; then
            readelf --version-info "${binary}" 2>/dev/null || true
        fi
    done
}

validate_delivery_elfs() {
    local binary dynamic_path entry missing leaked_path
    local failed=0

    for binary in "${STAGE_DIR}/bin/"* "${STAGE_DIR}/lib/"*; do
        [[ -f "${binary}" ]] || continue

        while IFS= read -r dynamic_path; do
            IFS=':' read -ra entries <<< "${dynamic_path}"
            for entry in "${entries[@]}"; do
                if [[ "${entry}" == /* ]]; then
                    echo "Absolute ELF runtime path in ${binary}: ${entry}" >&2
                    failed=1
                fi
            done
        done < <(
            readelf -d "${binary}" 2>/dev/null \
                | sed -n -E 's/.*\((RPATH|RUNPATH)\).*\[(.*)\]/\2/p'
        )

        leaked_path="$(
            strings -a "${binary}" \
                | grep -F -e "${SAGE_ROOT}" -e "${PECJ_DIR}" -e "${TORCH_ROOT}" \
                || true
        )"
        if [[ -n "${leaked_path}" ]]; then
            echo "Build-host path embedded in ${binary}:" >&2
            printf '%s\n' "${leaked_path}" | head -n 5 >&2
            failed=1
        fi

        if [[ ${BUNDLE_RUNTIME} -eq 1 ]]; then
            missing="$(env -u LD_LIBRARY_PATH ldd "${binary}" 2>&1 | sed -n '/not found/p')"
            if [[ -n "${missing}" ]]; then
                echo "Unresolved dependency in ${binary}:" >&2
                printf '%s\n' "${missing}" >&2
                failed=1
            fi
        fi
    done

    if [[ ${failed} -ne 0 ]]; then
        return 1
    fi
}

validate_c_api_symbols() {
    local library="${STAGE_DIR}/lib/libsage_tsdb_pecj_c.so"
    local actual expected unexpected

    if [[ ! -f "${library}" ]]; then
        echo "C API library is missing: ${library}" >&2
        return 1
    fi

    actual="$(
        nm -D --defined-only "${library}" \
            | awk '{print $3}' \
            | sed 's/@.*//' \
            | grep '^sage_pecj_' \
            | sort -u
    )"
    expected="$(printf '%s\n' \
        sage_pecj_abi_version \
        sage_pecj_clear \
        sage_pecj_component_version \
        sage_pecj_config_v1_init \
        sage_pecj_create \
        sage_pecj_destroy \
        sage_pecj_execute \
        sage_pecj_insert_r \
        sage_pecj_insert_s \
        sage_pecj_last_error \
        sage_pecj_result_v1_init | sort)"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "Unexpected C API symbol set" >&2
        diff -u <(printf '%s\n' "${expected}") <(printf '%s\n' "${actual}") >&2 || true
        return 1
    fi

    unexpected="$(
        nm -D --defined-only "${library}" \
            | awk '{print $3}' \
            | sed 's/@.*//' \
            | grep -Ev '^(sage_pecj_|SAGE_TSDB_PECJ_C_)' \
            || true
    )"
    if [[ -n "${unexpected}" ]]; then
        echo "Non-C symbols exported by ${library}:" >&2
        printf '%s\n' "${unexpected}" >&2
        return 1
    fi
}

BUILD_OS="unknown"
if [[ -r /etc/os-release ]]; then
    BUILD_OS="$(. /etc/os-release; printf '%s' "${PRETTY_NAME:-${NAME:-unknown}}")"
fi
BUILD_CPU_MODEL="$(lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -n 1)"
REQUIRED_GLIBC="$(collect_elf_versions | grep -Eo 'GLIBC_[0-9]+\.[0-9]+' | sort -Vu | tail -n 1)"
REQUIRED_GLIBCXX="$(collect_elf_versions | grep -Eo 'GLIBCXX_[0-9]+\.[0-9]+(\.[0-9]+)?' | sort -Vu | tail -n 1)"
CPU_ISA="$(readelf -n "${STAGE_DIR}/bin/sage_tsdb_pecj" 2>/dev/null \
    | sed -n 's/.*x86 ISA needed: //p' | head -n 1)"
KERNEL_ABI="$(readelf -n "${STAGE_DIR}/bin/sage_tsdb_pecj" 2>/dev/null \
    | sed -n 's/.*OS: Linux, ABI: //p' | head -n 1)"

{
    printf '%s\n' \
        "PECJ + sageTSDB compute component ${VERSION}" \
        "" \
        "Build environment (informational):" \
        "  OS: ${BUILD_OS}" \
        "  Kernel: $(uname -r)" \
        "  Architecture: $(uname -m)" \
        "  CPU: ${BUILD_CPU_MODEL:-unknown}" \
        "  Logical CPUs: $(getconf _NPROCESSORS_ONLN)" \
        "  glibc: $(getconf GNU_LIBC_VERSION)" \
        "  C++ compiler: $(c++ -dumpfullversion -dumpversion)" \
        "" \
        "Target runtime requirements (derived from shipped ELF files):" \
        "  OS/architecture: Linux x86_64" \
        "  CPU ISA declared by executable: ${CPU_ISA:-unknown}" \
        "  Required glibc symbol version: ${REQUIRED_GLIBC:-unknown}" \
        "  Required libstdc++ symbol version: ${REQUIRED_GLIBCXX:-unknown}" \
        "  ELF Linux ABI tag: ${KERNEL_ABI:-unknown}" \
        "  Recommended kernel: Linux 5.4 or newer" \
        "" \
        "The build CPU and kernel are not exact target requirements. Validate on" \
        "the deployment image; Ubuntu 22.04 x86_64 with current updates is the" \
        "recommended baseline for this binary package."
} > "${STAGE_DIR}/share/doc/sageTSDB/compute/BUILD_ENVIRONMENT.txt"

validate_delivery_elfs
validate_c_api_symbols

RUNTIME_PATH="${STAGE_DIR}/lib:${TORCH_LIB_DIR}:$(dirname "${PECJ_LIBRARY}")"
SELF_TEST_RUNTIME_DIR="${BUILD_ROOT}/self-test-runtime"
cmake -E remove_directory "${SELF_TEST_RUNTIME_DIR}"
mkdir -p "${SELF_TEST_RUNTIME_DIR}"
if [[ ${BUNDLE_RUNTIME} -eq 1 ]]; then
    (cd "${SELF_TEST_RUNTIME_DIR}" && env -u LD_LIBRARY_PATH "${STAGE_DIR}/bin/sage_tsdb_pecj" --self-test)
else
    (cd "${SELF_TEST_RUNTIME_DIR}" && LD_LIBRARY_PATH="${RUNTIME_PATH}" "${STAGE_DIR}/bin/sage_tsdb_pecj" --self-test)
fi

C_CONSUMER_BUILD_DIR="${BUILD_ROOT}/c-consumer"
cmake -E remove_directory "${C_CONSUMER_BUILD_DIR}"
cmake -S "${STAGE_DIR}/share/sageTSDB/c_component_consumer" \
    -B "${C_CONSUMER_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${STAGE_DIR}"
cmake --build "${C_CONSUMER_BUILD_DIR}" --parallel "${JOBS}"
C_CONSUMER_RUNTIME_DIR="${BUILD_ROOT}/c-consumer-runtime"
cmake -E remove_directory "${C_CONSUMER_RUNTIME_DIR}"
mkdir -p "${C_CONSUMER_RUNTIME_DIR}"
if [[ ${BUNDLE_RUNTIME} -eq 1 ]]; then
    (cd "${C_CONSUMER_RUNTIME_DIR}" && env -u LD_LIBRARY_PATH "${C_CONSUMER_BUILD_DIR}/pecj_c_consumer")
else
    (cd "${C_CONSUMER_RUNTIME_DIR}" && LD_LIBRARY_PATH="${RUNTIME_PATH}" "${C_CONSUMER_BUILD_DIR}/pecj_c_consumer")
fi

CONSUMER_BUILD_DIR="${BUILD_ROOT}/consumer"
cmake -E remove_directory "${CONSUMER_BUILD_DIR}"
cmake -S "${STAGE_DIR}/share/sageTSDB/component_consumer" \
    -B "${CONSUMER_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${STAGE_DIR}"
cmake --build "${CONSUMER_BUILD_DIR}" --parallel "${JOBS}"
CONSUMER_RUNTIME_DIR="${BUILD_ROOT}/consumer-runtime"
cmake -E remove_directory "${CONSUMER_RUNTIME_DIR}"
mkdir -p "${CONSUMER_RUNTIME_DIR}"
if [[ ${BUNDLE_RUNTIME} -eq 1 ]]; then
    (cd "${CONSUMER_RUNTIME_DIR}" && env -u LD_LIBRARY_PATH "${CONSUMER_BUILD_DIR}/pecj_consumer")
else
    (cd "${CONSUMER_RUNTIME_DIR}" && LD_LIBRARY_PATH="${RUNTIME_PATH}" "${CONSUMER_BUILD_DIR}/pecj_consumer")
fi

(
    cd "${STAGE_DIR}"
    find . \( -type f -o -type l \) ! -name SHA256SUMS -print0 \
        | sort -z \
        | xargs -0 sha256sum > SHA256SUMS
)
tar -C "${OUTPUT_ROOT}" -czf "${ARCHIVE}" "${PACKAGE_NAME}"

echo "Package directory: ${STAGE_DIR}"
echo "Archive:           ${ARCHIVE}"
