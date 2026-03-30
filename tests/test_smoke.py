"""Smoke tests for package import and public API surface."""

import sage_tsdb


def test_package_imports() -> None:
    """The top-level package should import and expose version metadata."""
    assert isinstance(sage_tsdb.__version__, str)
    assert sage_tsdb.__version__


def test_public_symbols_exposed() -> None:
    """Core C++-backed symbols should be available from top-level imports."""
    expected = {
        "TimeSeriesData",
        "TimeSeriesDB",
        "TimeSeriesIndex",
        "TimeRange",
        "QueryConfig",
    }
    for name in expected:
        assert hasattr(sage_tsdb, name)
