#include "sage_tsdb/core/time_series_data.h"
#include <stdexcept>

namespace sage_tsdb {

double TimeSeriesData::as_double() const {
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    } else if (std::holds_alternative<std::vector<double>>(value)) {
        const auto& vec = std::get<std::vector<double>>(value);
        if (!vec.empty()) {
            return vec[0];
        }
    }
    return 0.0;
}

std::vector<double> TimeSeriesData::as_vector() const {
    if (std::holds_alternative<std::vector<double>>(value)) {
        return std::get<std::vector<double>>(value);
    } else if (std::holds_alternative<double>(value)) {
        return {std::get<double>(value)};
    }
    return {};
}

} // namespace sage_tsdb
