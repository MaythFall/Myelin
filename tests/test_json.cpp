#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <array>
#include "myelin.hpp"

using namespace std;
using namespace std::chrono;

struct TelemetryPacket {
    uint64_t timestamp;
    double velocity;
    int32_t health;
    bool is_active;
    std::string username;
    std::vector<uint32_t> lucky_numbers;
};

void benchmark_json() {
    TelemetryPacket p{1712345678, 299.79, 95, true, "Adam_Brazda", {7, 13, 42, 101, 202}};
    std::array<std::string_view, 6> names = {"time", "vel", "hp", "active", "user", "stats"};
    
    myelin::mem_view<TelemetryPacket, myelin::endian_policy::native> view;
    view.serialize(p);

    const int ITERS = 100000;
    cout << "Benchmarking to_json (" << ITERS << " ops) | Ryzen 3900X" << endl;
    cout << "------------------------------------------------------------" << endl;

    // --- Benchmark Index-based JSON ---
    auto start = high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
        std::string s = view.to_json();
        asm volatile("" : : "r"(s.data()) : "memory");
    }
    auto end = high_resolution_clock::now();
    auto dur = duration_cast<nanoseconds>(end - start).count();
    printf("JSON (Indices):    %8.2f ns/op (~%.2f us)\n", (double)dur / ITERS, (double)dur / ITERS / 1000.0);

    // --- Benchmark Named-field JSON ---
    start = high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
        std::string s = view.to_json(names);
        asm volatile("" : : "r"(s.data()) : "memory");
    }
    end = high_resolution_clock::now();
    dur = duration_cast<nanoseconds>(end - start).count();
    printf("JSON (Named):      %8.2f ns/op (~%.2f us)\n", (double)dur / ITERS, (double)dur / ITERS / 1000.0);
}

int main() {
    benchmark_json();
    return 0;
}