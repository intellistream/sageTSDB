# Changelog

All notable changes to **isage-tsdb** are documented here.

Format: [Semantic Versioning](https://semver.org/)  
PyPI: https://pypi.org/project/isage-tsdb/

---

## [Unreleased]

### Added
- Python examples for `sage_tsdb` usage patterns
- Unified pre-push hook with automatic post-push PyPI publish
- `quickstart.sh` for streamlined development setup

---

## [0.1.5] - 2026-01-04

### Fixed
- **GLIBC compatibility**: Rebuilt on Ubuntu 22.04 (GLIBC 2.35) to fix `ImportError: GLIBC_2.38 not found` on systems running Ubuntu 22.04 / Debian 12
  - Previous 0.1.4 wheel was built on Ubuntu 24.04 (GLIBC 2.38) and was incompatible with Ubuntu 22.04
  - New wheel uses `manylinux_2_35_x86_64` platform tag (requires GLIBC 2.35+)

### Added
- GitHub Actions workflow for automated wheel builds (`build-wheels.yml`)
- Docker support for manylinux container builds (`docker/Dockerfile.manylinux`)
- Build scripts: `scripts/build_native_wheel.sh`, `scripts/build_manylinux_wheel.sh`
- Wheel build and PyPI publish guide (`docs/WHEEL_BUILD_AND_PYPI_PUBLISH.md`)

### Compatibility

| OS | GLIBC | Supported |
|----|-------|-----------|
| Ubuntu 24.04 | 2.38 | ✅ |
| Ubuntu 22.04 | 2.35 | ✅ |
| Ubuntu 20.04 | 2.31 | ❌ |
| Debian 12 | 2.36 | ✅ |
| Debian 11 | 2.31 | ❌ |

---

## [0.1.4] - 2026-01

### Notes
- Built on Ubuntu 24.04; incompatible with Ubuntu 22.04 due to GLIBC version mismatch
- Superseded by 0.1.5

---

## [0.1.1] - 2025-12

### Added
- Initial PyPI release as `isage-tsdb`
- Python bindings via pybind11 for C++ core (`_sage_tsdb` extension module)
- `sage_tsdb` package with `TimeSeriesDB`, `TableType`, `QueryParams`, `WindowConfig`
- C++ shared libraries: `libsage_tsdb_core.so`, `libsage_tsdb_algorithms.so`, `libsage_tsdb_plugins.so`
- scikit-build-core based packaging
- PECJ (Progressive Early Cancellation Join) integrated and plugin modes
- LSM-Tree storage engine for time-series data
- Window-based stream join operations
- Resource manager for unified thread/memory allocation

### Technical Notes
- Requires Python 3.10+
- C++20 core with old ABI (`_GLIBCXX_USE_CXX11_ABI=0`) for PyTorch/PECJ compatibility
- Package renamed from `python/` to `sage_tsdb/` for proper PyPI packaging

---

[Unreleased]: https://github.com/intellistream/sageTSDB/compare/v0.1.5...HEAD
[0.1.5]: https://github.com/intellistream/sageTSDB/releases/tag/v0.1.5
[0.1.4]: https://github.com/intellistream/sageTSDB/releases/tag/v0.1.4
[0.1.1]: https://github.com/intellistream/sageTSDB/releases/tag/v0.1.1
