"""Tests for sage_tsdb algorithms — OutOfOrderStreamJoin and WindowAggregator."""

from sage_tsdb import OutOfOrderStreamJoin, WindowAggregator
from sage_tsdb.core import TimeSeriesData


# ---------------------------------------------------------------------------
# OutOfOrderStreamJoin
# ---------------------------------------------------------------------------


def _make_data(ts: int, value: float, tag: str | None = None) -> TimeSeriesData:
    tags = {"id": tag} if tag else {}
    return TimeSeriesData(timestamp=ts, value=value, tags=tags)


def test_stream_join_basic():
    # max_delay=0 means watermark == latest, so all data is immediately ready
    join = OutOfOrderStreamJoin({"window_size": 5000, "max_delay": 0})
    left = [_make_data(1000, 1.0), _make_data(2000, 2.0)]
    right = [_make_data(1200, 1.5), _make_data(2100, 2.5)]
    joined = join.process(left_stream=left, right_stream=right)
    assert len(joined) > 0
    for lpt, r in joined:
        assert abs(lpt.timestamp - r.timestamp) <= 5000


def test_stream_join_equi_join_key():
    join = OutOfOrderStreamJoin({"window_size": 5000, "max_delay": 0, "join_key": "id"})
    left = [_make_data(1000, 1.0, "x"), _make_data(2000, 2.0, "y")]
    right = [_make_data(1100, 1.5, "x"), _make_data(2100, 2.5, "z")]  # "z" ≠ "y"
    joined = join.process(left_stream=left, right_stream=right)
    assert len(joined) == 1
    assert joined[0][0].tags == {"id": "x"}


def test_stream_join_reset():
    join = OutOfOrderStreamJoin({"window_size": 5000, "max_delay": 1000})
    join.process(left_stream=[_make_data(1000, 1.0)])
    join.reset()
    stats = join.get_stats()
    assert stats["total_joined"] == 0


def test_stream_join_stats():
    join = OutOfOrderStreamJoin({"window_size": 5000, "max_delay": 0})
    left = [_make_data(1000, 1.0)]
    right = [_make_data(1200, 1.5)]
    join.process(left_stream=left, right_stream=right)
    stats = join.get_stats()
    assert "total_joined" in stats
    assert "left_watermark" in stats


# ---------------------------------------------------------------------------
# WindowAggregator
# ---------------------------------------------------------------------------


def _make_series(n: int = 10, step_ms: int = 1000) -> list[TimeSeriesData]:
    return [TimeSeriesData(timestamp=i * step_ms, value=float(i + 1)) for i in range(n)]


def test_window_aggregator_tumbling():
    agg = WindowAggregator({"window_type": "tumbling", "window_size": 3000, "aggregation": "avg"})
    data = _make_series(9, 1000)
    results = agg.process(data)
    assert len(results) >= 1


def test_window_aggregator_sliding():
    agg = WindowAggregator(
        {
            "window_type": "sliding",
            "window_size": 3000,
            "slide_interval": 1000,
            "aggregation": "sum",
        }
    )
    data = _make_series(5, 1000)
    results = agg.process(data)
    assert len(results) >= 1


def test_window_aggregator_session():
    agg = WindowAggregator({"window_type": "session", "session_gap": 500, "aggregation": "max"})
    # Two sessions: ts 0-200 and ts 1000-1200
    data = [
        TimeSeriesData(timestamp=0, value=1.0),
        TimeSeriesData(timestamp=200, value=2.0),
        TimeSeriesData(timestamp=1000, value=5.0),
        TimeSeriesData(timestamp=1200, value=3.0),
    ]
    results = agg.process(data)
    assert len(results) == 2
    assert results[0].value == 2.0  # max of session 1
    assert results[1].value == 5.0  # max of session 2


def test_window_aggregator_empty():
    agg = WindowAggregator({"window_size": 1000})
    assert agg.process([]) == []


def test_window_aggregator_reset():
    agg = WindowAggregator({"window_size": 1000})
    agg.process(_make_series(5))
    agg.reset()
    stats = agg.get_stats()
    assert stats["data_points_processed"] == 0


def test_window_aggregator_count():
    agg = WindowAggregator({"window_type": "tumbling", "window_size": 5000, "aggregation": "count"})
    data = _make_series(4, 1000)
    results = agg.process(data)
    assert results[0].value == 4.0
