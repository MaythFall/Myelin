#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <numeric>
#include "myelin.hpp"

using namespace myelin;

struct BenchmarkPacket {
    uint64_t id;
    double val;
    std::string payload;
};

template <endian_policy Policy>
void run_benchmark(const std::string& label, int iterations) {
    mem_view<BenchmarkPacket, Policy> view;
    BenchmarkPacket p{ 123456789, 3.14159, "Velocity and Myelin" };

    // Warm-up
    for(int i = 0; i < 1000; ++i) {
        view.serialize(p);
        auto dummy = view.template get_field<0>();
        asm volatile("" : : "g"(dummy) : "memory");
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        view.serialize(p);
        
        // Force the CPU to actually complete the write
        asm volatile("" : : "r"(view.body_ptr) : "memory");
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::nano> elapsed = end - start;

    double avg_ns = elapsed.count() / iterations;
    double mops = 1000.0 / avg_ns;

    printf("%-20s | %8.2f ns/op | %8.2f MOps/s\n", label.c_str(), avg_ns, mops);
}

int main() {
    const int iterations = 1000000;

    std::cout << "Myelin Endianness Benchmark (" << iterations << " iterations)" << std::endl;
    std::cout << "====================================================" << std::endl;

    run_benchmark<endian_policy::network>("Network (Big)", iterations);
    run_benchmark<endian_policy::native>("Native (Little)", iterations);

    std::cout << "====================================================" << std::endl;

    return 0;
}