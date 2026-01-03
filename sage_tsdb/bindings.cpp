/**
 * @file bindings.cpp
 * @brief Python bindings for SAGE TSDB using pybind11
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <sstream>

#include "sage_tsdb/core/time_series_data.h"
#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/time_series_index.h"

namespace py = pybind11;
using namespace sage_tsdb;

PYBIND11_MODULE(_sage_tsdb, m) {
    m.doc() = "SAGE TSDB - High-performance time series database C++ bindings";

    // TimeSeriesData class
    py::class_<TimeSeriesData>(m, "TimeSeriesData")
        .def(py::init<>())
        .def(py::init<int64_t, double>(), 
             py::arg("timestamp"), 
             py::arg("value"))
        .def(py::init<int64_t, const std::vector<double>&>(),
             py::arg("timestamp"),
             py::arg("value"))
        .def_readwrite("timestamp", &TimeSeriesData::timestamp)
        .def_readwrite("value", &TimeSeriesData::value)
        .def_readwrite("tags", &TimeSeriesData::tags)
        .def_readwrite("fields", &TimeSeriesData::fields)
        .def("as_double", &TimeSeriesData::as_double,
             "Get value as double (first element if array)")
        .def("as_vector", &TimeSeriesData::as_vector,
             "Get value as vector")
        .def("is_scalar", &TimeSeriesData::is_scalar,
             "Check if value is scalar")
        .def("is_array", &TimeSeriesData::is_array,
             "Check if value is array")
        .def("__repr__", [](const TimeSeriesData& data) {
            std::ostringstream oss;
            oss << "<TimeSeriesData timestamp=" << data.timestamp 
                << " value=" << data.as_double() << ">";
            return oss.str();
        });

    // TimeRange class
    py::class_<TimeRange>(m, "TimeRange")
        .def(py::init<int64_t, int64_t>(),
             py::arg("start_time"),
             py::arg("end_time"))
        .def_readwrite("start_time", &TimeRange::start_time)
        .def_readwrite("end_time", &TimeRange::end_time)
        .def("__repr__", [](const TimeRange& range) {
            std::ostringstream oss;
            oss << "<TimeRange start=" << range.start_time 
                << " end=" << range.end_time << ">";
            return oss.str();
        });

    // QueryConfig class
    py::class_<QueryConfig>(m, "QueryConfig")
        .def(py::init<>())
        .def_readwrite("time_range", &QueryConfig::time_range)
        .def_readwrite("filter_tags", &QueryConfig::filter_tags)
        .def_readwrite("limit", &QueryConfig::limit);

    // TimeSeriesDB class
    py::class_<TimeSeriesDB>(m, "TimeSeriesDB")
        .def(py::init<>())
        .def("add", py::overload_cast<const TimeSeriesData&>(&TimeSeriesDB::add),
             py::arg("data"),
             "Add a single data point")
        .def("add", py::overload_cast<int64_t, double, const Tags&, const Fields&>(
                 &TimeSeriesDB::add),
             py::arg("timestamp"),
             py::arg("value"),
             py::arg("tags") = Tags{},
             py::arg("fields") = Fields{},
             "Add data with timestamp and scalar value")
        .def("add", py::overload_cast<int64_t, const std::vector<double>&, 
                 const Tags&, const Fields&>(&TimeSeriesDB::add),
             py::arg("timestamp"),
             py::arg("value"),
             py::arg("tags") = Tags{},
             py::arg("fields") = Fields{},
             "Add data with timestamp and vector value")
        .def("add_batch", &TimeSeriesDB::add_batch,
             py::arg("data_list"),
             "Add multiple data points")
        .def("query", py::overload_cast<const QueryConfig&>(
                 &TimeSeriesDB::query, py::const_),
             py::arg("config"),
             "Query with full configuration")
        .def("query", py::overload_cast<const TimeRange&, const Tags&>(
                 &TimeSeriesDB::query, py::const_),
             py::arg("time_range"),
             py::arg("filter_tags") = Tags{},
             "Query with time range")
        .def("size", &TimeSeriesDB::size,
             "Get number of data points")
        .def("clear", &TimeSeriesDB::clear,
             "Clear all data")
        .def("get_stats", &TimeSeriesDB::get_stats,
             "Get database statistics")
        .def("__len__", &TimeSeriesDB::size)
        .def("__repr__", [](const TimeSeriesDB& db) {
            std::ostringstream oss;
            oss << "<TimeSeriesDB size=" << db.size() << ">";
            return oss.str();
        });

    // TimeSeriesIndex class
    py::class_<TimeSeriesIndex>(m, "TimeSeriesIndex")
        .def(py::init<>())
        .def("add", &TimeSeriesIndex::add,
             py::arg("data"),
             "Add a single data point")
        .def("add_batch", &TimeSeriesIndex::add_batch,
             py::arg("data_list"),
             "Add multiple data points")
        .def("query", &TimeSeriesIndex::query,
             py::arg("config"),
             "Query data within time range")
        .def("get", &TimeSeriesIndex::get,
             py::arg("index"),
             "Get data point by index")
        .def("clear", &TimeSeriesIndex::clear,
             "Clear index")
        .def("size", &TimeSeriesIndex::size,
             "Get index size")
        .def("empty", &TimeSeriesIndex::empty,
             "Check if index is empty")
        .def("__len__", &TimeSeriesIndex::size);
}
