# sageTSDB - Standalone C++ Time Series Database

This directory contains the standalone C++ implementation of sageTSDB,
which is used as a submodule in the SAGE project.

## Repository Setup

This directory is designed to be a standalone Git repository that can be
pushed to https://github.com/intellistream/sageTSDB

### Initial Setup

```bash
cd sageTSDB
git init
git add .
git commit -m "Initial commit: sageTSDB C++ core implementation"
git branch -M main
git remote add origin https://github.com/intellistream/sageTSDB.git
git push -u origin main
```

### After Pushing to GitHub

In the SAGE repository, add sageTSDB as a submodule:

```bash
cd /path/to/SAGE
git submodule add https://github.com/intellistream/sageTSDB.git \
    packages/sage-middleware/src/sage/middleware/components/sage_tsdb/sageTSDB

git submodule update --init --recursive
git add .
git commit -m "Add sageTSDB as submodule"
git push
```

## Project Structure

```
sageTSDB/
├── include/sage_tsdb/    # Public C++ headers
│   ├── core/             # Core time series functionality
│   ├── algorithms/       # Pluggable algorithms
│   └── utils/            # Utilities
├── src/                  # C++ implementation
│   ├── core/
│   ├── algorithms/
│   └── utils/
├── python/               # Python bindings (pybind11)
├── tests/                # Unit tests
├── examples/             # Example code
├── CMakeLists.txt        # Build configuration
├── build.sh              # Build script
└── README.md             # Documentation
```

## Building

```bash
./build.sh              # Build library
./build.sh --test       # Build and run tests
./build.sh --install    # Build and install system-wide
```

## Integration with SAGE

The Python layer in SAGE's sage_tsdb component uses this C++ core
through pybind11 bindings. The service layer and SAGE-specific
integrations remain in the SAGE repository.

## Development Workflow

1. Make changes to C++ code in sageTSDB/
2. Test locally: `./build.sh --test`
3. Commit and push to sageTSDB repository
4. Update submodule in SAGE:
   ```bash
   cd packages/sage-middleware/src/sage/middleware/components/sage_tsdb/sageTSDB
   git pull origin main
   cd /path/to/SAGE
   git add sageTSDB
   git commit -m "Update sageTSDB submodule"
   git push
   ```

## License

Apache License 2.0 - See LICENSE file
