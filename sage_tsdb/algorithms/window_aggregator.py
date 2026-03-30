"""
Window Aggregator Algorithm.

Provides tumbling, sliding, and session windowing strategies for
time series aggregation.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any

import numpy as np

from ..core import AggregationType, TimeSeriesData
from .base import TimeSeriesAlgorithm


class WindowType(Enum):
    """Window types for aggregation."""

    TUMBLING = "tumbling"  # Non-overlapping fixed-size windows
    SLIDING = "sliding"  # Overlapping fixed-size windows
    SESSION = "session"  # Dynamic windows based on inactivity gap


@dataclass
class WindowConfig:
    """Configuration for windowing."""

    window_type: WindowType
    window_size: int  # milliseconds
    slide_interval: int | None = None  # for sliding windows (ms)
    session_gap: int | None = None  # for session windows (ms)
    aggregation: AggregationType = AggregationType.AVG


class WindowAggregator(TimeSeriesAlgorithm):
    """
    Window-based aggregation algorithm.

    Supports tumbling, sliding, and session windowing with multiple
    aggregation functions (sum, avg, min, max, count, stddev, …).
    """

    def __init__(self, config: dict[str, Any] | None = None) -> None:
        super().__init__(config)

        window_type_str: str = self.config.get("window_type", "tumbling")
        self.window_type = WindowType(window_type_str)
        self.window_size: int = self.config.get("window_size", 60000)
        self.slide_interval: int = self.config.get("slide_interval", self.window_size)
        self.session_gap: int = self.config.get("session_gap", 30000)

        agg_raw = self.config.get("aggregation", "avg")
        self.aggregation = AggregationType(agg_raw) if isinstance(agg_raw, str) else agg_raw

        self.windows: dict[int, list[TimeSeriesData]] = {}
        self.stats: dict[str, int] = {
            "windows_created": 0,
            "windows_completed": 0,
            "data_points_processed": 0,
        }

    def process(self, data: list[TimeSeriesData], **kwargs: Any) -> list[TimeSeriesData]:
        """
        Process time series data with windowing.

        Returns:
            Aggregated time series data (one point per window).
        """
        if not data:
            return []

        sorted_data = sorted(data, key=lambda x: x.timestamp)

        if self.window_type == WindowType.TUMBLING:
            return self._tumbling_window(sorted_data)
        elif self.window_type == WindowType.SLIDING:
            return self._sliding_window(sorted_data)
        elif self.window_type == WindowType.SESSION:
            return self._session_window(sorted_data)
        return []

    def _tumbling_window(self, data: list[TimeSeriesData]) -> list[TimeSeriesData]:
        if not data:
            return []

        results: list[TimeSeriesData] = []
        window_start = self._align_to_window(data[0].timestamp)
        window_data: list[TimeSeriesData] = []

        for point in data:
            window_key = self._align_to_window(point.timestamp)
            if window_key == window_start:
                window_data.append(point)
            else:
                if window_data:
                    results.append(self._aggregate_window(window_data, window_start))
                    self.stats["windows_completed"] += 1
                while window_key > window_start:
                    window_start += self.window_size
                window_data = [point]
                self.stats["windows_created"] += 1

        if window_data:
            results.append(self._aggregate_window(window_data, window_start))
            self.stats["windows_completed"] += 1

        self.stats["data_points_processed"] += len(data)
        return results

    def _sliding_window(self, data: list[TimeSeriesData]) -> list[TimeSeriesData]:
        if not data:
            return []

        results: list[TimeSeriesData] = []
        window_start = self._align_to_window(data[0].timestamp)
        last_timestamp = data[-1].timestamp

        while window_start <= last_timestamp:
            window_end = window_start + self.window_size
            window_data = [p for p in data if window_start <= p.timestamp < window_end]
            if window_data:
                results.append(self._aggregate_window(window_data, window_start))
                self.stats["windows_completed"] += 1
            window_start += self.slide_interval
            self.stats["windows_created"] += 1

        self.stats["data_points_processed"] += len(data)
        return results

    def _session_window(self, data: list[TimeSeriesData]) -> list[TimeSeriesData]:
        if not data:
            return []

        results: list[TimeSeriesData] = []
        session_data: list[TimeSeriesData] = []
        last_timestamp = data[0].timestamp
        session_start = data[0].timestamp

        for point in data:
            if point.timestamp - last_timestamp <= self.session_gap:
                session_data.append(point)
            else:
                if session_data:
                    results.append(self._aggregate_window(session_data, session_start))
                    self.stats["windows_completed"] += 1
                session_data = [point]
                session_start = point.timestamp
                self.stats["windows_created"] += 1
            last_timestamp = point.timestamp

        if session_data:
            results.append(self._aggregate_window(session_data, session_start))
            self.stats["windows_completed"] += 1

        self.stats["data_points_processed"] += len(data)
        return results

    def _align_to_window(self, timestamp: int) -> int:
        return (timestamp // self.window_size) * self.window_size

    def _aggregate_window(
        self, data: list[TimeSeriesData], window_timestamp: int
    ) -> TimeSeriesData:
        if not data:
            return TimeSeriesData(timestamp=window_timestamp, value=0.0)

        values: list[float] = []
        for point in data:
            if isinstance(point.value, (list, np.ndarray)):
                values.extend(np.ravel(point.value).tolist())
            else:
                values.append(float(point.value))

        agg: float
        if self.aggregation == AggregationType.SUM:
            agg = sum(values)
        elif self.aggregation == AggregationType.AVG:
            agg = sum(values) / len(values)
        elif self.aggregation == AggregationType.MIN:
            agg = min(values)
        elif self.aggregation == AggregationType.MAX:
            agg = max(values)
        elif self.aggregation == AggregationType.COUNT:
            agg = float(len(values))
        elif self.aggregation == AggregationType.FIRST:
            agg = values[0]
        elif self.aggregation == AggregationType.LAST:
            agg = values[-1]
        elif self.aggregation == AggregationType.STDDEV:
            agg = float(np.std(values))
        else:
            agg = sum(values) / len(values)

        merged_tags: dict[str, str] = {}
        for point in data:
            if point.tags:
                merged_tags.update(point.tags)

        return TimeSeriesData(
            timestamp=window_timestamp,
            value=agg,
            tags=merged_tags,
            fields={"window_size": len(data), "aggregation": self.aggregation.value},
        )

    def reset(self) -> None:
        self.windows = {}
        self.stats = {"windows_created": 0, "windows_completed": 0, "data_points_processed": 0}

    def get_stats(self) -> dict[str, Any]:
        return {**self.stats, "active_windows": len(self.windows)}


__all__ = ["WindowAggregator", "WindowType", "WindowConfig"]
