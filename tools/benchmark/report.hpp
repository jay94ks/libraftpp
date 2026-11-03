#ifndef RAFTPP_BENCHMARK_REPORT_HPP
#define RAFTPP_BENCHMARK_REPORT_HPP

// Replaces cowsql/raft tools/benchmark/report.c with standard values/algorithms;
// see LICENSE.md. Durations are nanoseconds; rates are operations per second.
#include <raftpp/common.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace raftpp::benchmark {
    using clock = chrono::steady_clock;
    inline uint64_t elapsed(clock::time_point start) noexcept {
        return static_cast<uint64_t>(
            chrono::duration_cast<chrono::nanoseconds>(clock::now() - start).count());
    }
    struct metric {
        double value = 0, lower_bound = 0, upper_bound = 0;
    };
    enum class metric_kind { latency, throughput };
    class latency_samples {
    public:
        explicit latency_samples(size_t expected = 0) {
            values_.reserve(expected);
        }
        void add(uint64_t nanoseconds) {
            values_.push_back(nanoseconds);
        }
        size_t size() const noexcept {
            return values_.size();
        }
        metric summarize() {
            if (values_.empty())
                throw invalid_argument("latency measurement has no samples");
            const auto bounds = std::minmax_element(values_.begin(), values_.end());
            const auto minimum = *bounds.first, maximum = *bounds.second;
            const auto median = values_.begin() + static_cast<ptrdiff_t>((values_.size() - 1) / 2);
            std::nth_element(values_.begin(), median, values_.end());
            return {static_cast<double>(*median), static_cast<double>(minimum),
                    static_cast<double>(maximum)};
        }

    private:
        vector<uint64_t> values_;
    };
    inline metric throughput(double operations, uint64_t nanoseconds) {
        if (!std::isfinite(operations) || operations < 0 || nanoseconds == 0 ||
            nanoseconds > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
            throw invalid_argument("invalid throughput measurement");
        const auto seconds = chrono::duration<double>{
            chrono::nanoseconds{static_cast<int64_t>(
                nanoseconds)}}.count();
        if (seconds <= 0)
            throw invalid_argument("throughput duration is out of range");
        return {operations / seconds, 0, 0};
    }
    class report {
    public:
        void add(string name, metric_kind kind, metric value) {
            if (name.empty() || !std::ranges::all_of(name, [](unsigned char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == ':' || c == '-' || c == '_';
                }))
                throw invalid_argument("invalid benchmark name");
            if ((kind != metric_kind::latency && kind != metric_kind::throughput) ||
                !std::isfinite(value.value) || !std::isfinite(value.lower_bound) ||
                !std::isfinite(value.upper_bound) || value.value < 0 || value.lower_bound < 0 ||
                value.upper_bound < 0)
                throw invalid_argument("invalid benchmark metric");
            if (!values_[std::move(name)].emplace(kind, value).second)
                throw invalid_argument("duplicate benchmark metric");
        }
        string json() const {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::setprecision(17) << "{\n";
            bool first = true;
            for (const auto& [name, metrics] : values_) {
                if (!first)
                    output << ",\n";
                first = false;
                output << "  \"" << name << "\": {\n";
                bool first_metric = true;
                for (const auto& [kind, value] : metrics) {
                    if (!first_metric)
                        output << ",\n";
                    first_metric = false;
                    output << "    \"" << (kind == metric_kind::latency ? "latency" : "throughput")
                           << "\": {\n"
                           << "      \"value\": " << value.value << ",\n"
                           << "      \"lower_bound\": " << value.lower_bound << ",\n"
                           << "      \"upper_bound\": " << value.upper_bound << "\n    }";
                }
                output << "\n  }";
            }
            output << "\n}\n";
            return output.str();
        }

    private:
        map<string, map<metric_kind, metric>> values_;
    };
}
#endif // RAFTPP_BENCHMARK_REPORT_HPP
