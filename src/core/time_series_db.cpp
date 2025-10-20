#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/algorithms/algorithm_base.h"

namespace sage_tsdb {

TimeSeriesDB::TimeSeriesDB()
    : index_(std::make_unique<TimeSeriesIndex>()),
      query_count_(0),
      write_count_(0) {}

size_t TimeSeriesDB::add(const TimeSeriesData& data) {
    ++write_count_;
    return index_->add(data);
}

size_t TimeSeriesDB::add(int64_t timestamp, double value,
                         const Tags& tags, const Fields& fields) {
    TimeSeriesData data(timestamp, value, tags);
    data.fields = fields;
    return add(data);
}

size_t TimeSeriesDB::add(int64_t timestamp, const std::vector<double>& value,
                         const Tags& tags, const Fields& fields) {
    TimeSeriesData data(timestamp, value, tags);
    data.fields = fields;
    return add(data);
}

std::vector<size_t> TimeSeriesDB::add_batch(
    const std::vector<TimeSeriesData>& data_list) {
    write_count_ += data_list.size();
    return index_->add_batch(data_list);
}

std::vector<TimeSeriesData> TimeSeriesDB::query(
    const QueryConfig& config) const {
    ++query_count_;
    return index_->query(config);
}

std::vector<TimeSeriesData> TimeSeriesDB::query(
    const TimeRange& time_range, const Tags& filter_tags) const {
    QueryConfig config(time_range, filter_tags);
    return query(config);
}

void TimeSeriesDB::register_algorithm(
    const std::string& name,
    std::shared_ptr<TimeSeriesAlgorithm> algorithm) {
    algorithms_[name] = algorithm;
}

std::vector<TimeSeriesData> TimeSeriesDB::apply_algorithm(
    const std::string& name,
    const std::vector<TimeSeriesData>& data) const {
    auto it = algorithms_.find(name);
    if (it != algorithms_.end()) {
        return it->second->process(data);
    }
    return {};
}

bool TimeSeriesDB::has_algorithm(const std::string& name) const {
    return algorithms_.find(name) != algorithms_.end();
}

std::vector<std::string> TimeSeriesDB::list_algorithms() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : algorithms_) {
        names.push_back(name);
    }
    return names;
}

size_t TimeSeriesDB::size() const {
    return index_->size();
}

bool TimeSeriesDB::empty() const {
    return index_->empty();
}

void TimeSeriesDB::clear() {
    index_->clear();
    query_count_ = 0;
    write_count_ = 0;
}

std::map<std::string, int64_t> TimeSeriesDB::get_stats() const {
    return {
        {"size", static_cast<int64_t>(size())},
        {"query_count", query_count_},
        {"write_count", write_count_},
        {"algorithm_count", static_cast<int64_t>(algorithms_.size())}
    };
}

} // namespace sage_tsdb
