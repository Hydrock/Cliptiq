// Synthetic in-memory workload: no clipboard or personal history access.
#include "cliptiq/history.hpp"
#include "cliptiq/policy.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using namespace cliptiq;
constexpr std::size_t count = 1000;
template<class T> T take(Result<T> r) {
    if (!r) throw std::runtime_error(r.error().message);
    return std::move(r.value());
}
void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
struct Timing { const char* name; double elapsed_ms, cpu_ms; };
double process_cpu_ms() {
#ifdef _WIN32
    FILETIME creation, exit, kernel, user;
    require(GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) != 0,
            "Process CPU clock unavailable");
    const auto ticks = [](FILETIME value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel)+ticks(user)) / 10000.0;
#else
    auto ticks = std::clock();
    require(ticks != std::clock_t(-1), "Process CPU clock unavailable");
    return 1000.0 * static_cast<double>(ticks) / CLOCKS_PER_SEC;
#endif
}
template<class F> Timing measure(const char* name, F work) {
    auto start = std::chrono::steady_clock::now();
    auto cpu = process_cpu_ms();
    work();
    auto finish_cpu = process_cpu_ms();
    auto finish = std::chrono::steady_clock::now();
    return {name, std::chrono::duration<double, std::milli>(finish-start).count(),
            finish_cpu-cpu};
}
ClipboardSnapshot fixture(std::size_t i) {
    constexpr std::size_t sizes[]{128, 1024, 8192};
    constexpr char alphabet[] = " abcdefghijklmnopqrstuvwxyz0123456789\n";
    std::string text = "Synthetic clipboard item " + std::to_string(i) + ": ";
    std::uint32_t random = static_cast<std::uint32_t>(i+1);
    while (text.size() < sizes[i%3]) {
        random = random * 1664525u + 1013904223u;
        text += alphabet[(random >> 24) % (sizeof(alphabet)-1)];
    }
    ClipboardSnapshot snapshot;
    snapshot.sequence_token = std::to_string(i);
    snapshot.source = {"org.cliptiq.synthetic-benchmark", SourceConfidence::BestEffort};
    snapshot.representations.push_back({0, "benchmark", "text/plain", CanonicalKind::Text,
                                       Bytes(text.begin(), text.end())});
    return snapshot;
}
}

int main() {
    try {
        using namespace cliptiq;
        std::vector<ClipboardSnapshot> fixtures;
        for (std::size_t i=0; i<count; ++i) fixtures.push_back(fixture(i));
        auto store = take(open_store(":memory:"));
        HistoryPolicy history;
        history.max_count = count;
        CapturePolicy policy;
        Capabilities caps;
        caps.platform = CapturePlatform::X11;
        caps.capture = {CapabilityStatus::Available, "synthetic benchmark"};
        caps.native_types.push_back({"benchmark", "text/plain", CanonicalKind::Text});
        std::vector<EntryId> ids;
        ids.reserve(count);
        std::vector<Timing> timings;
        std::uint64_t payload_bytes=0, loaded_bytes=0, rows_observed=0;
        timings.push_back(measure("capture_ingest", [&] {
            for (std::size_t i=0; i<count; ++i) {
                auto decision = evaluate_capture(fixtures[i], policy, caps);
                require(decision.disposition == CaptureDisposition::Accept && decision.snapshot.has_value(), "capture rejected");
                auto id = take(ingest(store, std::move(*decision.snapshot), history, static_cast<std::int64_t>(i+1)));
                require(id.has_value(), "ingest did not retain item");
                ids.push_back(*id);
                payload_bytes += fixtures[i].representations[0].data.size();
            }
        }));
        auto rows = take(list_history(store, SortOrder::Created, count));
        require(rows.size() == count && rows.front().id == ids.back(), "history count/order mismatch");
        timings.push_back(measure("list_recent_30", [&] {
            for (std::size_t i=0; i<count; ++i) {
                auto recent = take(list_history(store, SortOrder::LastUsed, 30));
                require(recent.size() == 30 && recent.front().id == ids.back(), "recent history mismatch");
                rows_observed += recent.size();
            }
        }));
        timings.push_back(measure("load_snapshot", [&] {
            for (std::size_t i=0; i<count; ++i) {
                auto index = (i*37)%count;
                auto loaded = take(load_snapshot(store, ids[index]));
                require(loaded.representations.size() == 1 && loaded.representations[0].data == fixtures[index].representations[0].data, "loaded content mismatch");
                loaded_bytes += loaded.representations[0].data.size();
            }
        }));
        timings.push_back(measure("duplicate_ingest", [&] {
            for (std::size_t i=0; i<count; ++i) {
                auto id = take(ingest(store, fixtures[i], history, static_cast<std::int64_t>(count+i+1)));
                require(id && *id == ids[i], "duplicate identity mismatch");
            }
        }));
        timings.push_back(measure("touch_history", [&] {
            for (std::size_t i=0; i<count; ++i) {
                auto r = touch_history(store, ids[i], static_cast<std::int64_t>(2*count+i+1));
                if (!r) throw std::runtime_error(r.error().message);
            }
        }));
        rows = take(list_history(store, SortOrder::LastUsed, count));
        require(rows.size() == count && rows.front().id == ids.back() && loaded_bytes == payload_bytes, "final history mismatch");
        std::cout << std::fixed << std::setprecision(3)
                  << "{\"schema_version\":1,\"workload\":\"synthetic-memory-history-v1\",\"count\":" << count
                  << ",\"payload_bytes\":" << payload_bytes << ",\"loaded_bytes\":" << loaded_bytes
                  << ",\"rows_observed\":" << rows_observed << ",\"phases\":[";
        for (std::size_t i=0; i<timings.size(); ++i) {
            const auto& t = timings[i];
            if (i) std::cout << ',';
            std::cout << "{\"name\":\"" << t.name << "\",\"operations\":" << count
                      << ",\"elapsed_ms\":" << t.elapsed_ms << ",\"cpu_ms\":" << t.cpu_ms << '}';
        }
        std::cout << "],\"correct\":true}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Synthetic benchmark failed: " << e.what() << '\n';
        return 1;
    }
}
