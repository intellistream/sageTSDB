"""
Base algorithm interface for time series processing.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any

from ..core import TimeSeriesData


class TimeSeriesAlgorithm(ABC):
    """
    Abstract base class for time series processing algorithms.

    All algorithm implementations should inherit from this class and
    implement the ``process`` method.
    """

    def __init__(self, config: dict[str, Any] | None = None) -> None:
        self.config = config or {}

    @abstractmethod
    def process(self, data: list[TimeSeriesData], **kwargs: Any) -> Any:
        """
        Process time series data.

        Args:
            data: Input time series data points.
            **kwargs: Additional algorithm-specific parameters.

        Returns:
            Processed results (algorithm-specific format).
        """

    def reset(self) -> None:  # noqa: B027
        """Reset algorithm state (for stateful algorithms)."""

    def get_stats(self) -> dict[str, Any]:
        """Get algorithm statistics."""
        return {}


__all__ = ["TimeSeriesAlgorithm"]
