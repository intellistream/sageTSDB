#!/bin/bash
# Upload sageTSDB wheel to PyPI using sage-pypi-publisher

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Check for wheel files
if [ ! -d "${PROJECT_ROOT}/dist" ] || [ -z "$(ls -A ${PROJECT_ROOT}/dist/*.whl 2>/dev/null)" ]; then
    echo -e "${RED}No wheel files found in dist/. Run build first:${NC}"
    echo "  ./scripts/build_native_wheel.sh"
    exit 1
fi

echo -e "${GREEN}=== sageTSDB PyPI Upload ===${NC}"
echo -e "${YELLOW}Wheel files:${NC}"
ls -lh "${PROJECT_ROOT}"/dist/*.whl

# Ask for target repository
echo -e "\n${YELLOW}Select target repository:${NC}"
echo "  1) TestPyPI (test.pypi.org) - Recommended for testing"
echo "  2) PyPI (pypi.org) - Production release"
read -p "Enter choice [1-2]: " choice

case $choice in
    1)
        REPO="testpypi"
        echo -e "${YELLOW}Uploading to TestPyPI...${NC}"
        ;;
    2)
        REPO="pypi"
        echo -e "${YELLOW}Uploading to PyPI (production)...${NC}"
        read -p "Are you sure? This will publish to production. [y/N]: " confirm
        if [[ ! $confirm =~ ^[Yy]$ ]]; then
            echo "Cancelled."
            exit 1
        fi
        ;;
    *)
        echo -e "${RED}Invalid choice${NC}"
        exit 1
        ;;
esac

# Upload with sage-pypi-publisher
echo -e "\n${GREEN}Running sage-pypi-publisher...${NC}"
sage-pypi-publisher upload "${PROJECT_ROOT}"/dist/*.whl \
    -r "$REPO" \
    --no-dry-run

echo -e "\n${GREEN}✅ Upload complete!${NC}"

if [ "$REPO" = "testpypi" ]; then
    echo -e "\n${YELLOW}Test installation with:${NC}"
    echo "  pip install -i https://test.pypi.org/simple/ --extra-index-url https://pypi.org/simple isage-tsdb==0.1.5"
else
    echo -e "\n${YELLOW}Install with:${NC}"
    echo "  pip install --upgrade isage-tsdb"
fi
