// Load generator: drives the cluster through the routing client and reports,
// once per second, throughput, availability and hit rate, then latency
// percentiles at the end.
//
// Workload: keys drawn from a Zipf distribution (a few hot keys, a long tail),
// like real cache traffic. Reads follow the cache-aside pattern: on a miss the
// client "loads from the database" and writes the value into the cache.
//
// Usage: loadgen [--coord host:port] [--keys 10000] [--seconds 10]
//                [--read-ratio 0.9] [--value-size 32] [--zipf 0.99] [--preload 1]
//
// Definitions:
//   availability = successful requests / all requests
//   hit rate     = GETs that found the key / all successful GETs
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "distcache/client/client.h"
#include "distcache/common/time.h"

using namespace distcache;

struct Counters {
    uint64_t ops = 0, ok = 0, err = 0, hits = 0, misses = 0;
};

// Samples ranks 0..n-1 with P(rank k) proportional to 1 / (k+1)^s.
class Zipf {
public:
    Zipf(size_t n, double s) : cdf_(n) {
        double sum = 0;
        for (size_t k = 0; k < n; k++) {
            sum += 1.0 / std::pow(double(k + 1), s);
            cdf_[k] = sum;
        }
        for (double &c : cdf_) c /= sum;
    }
    size_t sample(std::mt19937_64 &rng) {
        double u = std::uniform_real_distribution<double>(0, 1)(rng);
        return size_t(std::lower_bound(cdf_.begin(), cdf_.end(), u) - cdf_.begin());
    }

private:
    std::vector<double> cdf_;
};

static double pct(uint64_t a, uint64_t b) { return b ? 100.0 * double(a) / double(b) : 0.0; }

int main(int argc, char **argv) {
    std::string coord = "127.0.0.1:7000";
    size_t num_keys = 10000, value_size = 32;
    int seconds = 10;
    double read_ratio = 0.9, zipf_s = 0.99;
    bool preload = true;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string f = argv[i], v = argv[i + 1];
        if (f == "--coord") coord = v;
        else if (f == "--keys") num_keys = std::strtoull(v.c_str(), nullptr, 10);
        else if (f == "--seconds") seconds = std::atoi(v.c_str());
        else if (f == "--read-ratio") read_ratio = std::atof(v.c_str());
        else if (f == "--value-size") value_size = std::strtoull(v.c_str(), nullptr, 10);
        else if (f == "--zipf") zipf_s = std::atof(v.c_str());
        else if (f == "--preload") preload = std::atoi(v.c_str()) != 0;
    }
    if (num_keys == 0 || seconds <= 0) {
        std::fprintf(stderr, "bad arguments\n");
        return 2;
    }

    ClusterClient client(coord);
    std::string err;
    if (!client.refresh(&err)) {
        std::fprintf(stderr, "cannot load cluster map from %s: %s\n", coord.c_str(), err.c_str());
        return 1;
    }
    std::string value(value_size, 'v');
    auto key_of = [](size_t k) { return "user:" + std::to_string(k); };

    if (preload) {
        size_t failed = 0;
        for (size_t k = 0; k < num_keys; k++) {
            if (client.call({"set", key_of(k), value}).is_err()) failed++;
        }
        std::fprintf(stderr, "preloaded %zu keys (%zu failed)\n", num_keys - failed, failed);
    }

    std::mt19937_64 rng(42);
    Zipf zipf(num_keys, zipf_s);
    std::uniform_real_distribution<double> coin(0, 1);
    std::vector<uint32_t> latencies_us;
    Counters total, sec;

    std::printf("%6s %9s %9s %9s %13s %10s\n", "second", "requests", "ok", "errors",
                "availability", "hit_rate");
    uint64_t start = mono_ms(), next_report = start + 1000;
    int second = 1;
    while (second <= seconds) {
        std::string key = key_of(zipf.sample(rng));
        bool is_read = coin(rng) < read_ratio;
        uint64_t t0 = mono_us();
        Value v = client.call(is_read ? std::vector<std::string>{"get", key}
                                      : std::vector<std::string>{"set", key, value});
        bool ok = !v.is_err();
        if (ok && is_read) {
            if (v.tag == TAG_STR) {
                sec.hits++;
            } else {
                sec.misses++;
                client.call({"set", key, value});  // cache-aside fill after a miss
            }
        }
        latencies_us.push_back(uint32_t(std::min<uint64_t>(mono_us() - t0, UINT32_MAX)));
        sec.ops++;
        (ok ? sec.ok : sec.err)++;

        if (mono_ms() >= next_report) {
            std::printf("%6d %9llu %9llu %9llu %12.2f%% %9.2f%%\n", second,
                        (unsigned long long)sec.ops, (unsigned long long)sec.ok,
                        (unsigned long long)sec.err, pct(sec.ok, sec.ops),
                        pct(sec.hits, sec.hits + sec.misses));
            std::fflush(stdout);
            total.ops += sec.ops;
            total.ok += sec.ok;
            total.err += sec.err;
            total.hits += sec.hits;
            total.misses += sec.misses;
            sec = Counters();
            next_report += 1000;
            second++;
        }
    }

    double elapsed_s = double(mono_ms() - start) / 1000.0;
    std::sort(latencies_us.begin(), latencies_us.end());
    auto pctl = [&](double p) {
        return latencies_us.empty() ? 0u : latencies_us[size_t(p * double(latencies_us.size() - 1))];
    };
    std::printf("\nrequests %llu, ok %llu, errors %llu, throughput %.0f req/s\n",
                (unsigned long long)total.ops, (unsigned long long)total.ok,
                (unsigned long long)total.err, double(total.ops) / elapsed_s);
    std::printf("availability %.2f%%, hit rate %.2f%%, map refreshes %llu\n",
                pct(total.ok, total.ops), pct(total.hits, total.hits + total.misses),
                (unsigned long long)client.refreshes());
    std::printf("latency p50 %u us, p99 %u us, max %u us\n", pctl(0.50), pctl(0.99), pctl(1.0));
    return 0;
}
