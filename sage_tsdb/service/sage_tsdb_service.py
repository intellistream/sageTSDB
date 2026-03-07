"""
SageTSDB micro-service wrapper.

Provides a simplified service-style interface for time series operations,
integrating TimeSeriesDB with built-in algorithm registration.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from typing import Any

import numpy as np

from ..algorithms import OutOfOrderStreamJoin, WindowAggregator
from ..core import AggregationType, TimeRange, TimeSeriesData, TimeSeriesDB


@dataclass
class SageTSDBServiceConfig:
    """Configuration for SageTSDB service."""

    enable_compression: bool = False
    max_memory_mb: int = 1024
    default_window_size: int = 60000  # milliseconds
    default_aggregation: str = "avg"


class SageTSDBService:
    """
    Micro-service style wrapper for TimeSeriesDB.

    Provides a simplified interface for time series operations and
    integrates with SAGE's service ecosystem.
    """

    def __init__(self, config: SageTSDBServiceConfig | None = None) -> None:
        self._config = config or SageTSDBServiceConfig()
        self._db = TimeSeriesDB()
        self._register_default_algorithms()
        self._stats: dict[str, int] = {
            "total_writes": 0,
            "total_queries": 0,
            "total_joins": 0,
            "total_aggregations": 0,
        }

    def _register_default_algorithms(self) -> None:
        self._db.register_algorithm(
            "stream_join",
            OutOfOrderStreamJoin(
                {
                    "window_size": self._config.default_window_size,
                    "max_delay": 5000,
                }
            ),
        )
        self._db.register_algorithm(
            "window_aggregate",
            WindowAggregator(
                {
                    "window_type": "tumbling",
                    "window_size": self._config.default_window_size,
                    "aggregation": self._config.default_aggregation,
                }
            ),
        )

    def add(
        self,
        timestamp: int | datetime,
        value: float | np.ndarray | list[float],
        tags: dict[str, str] | None = None,
        fields: dict[str, Any] | None = None,
    ) -> int:
        """Add a single time series data point."""
        if isinstance(value, list):
            value = np.array(value, dtype=np.float32)
        idx = self._db.add(timestamp=timestamp, value=value, tags=tags, fields=fields)
        self._stats["total_writes"] += 1
        return idx

    def add_batch(
        self,
        timestamps: list[int] | list[datetime] | np.ndarray,
        values: list[float] | np.ndarray,
        tags_list: list[dict[str, str]] | None = None,
        fields_list: list[dict[str, Any]] | None = None,
    ) -> list[int]:
        """Add multiple time series data points."""
        indices = self._db.add_batch(
            timestamps=timestamps,
            values=values,
            tags_list=tags_list,
            fields_list=fields_list,
        )
        self._stats["total_writes"] += len(indices)
        return indices

    def query(
        self,
        start_time: int | datetime,
        end_time: int | datetime,
        tags: dict[str, str] | None = None,
        aggregation: str | None = None,
        window_size: int | None = None,
        limit: int | None = None,
    ) -> list[dict[str, Any]]:
        """Query time series data, returning results as plain dicts."""
        time_range = TimeRange(start_time=start_time, end_time=end_time)
        agg_type = AggregationType(aggregation) if aggregation else None

        results = self._db.query(
            time_range=time_range,
            tags=tags,
            aggregation=agg_type,
            window_size=window_size,
            limit=limit,
        )

        formatted: list[dict[str, Any]] = []
        for r in results:
            formatted.append(
                {
                    "timestamp": r.timestamp,
                    "value": (
                        float(r.value)
                        if isinstance(r.value, (int, float))
                        else (r.value.tolist() if isinstance(r.value, np.ndarray) else r.value)
                    ),
                    "tags": dict(r.tags) if r.tags else {},
                    "fields": dict(r.fields) if r.fields else {},
                }
            )

        self._stats["total_queries"] += 1
        return formatted

    def stream_join(
        self,
        left_stream: list[dict[str, Any]],
        right_stream: list[dict[str, Any]],
        window_size: int | None = None,
        max_delay: int | None = None,
        join_key: str | None = None,
    ) -> list[dict[str, Any]]:
        """Perform out-of-order stream join."""
        config = {
            "window_size": window_size or self._config.default_window_size,
            "max_delay": max_delay or 5000,
            "join_key": join_key,
        }
        join_algo = OutOfOrderStreamJoin(config)

        left_data = [
            TimeSeriesData(
                timestamp=item["timestamp"],
                value=item["value"],
                tags=item.get("tags"),
                fields=item.get("fields"),
            )
            for item in left_stream
        ]
        right_data = [
            TimeSeriesData(
                timestamp=item["timestamp"],
                value=item["value"],
                tags=item.get("tags"),
                fields=item.get("fields"),
            )
            for item in right_stream
        ]

        joined = join_algo.process(left_stream=left_data, right_stream=right_data)

        results: list[dict[str, Any]] = []
        for left, right in joined:
            results.append(
                {
                    "left": {
                        "timestamp": left.timestamp,
                        "value": float(left.value)
                        if isinstance(left.value, (int, float))
                        else left.value,
                        "tags": dict(left.tags) if left.tags else {},
                    },
                    "right": {
                        "timestamp": right.timestamp,
                        "value": float(right.value)
                        if isinstance(right.value, (int, float))
                        else right.value,
                        "tags": dict(right.tags) if right.tags else {},
                    },
                }
            )

        self._stats["total_joins"] += 1
        return results

    def window_aggregate(
        self,
        start_time: int | datetime,
        end_time: int | datetime,
        window_type: str = "tumbling",
        window_size: int | None = None,
        aggregation: str = "avg",
        tags: dict[str, str] | None = None,
    ) -> list[dict[str, Any]]:
        """Perform window-based aggregation over stored data."""
        time_range = TimeRange(start_time=start_time, end_time=end_time)
        data = self._db.query(time_range=time_range, tags=tags)

        aggregator = WindowAggregator(
            {
                "window_type": window_type,
                "window_size": window_size or self._config.default_window_size,
                "aggregation": aggregation,
            }
        )
        aggregated = aggregator.process(data)

        results: list[dict[str, Any]] = []
        for item in aggregated:
            results.append(
                {
                    "timestamp": item.timestamp,
                    "value": float(item.value)
                    if isinstance(item.value, (int, float))
                    else item.value,
                    "tags": dict(item.tags) if item.tags else {},
                    "fields": dict(item.fields) if item.fields else {},
                }
            )

        self._stats["total_aggregations"] += 1
        return results

    def stats(self) -> dict[str, Any]:
        """Get service statistics."""
        db_stats = self._db.get_stats()
        return {
            **self._stats,
            "db_size": db_stats["size"],
            "registered_algorithms": db_stats["algorithms"],
        }

    def reset(self) -> None:
        """Reset service state."""
        self._db = TimeSeriesDB()
        self._register_default_algorithms()
        self._stats = {
            "total_writes": 0,
            "total_queries": 0,
            "total_joins": 0,
            "total_aggregations": 0,
        }


__all__ = ["SageTSDBService", "SageTSDBServiceConfig"]
