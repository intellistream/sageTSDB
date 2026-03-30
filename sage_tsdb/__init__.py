"""
SAGE TSDB Python Bindings
High-performance time series database for streaming data.

Provides C++ core bindings when available, with a pure-Python fallback.
Python-side streaming algorithms (WindowAggregator, OutOfOrderStreamJoin)
and service wrapper (SageTSDBService) are always available.
"""

# ---------------------------------------------------------------------------
# C++ core bindings (preferred when available)
# ---------------------------------------------------------------------------
_HAS_CPP = False
try:
    from ._sage_tsdb import (  # noqa: F401, F403
        QueryConfig,
        TimeRange,
        TimeSeriesDB,
        TimeSeriesData,
        TimeSeriesIndex,
    )

    _HAS_CPP = True
except ImportError:
    # Pure-Python fallback — used when C++ bindings are not compiled for
    # the current interpreter (e.g. development without CMake build).
    from .core import (  # noqa: F401
        QueryConfig,
        TimeRange,
        TimeSeriesDB,
        TimeSeriesData,
        TimeSeriesIndex,
    )

# AggregationType and InterpolationType are Python-only (not in C++ bindings)
from .core import AggregationType, InterpolationType  # noqa: F401, E402

# ---------------------------------------------------------------------------
# Python-side streaming algorithms
# ---------------------------------------------------------------------------
from .algorithms import (  # noqa: F401, E402
    OutOfOrderStreamJoin,
    TimeSeriesAlgorithm,
    WindowAggregator,
)

# ---------------------------------------------------------------------------
# Service wrapper
# ---------------------------------------------------------------------------
from .service import SageTSDBService, SageTSDBServiceConfig  # noqa: F401, E402

# ---------------------------------------------------------------------------
# Version
# ---------------------------------------------------------------------------
from ._version import __version__, __author__, __email__  # noqa: F401, E402

__all__ = [
    # core types
    "TimeSeriesData",
    "TimeSeriesDB",
    "TimeSeriesIndex",
    "TimeRange",
    "QueryConfig",
    "AggregationType",
    "InterpolationType",
    # algorithms
    "TimeSeriesAlgorithm",
    "OutOfOrderStreamJoin",
    "WindowAggregator",
    # service
    "SageTSDBService",
    "SageTSDBServiceConfig",
    # meta
    "__version__",
    "__author__",
    "__email__",
    "_HAS_CPP",
]
