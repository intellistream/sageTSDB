#!/bin/bash
# Build wheel using current system (faster alternative to Docker)
# This builds a wheel compatible with the current GLIBC version

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo -e "${GREEN}Building sageTSDB wheel (native)...${NC}"

cd "${PROJECT_ROOT}"

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf build dist *.egg-info
rm -rf sage_tsdb/*.so sage_tsdb/*.pyc sage_tsdb/__pycache__

# Build wheel
echo -e "${YELLOW}Building wheel with scikit-build-core...${NC}"
python -m build --wheel -o dist/

echo -e "${GREEN}Wheel built successfully!${NC}"
echo -e "${YELLOW}Output directory: ${PROJECT_ROOT}/dist${NC}"
ls -lh "${PROJECT_ROOT}/dist"

# Show wheel info
for whl in "${PROJECT_ROOT}"/dist/*.whl; do
    if [ -f "$whl" ]; then
        echo -e "\n${GREEN}Wheel info for: $(basename $whl)${NC}"
        python -m zipfile -l "$whl" | grep -E '\.so|\.py$' | head -20
        echo -e "\n${YELLOW}Platform tag:${NC}"
        basename "$whl" | grep -oP '(linux|manylinux|macosx).*\.whl' || echo "  $(basename $whl)"
    fi
done

echo -e "\n${GREEN}Done! Upload with:${NC}"
echo -e "  sage-pypi-publisher upload ${PROJECT_ROOT}/dist/*.whl -r testpypi --no-dry-run"
echo -e "  (or -r pypi for production)"
