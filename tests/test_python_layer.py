"""Tests for sage_tsdb.core — TimeSeriesDB, TimeSeriesIndex, data types."""

import pytest

from sage_tsdb import (
    AggregationType,
    QueryConfig,
    TimeRange,
    TimeSeriesData,
    TimeSeriesDB,
    TimeSeriesIndex,
)


# ---------------------------------------------------------------------------
# TimeRange
# ---------------------------------------------------------------------------


def test_time_range_plain():
    tr = TimeRange(start_time=1000, end_time=2000)
    assert tr.start_time == 1000
    assert tr.end_time == 2000


def test_time_range_from_datetime():
    from datetime import datetime, timezone

    dt_start = datetime(2024, 1, 1, tzinfo=timezone.utc)
    dt_end = datetime(2024, 1, 2, tzinfo=timezone.utc)
    tr = TimeRange(start_time=dt_start, end_time=dt_end)
    # should be converted to ms
    assert isinstance(tr.start_time, int)
    assert tr.start_time < tr.end_time


# ---------------------------------------------------------------------------
# TimeSeriesData
# ---------------------------------------------------------------------------


def test_time_series_data_defaults():
    d = TimeSeriesData(timestamp=1000, value=3.14)
    assert d.tags == {}
    assert d.fields == {}


# ---------------------------------------------------------------------------
# TimeSeriesIndex
# ---------------------------------------------------------------------------


def test_index_add_and_query():
    idx = TimeSeriesIndex()
    idx.add(TimeSeriesData(timestamp=1000, value=1.0))
    idx.add(TimeSeriesData(timestamp=2000, value=2.0))
    idx.add(TimeSeriesData(timestamp=3000, value=3.0))

    cfg = QueryConfig(time_range=TimeRange(900, 2500))
    results = idx.query(cfg)
    assert len(results) == 2


def test_index_tag_filter():
    idx = TimeSeriesIndex()
    idx.add(TimeSeriesData(timestamp=1000, value=1.0, tags={"sensor": "A"}))
    idx.add(TimeSeriesData(timestamp=2000, value=2.0, tags={"sensor": "B"}))

    cfg = QueryConfig(time_range=TimeRange(500, 3000), tags={"sensor": "A"})
    results = idx.query(cfg)
    assert len(results) == 1
    assert results[0].value == 1.0


def test_index_out_of_order_insert():
    idx = TimeSeriesIndex()
    idx.add(TimeSeriesData(timestamp=3000, value=3.0))
    idx.add(TimeSeriesData(timestamp=1000, value=1.0))
    idx.add(TimeSeriesData(timestamp=2000, value=2.0))

    cfg = QueryConfig(time_range=TimeRange(0, 4000))
    results = idx.query(cfg)
    assert [r.timestamp for r in results] == [1000, 2000, 3000]


# ---------------------------------------------------------------------------
# TimeSeriesDB
# ---------------------------------------------------------------------------


def test_db_add_and_size():
    db = TimeSeriesDB()
    db.add(timestamp=1000, value=1.0)
    db.add(timestamp=2000, value=2.0)
    assert db.size == 2


def test_db_add_batch():
    db = TimeSeriesDB()
    indices = db.add_batch(
        timestamps=[1000, 2000, 3000],
        values=[1.0, 2.0, 3.0],
    )
    assert len(indices) == 3
    assert db.size == 3


def test_db_query_time_range():
    db = TimeSeriesDB()
    db.add(1000, 1.0)
    db.add(2000, 2.0)
    db.add(3000, 3.0)
    results = db.query(TimeRange(900, 2500))
    assert len(results) == 2


def test_db_query_with_aggregation():
    db = TimeSeriesDB()
    for ts, v in [(1000, 1.0), (2000, 2.0), (3000, 3.0), (4000, 4.0)]:
        db.add(ts, v)
    results = db.query(TimeRange(0, 5000), aggregation=AggregationType.AVG, window_size=2500)
    assert len(results) >= 1


def test_db_stats():
    db = TimeSeriesDB()
    db.add(1000, 1.0)
    stats = db.get_stats()
    assert stats["size"] == 1
    assert stats["backend"] == "python"


def test_db_register_and_apply_algorithm():
    from sage_tsdb.algorithms import WindowAggregator

    db = TimeSeriesDB()
    db.add(1000, 1.0)
    db.add(2000, 2.0)
    db.register_algorithm("wagg", WindowAggregator({"window_size": 5000, "aggregation": "avg"}))
    results = db.apply_algorithm("wagg", db.query(TimeRange(0, 5000)))
    assert len(results) >= 1


def test_db_apply_unknown_algorithm():
    db = TimeSeriesDB()
    with pytest.raises(ValueError, match="not registered"):
        db.apply_algorithm("nope", [])
