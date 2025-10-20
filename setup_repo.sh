#!/bin/bash
# Setup script for sageTSDB repository

set -e

SAGE_TSDB_DIR="/home/shuhao/SAGE/packages/sage-middleware/src/sage/middleware/components/sage_tsdb/sageTSDB"
REMOTE_URL="https://github.com/intellistream/sageTSDB.git"

echo "======================================================================"
echo "sageTSDB Repository Setup Script"
echo "======================================================================"
echo ""

# Check if we're in the right directory
if [ ! -d "$SAGE_TSDB_DIR" ]; then
    echo "Error: sageTSDB directory not found at $SAGE_TSDB_DIR"
    exit 1
fi

cd "$SAGE_TSDB_DIR"

echo "Current directory: $(pwd)"
echo ""

# Step 1: Initialize git repository if not already initialized
if [ ! -d ".git" ]; then
    echo "Step 1: Initializing Git repository..."
    git init
    echo "✓ Git repository initialized"
else
    echo "Step 1: Git repository already initialized"
fi
echo ""

# Step 2: Add all files
echo "Step 2: Adding files to Git..."
git add .
echo "✓ Files added"
echo ""

# Step 3: Create initial commit
echo "Step 3: Creating initial commit..."
if git diff-index --quiet HEAD --; then
    echo "No changes to commit"
else
    git commit -m "Initial commit: sageTSDB C++ core implementation

Features:
- High-performance time series storage with indexing
- Out-of-order data handling with watermarking
- Pluggable algorithm framework
- Stream join algorithm for time series
- Window aggregation (tumbling, sliding, session)
- Thread-safe operations with read-write locks
- CMake build system
- Python bindings support via pybind11"
    echo "✓ Initial commit created"
fi
echo ""

# Step 4: Set main branch
echo "Step 4: Setting main branch..."
git branch -M main
echo "✓ Branch set to main"
echo ""

# Step 5: Add remote
echo "Step 5: Adding remote repository..."
if git remote get-url origin > /dev/null 2>&1; then
    CURRENT_REMOTE=$(git remote get-url origin)
    if [ "$CURRENT_REMOTE" != "$REMOTE_URL" ]; then
        echo "Warning: Remote 'origin' already exists with different URL:"
        echo "  Current: $CURRENT_REMOTE"
        echo "  Expected: $REMOTE_URL"
        echo ""
        read -p "Do you want to update the remote URL? (y/n) " -n 1 -r
        echo ""
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            git remote set-url origin "$REMOTE_URL"
            echo "✓ Remote URL updated"
        else
            echo "Keeping current remote URL"
        fi
    else
        echo "✓ Remote already configured correctly"
    fi
else
    git remote add origin "$REMOTE_URL"
    echo "✓ Remote added: $REMOTE_URL"
fi
echo ""

# Step 6: Instructions for pushing
echo "======================================================================"
echo "Setup Complete!"
echo "======================================================================"
echo ""
echo "Next steps:"
echo ""
echo "1. Create the repository on GitHub:"
echo "   - Go to https://github.com/intellistream"
echo "   - Create a new repository named 'sageTSDB'"
echo "   - Do NOT initialize with README, .gitignore, or license"
echo ""
echo "2. Push the code:"
echo "   cd $SAGE_TSDB_DIR"
echo "   git push -u origin main"
echo ""
echo "3. Add as submodule in SAGE:"
echo "   cd /home/shuhao/SAGE"
echo "   git submodule add https://github.com/intellistream/sageTSDB.git \\"
echo "       packages/sage-middleware/src/sage/middleware/components/sage_tsdb/sageTSDB"
echo "   git add ."
echo "   git commit -m 'Add sageTSDB as submodule'"
echo "   git push"
echo ""
echo "======================================================================"
