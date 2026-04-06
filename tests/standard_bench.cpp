#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <array>
#include <filesystem>
#include <iomanip>
#include "myelin.hpp"

using namespace std;
using namespace std::chrono;

struct ScalarPacket { uint64_t t; double v; int32_t i; bool s; };
struct ComplexPacket { 
    uint64_t t; double v; int32_t h; bool a; 
    std::string u; std::vector<uint32_t> l; 
};

const int ITERS = 1000000;

// Helper to print results in the README format
void print_row(string op, double dur_ns) {
    double mops = 1000.0 / dur_ns;
    printf("| %-18s | %8.2f ns | %8.2f Mops/s |\n", op.c_str(), dur_ns, mops);
}

void run_all_benchmarks() {
    ScalarPacket s_p{1712345678, 123.456, 42, true};
    ComplexPacket c_p{1712345678, 299.79, 95, true, "Adam_Brazda", {7, 13, 42, 101, 202}};
    std::array<std::string_view, 6> names = {"time", "vel", "hp", "active", "user", "stats"};

    printf("Myelin Comprehensive Performance | Ryzen 3900X\n");
    printf("| Operation          | Latency     | Throughput    |\n");
    printf("| ------------------ | ----------- | ------------- |\n");

    // --- HEAP-BASED (mem_view) ---
    {
        myelin::mem_view<ScalarPacket> s_mem;
        myelin::mem_view<ComplexPacket> c_mem;

        // Scalar Serialize
        auto start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { s_mem.serialize(s_p); asm volatile(""::"r"(s_mem.body_ptr):"memory"); }
        print_row("Mem Ser (Scalar)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        // Complex Serialize
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { c_mem.serialize(c_p); asm volatile(""::"r"(c_mem.body_ptr):"memory"); }
        print_row("Mem Ser (Complex)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        // Field Access
        c_mem.serialize(c_p);
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*10; ++i) { auto v = c_mem.get_field<1>(); asm volatile(""::"r"(v):"memory"); }
        print_row("Mem Access", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*10));

        // JSON Export
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS/10; ++i) { std::string s = c_mem.to_json(names); asm volatile(""::"r"(s.data()):"memory"); }
        print_row("Mem JSON", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS/10));
    }

    printf("| ------------------ | ----------- | ------------- |\n");

    // --- DISK-BACKED (map_view) ---
    {
        myelin::map_view<ScalarPacket> s_map; s_map.map("./s.bin");
        myelin::map_view<ComplexPacket> c_map; c_map.map("./c.bin");

        // Scalar Serialize
        auto start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { s_map.serialize(s_p); asm volatile(""::"r"(s_map.body_ptr):"memory"); }
        print_row("Mmap Ser (Scalar)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        // Complex Serialize
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { c_map.serialize(c_p); asm volatile(""::"r"(c_map.body_ptr):"memory"); }
        print_row("Mmap Ser (Complex)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        // Field Access
        c_map.serialize(c_p);
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*10; ++i) { auto v = c_map.get_field<1>(); asm volatile(""::"r"(v):"memory"); }
        print_row("Mmap Access", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*10));

        // JSON Export
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS/10; ++i) { std::string s = c_map.to_json(names); asm volatile(""::"r"(s.data()):"memory"); }
        print_row("Mmap JSON", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS/10));

        std::filesystem::remove("./s.bin");
        std::filesystem::remove("./c.bin");
    }
}

int main() {
    run_all_benchmarks();
    return 0;
}