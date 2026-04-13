#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <array>
#include <iomanip>
#include "myelin_src/myelin.hpp"

using namespace std;
using namespace std::chrono;

// --- Modern Capabilities Schema ---
struct Physics { 
    float x, y; 
}; // Trivial Nest

struct Meta { 
    uint64_t id; 
    std::string tag; 
}; // True Nest (Recursive)

struct MegaPacket {
    uint32_t version;
    Physics pos;
    Meta info;
    std::vector<int32_t> logs;
    double entropy;
};

void benchmark_json() {
    // 1. Setup high-complexity data
    MegaPacket p{
        .version = 42,
        .pos = { 1.5f, 9.8f },
        .info = { 0xABCDEF, "NODE_01_DEEP_SPACE" },
        .logs = { 101, 202, 303, 404, 505 },
        .entropy = 0.9979
    };

    // Field names for the named benchmark
    std::array<std::string_view, 5> names = {
        "ver", "physics", "metadata", "log_stream", "system_entropy"
    };
    
    myelin::mem_view<MegaPacket> view;
    view.serialize(p);

    const int ITERS = 100000;
    cout << "--- Myelin JSON Performance Profile | Ryzen 3900X ---" << endl;
    cout << "Packet Size: " << view.total_size() << " bytes" << endl;
    cout << "Iterations:  " << ITERS << endl;
    cout << "------------------------------------------------------------" << endl;

    // --- TEST 1: RAW OUTPUT (Sanity Check) ---
    cout << "Sample Output (Named):\n" << view.to_json(names) << "\n" << endl;

    // --- TEST 2: Benchmark Index-based JSON ---
    auto start = high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
        std::string s = view.to_json();
        // Prevent compiler from optimizing away the string creation
        asm volatile("" : : "r"(s.data()) : "memory");
    }
    auto end = high_resolution_clock::now();
    auto dur = duration_cast<nanoseconds>(end - start).count();
    printf("JSON (Index-Only)  | Avg: %8.2f ns | ~%.2f us\n", 
           (double)dur / ITERS, (double)dur / ITERS / 1000.0);

    // --- TEST 3: Benchmark Named-field JSON ---
    start = high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
        std::string s = view.to_json(names);
        asm volatile("" : : "r"(s.data()) : "memory");
    }
    end = high_resolution_clock::now();
    dur = duration_cast<nanoseconds>(end - start).count();
    printf("JSON (Named-Keys)  | Avg: %8.2f ns | ~%.2f us\n", 
           (double)dur / ITERS, (double)dur / ITERS / 1000.0);
}

int main() {
    benchmark_json();
    return 0;
}