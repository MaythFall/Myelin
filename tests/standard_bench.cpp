#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <list>
#include <deque>
#include "myelin/myelin.hpp"

using namespace std;
using namespace std::chrono;

// --- PACKET DEFINITIONS ---
struct ScalarPacket { uint64_t t; double v; int32_t i; bool s; };

struct ComplexPacket { 
    uint64_t t; double v; int32_t h; bool a; 
    std::string u; std::vector<uint32_t> l; 
};

// 2D Array Stress
struct MatrixPacket {
    std::array<std::array<float, 4>, 4> transform;
    std::array<std::array<uint8_t, 16>, 16> heatmap;
};

// Recursive Struct Stress
struct Inner { float x, y, z; };
struct NestedPacket {
    uint32_t id;
    Inner a; Inner b; Inner c; // Multiple nested jumps
    uint64_t timestamp;
};

const int ITERS = 1000000;

void print_row(string op, double dur_ns) {
    double mops = 1000.0 / dur_ns;
    printf("| %-22s | %8.2f ns | %8.2f Mops/s |\n", op.c_str(), dur_ns, mops);
}

void run_bench() {
    ScalarPacket s_p{1712345678, 123.456, 42, true};
    ComplexPacket c_p{1712345678, 299.79, 95, true, "Adam_Brazda", {7, 13, 42}};
    MatrixPacket m_x{}; m_x.transform[2][2] = 1.0f;
    NestedPacket n_p{1, {1,1,1}, {2,2,2}, {3,3,3}, 99999};

    printf("Myelin Mega Benchmark | Ryzen 3900X\n");
    printf("| Operation              | Latency     | Throughput    |\n");
    printf("| ---------------------- | ----------- | ------------- |\n");

    auto perform = [&](auto& myelin_obj, string label, auto& data_source) {
        // Serialization
        auto start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { myelin_obj.serialize(data_source); asm volatile(""::"r"(myelin_obj.body_ptr):"memory"); }
        print_row(label + " Ser", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        // Access (Standard)
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*5; ++i) { auto v = myelin_obj.template get_field<0>(); asm volatile(""::"r"(v):"memory"); }
        print_row(label + " Access", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*5));
    };

    // --- MEMORY BACKED ---
    {
        myelin::mem_view<ScalarPacket> s_mem;
        myelin::mem_view<MatrixPacket> x_mem;
        myelin::mem_view<NestedPacket> n_mem;

        perform(s_mem, "Mem Scalar", s_p);
        perform(x_mem, "Mem 2D Array", m_x);
        perform(n_mem, "Mem Nested", n_p);

        // Special 2D Indexing test (Read-only math)
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
        myelin::map_view<NestedPacket> n_map; n_map.map("./n.bin");

        perform(s_map, "Mmap Scalar", s_p);
        perform(x_map, "Mmap 2D Array", m_x);
        perform(n_map, "Mmap Nested", n_p);

        std::filesystem::remove("./s.bin");
        std::filesystem::remove("./x.bin");
        std::filesystem::remove("./n.bin");
    }
}

int main() { run_bench(); return 0; }