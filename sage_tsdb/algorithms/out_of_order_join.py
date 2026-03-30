"""
Out-of-Order Stream Join Algorithm.

Handles joining two time series streams that may arrive out of order,
using windowing and watermarking strategies.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from ..core import TimeSeriesData
from .base import TimeSeriesAlgorithm


@dataclass
class JoinConfig:
    """Configuration for stream join."""

    window_size: int  # milliseconds
    max_delay: int  # maximum out-of-order delay (ms)
    join_key: str | None = None
    join_predicate: Callable[[TimeSeriesData, TimeSeriesData], bool] | None = None


class StreamBuffer:
    """Buffer for managing out-of-order streams."""

    def __init__(self, max_delay: int) -> None:
        self.max_delay = max_delay
        self.buffer: list[TimeSeriesData] = []
        self.watermark = 0

    def add(self, data: TimeSeriesData) -> None:
        self.buffer.append(data)
        self._update_watermark()

    def add_batch(self, data_list: list[TimeSeriesData]) -> None:
        self.buffer.extend(data_list)
        self._update_watermark()

    def _update_watermark(self) -> None:
        if self.buffer:
            self.buffer.sort(key=lambda x: x.timestamp)
            latest = self.buffer[-1].timestamp
            self.watermark = latest - self.max_delay

    def get_ready_data(self) -> list[TimeSeriesData]:
        """Return data that is ready for processing (before watermark)."""
        ready = [d for d in self.buffer if d.timestamp <= self.watermark]
        self.buffer = [d for d in self.buffer if d.timestamp > self.watermark]
        return ready

    def size(self) -> int:
        return len(self.buffer)


class OutOfOrderStreamJoin(TimeSeriesAlgorithm):
    """
    Out-of-Order Stream Join Algorithm.

    Joins two time series streams that may arrive out of order using
    window-based join semantics and watermark-driven late-data handling.
    """

    def __init__(self, config: dict[str, Any] | None = None) -> None:
        super().__init__(config)
        self.window_size: int = self.config.get("window_size", 10000)
        self.max_delay: int = self.config.get("max_delay", 5000)
        self.join_key: str | None = self.config.get("join_key", None)
        self.join_predicate: Callable | None = self.config.get("join_predicate", None)

        self.left_buffer = StreamBuffer(self.max_delay)
        self.right_buffer = StreamBuffer(self.max_delay)

        self.stats: dict[str, int] = {
            "total_joined": 0,
            "late_arrivals": 0,
            "dropped_late": 0,
        }

    def add_left_stream(self, data: list[TimeSeriesData]) -> None:
        self.left_buffer.add_batch(data)

    def add_right_stream(self, data: list[TimeSeriesData]) -> None:
        self.right_buffer.add_batch(data)

    def process(  # type: ignore[override]
        self,
        data: list[TimeSeriesData] | None = None,
        left_stream: list[TimeSeriesData] | None = None,
        right_stream: list[TimeSeriesData] | None = None,
        **kwargs: Any,
    ) -> list[tuple[TimeSeriesData, TimeSeriesData]]:
        if left_stream:
            self.add_left_stream(left_stream)
        if right_stream:
            self.add_right_stream(right_stream)

        left_ready = self.left_buffer.get_ready_data()
        right_ready = self.right_buffer.get_ready_data()
        joined = self._join_data(left_ready, right_ready)
        self.stats["total_joined"] += len(joined)
        return joined

    def _join_data(
        self,
        left_data: list[TimeSeriesData],
        right_data: list[TimeSeriesData],
    ) -> list[tuple[TimeSeriesData, TimeSeriesData]]:
        if self.join_key:
            return self._hash_join(left_data, right_data)
        return self._nested_loop_join(left_data, right_data)

    def _hash_join(
        self,
        left_data: list[TimeSeriesData],
        right_data: list[TimeSeriesData],
    ) -> list[tuple[TimeSeriesData, TimeSeriesData]]:
        joined: list[tuple[TimeSeriesData, TimeSeriesData]] = []
        right_hash: dict[str, list[TimeSeriesData]] = defaultdict(list)

        for right in right_data:
            key_value = right.tags.get(self.join_key) if self.join_key else None
            if key_value:
                right_hash[key_value].append(right)

        for left in left_data:
            key_value = left.tags.get(self.join_key) if self.join_key else None
            if key_value and key_value in right_hash:
                for right in right_hash[key_value]:
                    if abs(left.timestamp - right.timestamp) <= self.window_size:
                        if self.join_predicate is None or self.join_predicate(left, right):
                            joined.append((left, right))
        return joined

    def _nested_loop_join(
        self,
        left_data: list[TimeSeriesData],
        right_data: list[TimeSeriesData],
    ) -> list[tuple[TimeSeriesData, TimeSeriesData]]:
        joined: list[tuple[TimeSeriesData, TimeSeriesData]] = []
        for left in left_data:
            for right in right_data:
                if abs(left.timestamp - right.timestamp) <= self.window_size:
                    if self.join_predicate is None or self.join_predicate(left, right):
                        joined.append((left, right))
        return joined

    def reset(self) -> None:
        self.left_buffer = StreamBuffer(self.max_delay)
        self.right_buffer = StreamBuffer(self.max_delay)
        self.stats = {"total_joined": 0, "late_arrivals": 0, "dropped_late": 0}

    def get_stats(self) -> dict[str, Any]:
        return {
            **self.stats,
            "left_buffer_size": self.left_buffer.size(),
            "right_buffer_size": self.right_buffer.size(),
            "left_watermark": self.left_buffer.watermark,
            "right_watermark": self.right_buffer.watermark,
        }


__all__ = ["OutOfOrderStreamJoin", "JoinConfig", "StreamBuffer"]
