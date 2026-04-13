#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <array>
#include <filesystem>
#include "myelin/myelin.hpp"

using namespace std;
using namespace std::chrono;

// --- PACKET DEFINITIONS ---
struct ScalarPacket { uint64_t t; double v; int32_t i; bool s; };

// 2D Array Stress
struct MatrixPacket {
    std::array<std::array<float, 4>, 4> transform;
    std::array<std::array<uint8_t, 16>, 16> heatmap;
};

// Form 1: Trivial Nesting (Flat path)
struct Inner { float x, y, z; };
struct TrivialNested {
    uint32_t id;
    Inner a; Inner b; Inner c; 
    uint64_t timestamp;
};

// Form 2: True Nesting (Recursive path)
struct ComplexInner {
    uint64_t id;
    std::string label; // Forces non-triviality
};
struct DeeplyNested {
    uint32_t version;
    ComplexInner data;
    std::vector<int> payload;
};

const int ITERS = 1000000;

void print_row(string op, double dur_ns) {
    double mops = 1000.0 / dur_ns;
    printf("| %-22s | %8.2f ns | %8.2f Mops/s |\n", op.c_str(), dur_ns, mops);
}

void run_bench() {
    ScalarPacket s_p{1712345678, 123.456, 42, true};
    MatrixPacket m_x{}; m_x.transform[2][2] = 1.0f;
    TrivialNested t_n{1, {1,1,1}, {2,2,2}, {3,3,3}, 99999};
    DeeplyNested d_n{1, {0xDEADBEEF, "SENSOR_ALPHA_9"}, {10, 20, 30, 40, 50}};

    printf("Myelin Mega Benchmark | Ryzen 3900X\n");
    printf("| Operation              | Latency     | Throughput    |\n");
    printf("| ---------------------- | ----------- | ------------- |\n");

    auto perform = [&](auto& myelin_obj, string label, auto& data_source) {
        // Serialization (with mutation to prevent hoisting)
        auto start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { 
            // Small mutation
            if constexpr (requires { data_source.t; }) data_source.t++; 
            myelin_obj.serialize(data_source); 
            asm volatile(""::"r"(myelin_obj.body_ptr):"memory"); 
        }
        print_row(label + " Ser", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        // Access (Standard)
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*5; ++i) { 
            auto v = myelin_obj.template get_field<0>(); 
            asm volatile(""::"r"(v):"memory"); 
        }
        print_row(label + " Access", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*5));
    };

    // --- MEMORY BACKED ---
    {
        myelin::mem_view<ScalarPacket> s_mem;
        myelin::mem_view<MatrixPacket> x_mem;
        myelin::mem_view<TrivialNested> t_mem;
        myelin::mem_view<DeeplyNested> d_mem;

        perform(s_mem, "Mem Scalar", s_p);
        perform(x_mem, "Mem 2D Array", m_x);
        perform(t_mem, "Mem Trivial Nest", t_n);
        perform(d_mem, "Mem True Nest", d_n);

        // 2D Index Math
        x_mem.serialize(m_x);
        auto start = high_resolution_clock::now();
        for(int i=0; i<ITERS*10; ++i) {
            float val = x_mem.get_field<0>()[2][2];
            asm volatile(""::"r"(val):"memory");
        }
        print_row("2D Index Math", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*10));
    }

    printf("| ---------------------- | ----------- | ------------- |\n");

    // --- DISK BACKED (MMAP) ---
    {
        myelin::map_view<ScalarPacket> s_map; s_map.map("./s.bin");
        myelin::map_view<MatrixPacket> x_map; x_map.map("./x.bin");
        myelin::map_view<TrivialNested> t_map; t_map.map("./t.bin");
        myelin::map_view<DeeplyNested> d_map; d_map.map("./d.bin");

        perform(s_map, "Mmap Scalar", s_p);
        perform(x_map, "Mmap 2D Array", m_x);
        perform(t_map, "Mmap Trivial Nest", t_n);
        perform(d_map, "Mmap True Nest", d_n);

        std::filesystem::remove("./s.bin");
        std::filesystem::remove("./x.bin");
        std::filesystem::remove("./t.bin");
        std::filesystem::remove("./d.bin");
    }
}

int main() { run_bench(); return 0; }