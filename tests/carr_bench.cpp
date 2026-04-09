#include <iostream>
#include <chrono>
#include <vector>
#include "myelin_src/myelin.hpp"

struct DataPoint {
    uint64_t id;
    std::array<std::array<float, 4>, 4> matrix;
    std::array<uint8_t, 8> voxels;
    double timestamp;
};

void run_serialization_benchmark() {
    const size_t ITERATIONS = 10'000'000; // 10 Million serializations
    
    DataPoint point{};
    point.id = 0xDEADBEEF;
    point.timestamp = 1234567.89;
    for(int i=0; i<4; ++i) point.matrix[i][i] = 1.0f;

    myelin::mem_view<DataPoint> view;

    std::cout << "[MYELIN] Starting Serialization Benchmark (" << ITERATIONS << " iterations)..." << std::endl;

    // --- Benchmark: Myelin Serialize ---
    auto start = std::chrono::high_resolution_clock::now();
    for(size_t i = 0; i < ITERATIONS; ++i) {
        // This triggers resize_calc and pack_data_bucketed
        view.serialize(point);
        
        // Slightly modify data to prevent compiler from hoisting the call
        point.id++; 
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double> myelin_dur = end - start;
    double ops_per_sec = ITERATIONS / myelin_dur.count();
    double ns_per_op = (myelin_dur.count() * 1e9) / ITERATIONS;

    std::cout << "Total Time:      " << myelin_dur.count() << "s" << std::endl;
    std::cout << "Throughput:      " << (ops_per_sec / 1e6) << " million ops/sec" << std::endl;
    std::cout << "Latency:         " << ns_per_op << " ns per serialize()" << std::endl;

    // --- Comparative: Raw Memcpy (Theoretical Limit) ---
    uint8_t buffer[256];
    start = std::chrono::high_resolution_clock::now();
    for(size_t i = 0; i < ITERATIONS; ++i) {
        std::memcpy(buffer, &point, sizeof(DataPoint));
        point.id++;
    }
    end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> memcpy_dur = end - start;
    
    std::cout << "\nRaw Memcpy Time: " << memcpy_dur.count() << "s" << std::endl;
    std::cout << "Myelin Overhead: " << (ns_per_op - (memcpy_dur.count() * 1e9 / ITERATIONS)) << " ns" << std::endl;
}

int main() {
    run_serialization_benchmark();
    return 0;
}