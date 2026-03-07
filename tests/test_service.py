"""Tests for SageTSDBService."""

from sage_tsdb import SageTSDBService, SageTSDBServiceConfig


def test_service_add_and_query():
    svc = SageTSDBService()
    svc.add(1_000, 1.0, tags={"s": "A"})
    svc.add(2_000, 2.0, tags={"s": "A"})
    svc.add(3_000, 3.0, tags={"s": "B"})

    results = svc.query(start_time=500, end_time=2_500)
    assert len(results) == 2


def test_service_add_batch():
    svc = SageTSDBService()
    indices = svc.add_batch(
        timestamps=[1000, 2000, 3000],
        values=[1.0, 2.0, 3.0],
    )
    assert len(indices) == 3


def test_service_query_with_aggregation():
    svc = SageTSDBService()
    for ts, v in [(0, 1.0), (1000, 2.0), (2000, 3.0), (3000, 4.0)]:
        svc.add(ts, v)
    results = svc.query(0, 4000, aggregation="avg", window_size=2000)
    assert len(results) >= 1


def test_service_stream_join():
    svc = SageTSDBService()
    left = [{"timestamp": 1000, "value": 1.0}, {"timestamp": 2000, "value": 2.0}]
    right = [{"timestamp": 1100, "value": 1.5}, {"timestamp": 2100, "value": 2.5}]
    result = svc.stream_join(left, right, window_size=5000, max_delay=1000)
    assert isinstance(result, list)


def test_service_window_aggregate():
    svc = SageTSDBService()
    for ts, v in [(0, 1.0), (1000, 2.0), (2000, 3.0)]:
        svc.add(ts, v)
    results = svc.window_aggregate(
        start_time=0,
        end_time=5000,
        window_type="tumbling",
        window_size=3000,
        aggregation="avg",
    )
    assert len(results) >= 1


def test_service_stats():
    svc = SageTSDBService()
    svc.add(1000, 1.0)
    svc.query(0, 2000)
    st = svc.stats()
    assert st["total_writes"] == 1
    assert st["total_queries"] == 1


def test_service_reset():
    svc = SageTSDBService()
    svc.add(1000, 1.0)
    svc.reset()
    st = svc.stats()
    assert st["total_writes"] == 0
    assert st["db_size"] == 0


def test_service_custom_config():
    cfg = SageTSDBServiceConfig(default_window_size=10_000, default_aggregation="sum")
    svc = SageTSDBService(config=cfg)
    assert svc._config.default_aggregation == "sum"
