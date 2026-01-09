#!/bin/bash
# Build manylinux-compatible wheels using Docker

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo -e "${GREEN}Building manylinux2014 compatible wheels...${NC}"

# Check Docker
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Docker not found. Please install Docker first.${NC}"
    exit 1
fi

# Create dist directory
mkdir -p "${PROJECT_ROOT}/dist"

# Build using Docker
echo -e "${YELLOW}Building in manylinux2014 container...${NC}"
docker build -t sagetsdb-manylinux -f "${PROJECT_ROOT}/docker/Dockerfile.manylinux" "${PROJECT_ROOT}"
docker run --rm -v "${PROJECT_ROOT}/dist:/workspace/dist" sagetsdb-manylinux

echo -e "${GREEN}Wheels built successfully!${NC}"
echo -e "${YELLOW}Output directory: ${PROJECT_ROOT}/dist${NC}"
ls -lh "${PROJECT_ROOT}/dist"

# Verify wheels
echo -e "${YELLOW}Verifying wheel compatibility...${NC}"
for whl in "${PROJECT_ROOT}"/dist/*.whl; do
    if [ -f "$whl" ]; then
        echo -e "${GREEN}Checking: $(basename $whl)${NC}"
        unzip -l "$whl" | grep -E '\.so|\.dylib|\.pyd' || echo "  No binary extensions found"
    fi
done

echo -e "${GREEN}Done! You can now upload with:${NC}"
echo -e "  sage-pypi-publisher upload ${PROJECT_ROOT}/dist/*.whl -r pypi --no-dry-run"
