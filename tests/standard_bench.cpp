#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <array>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <list>
#include <deque>
#include "myelin/myelin.hpp"

using namespace std;
using namespace std::chrono;

struct ScalarPacket { uint64_t t; double v; int32_t i; bool s; };
struct ComplexPacket { 
    uint64_t t; double v; int32_t h; bool a; 
    std::string u; std::vector<uint32_t> l; 
};

// Updated Messy Struct: Now including the "Non-Contiguous Quad"
struct MessyPacket {
    std::map<uint32_t, float> sensors; 
    std::list<int32_t> history;
    std::deque<double> stream;
    std::set<uint32_t> ids; // Added Set support
};

const int ITERS = 1000000;

void print_row(string op, double dur_ns) {
    double mops = 1000.0 / dur_ns;
    printf("| %-18s | %8.2f ns | %8.2f Mops/s |\n", op.c_str(), dur_ns, mops);
}

void run_all_benchmarks() {
    ScalarPacket s_p{1712345678, 123.456, 42, true};
    ComplexPacket c_p{1712345678, 299.79, 95, true, "Adam_Brazda", {7, 13, 42, 101, 202}};
    
    MessyPacket m_p;
    m_p.sensors = {{101, 1.1f}, {202, 2.2f}, {303, 3.3f}};
    m_p.history = {10, 20, 30, 40, 50};
    m_p.stream  = {0.123, 0.456, 0.789};
    m_p.ids     = {1001, 1002, 1003, 1004, 1005};

    std::array<std::string_view, 6> names = {"time", "vel", "hp", "active", "user", "stats"};

    printf("Myelin Comprehensive Performance | Ryzen 3900X\n");
    printf("| Operation          | Latency     | Throughput    |\n");
    printf("| ------------------ | ----------- | ------------- |\n");

    // --- HEAP-BASED (mem_view) ---
    {
        myelin::mem_view<ScalarPacket> s_mem;
        myelin::mem_view<ComplexPacket> c_mem;
        myelin::mem_view<MessyPacket> m_mem;

        auto start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { s_mem.serialize(s_p); asm volatile(""::"r"(s_mem.body_ptr):"memory"); }
        print_row("Mem Ser (Scalar)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { c_mem.serialize(c_p); asm volatile(""::"r"(c_mem.body_ptr):"memory"); }
        print_row("Mem Ser (Complex)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        // Messy Serialize: Now traversing Map, List, Deque, AND Set
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { m_mem.serialize(m_p); asm volatile(""::"r"(m_mem.body_ptr):"memory"); }
        print_row("Mem Ser (Messy)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*10; ++i) { auto v = c_mem.get_field<1>(); asm volatile(""::"r"(v):"memory"); }
        print_row("Mem Access", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*10));

        // Map Lookup
        m_mem.serialize(m_p);
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*5; ++i) {
            auto blob = m_mem.get_field<0>(); 
            myelin::std_map_view<uint32_t, float, true> mv;
            myelin::mapify(blob, mv);
            auto res = mv.find(202);
            asm volatile(""::"r"(res):"memory");
        }
        print_row("Mem Map Lookup", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*5));

        // NEW: Set Lookup (contains)
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*5; ++i) {
            auto blob = m_mem.get_field<3>(); // Field 3 is our Set
            myelin::std_set_view<uint32_t, true> sv;
            myelin::setify(blob, sv);
            bool has = sv.contains(1003);
            asm volatile(""::"r"(has):"memory");
        }
        print_row("Mem Set Lookup", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*5));

        start = high_resolution_clock::now();
        for(int i=0; i<ITERS/10; ++i) { std::string s = c_mem.to_json(names); asm volatile(""::"r"(s.data()):"memory"); }
        print_row("Mem JSON", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS/10));
    }

    printf("| ------------------ | ----------- | ------------- |\n");

    // --- DISK-BACKED (map_view) ---
    {
        myelin::map_view<ScalarPacket> s_map; s_map.map("./s.bin");
        myelin::map_view<ComplexPacket> c_map; c_map.map("./c.bin");
        myelin::map_view<MessyPacket> m_map; m_map.map("./m.bin");

        auto start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { s_map.serialize(s_p); asm volatile(""::"r"(s_map.body_ptr):"memory"); }
        print_row("Mmap Ser (Scalar)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { c_map.serialize(c_p); asm volatile(""::"r"(c_map.body_ptr):"memory"); }
        print_row("Mmap Ser (Complex)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        start = high_resolution_clock::now();
        for(int i=0; i<ITERS; ++i) { m_map.serialize(m_p); asm volatile(""::"r"(m_map.body_ptr):"memory"); }
        print_row("Mmap Ser (Messy)", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/ITERS);

        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*10; ++i) { auto v = c_map.get_field<1>(); asm volatile(""::"r"(v):"memory"); }
        print_row("Mmap Access", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*10));

        // Mmap Map Lookup
        m_map.serialize(m_p);
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*5; ++i) {
            auto blob = m_map.get_field<0>();
            myelin::std_map_view<uint32_t, float, true> mv;
            myelin::mapify(blob, mv);
            auto res = mv.find(202);
            asm volatile(""::"r"(res):"memory");
        }
        print_row("Mmap Map Lookup", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*5));

        // NEW: Mmap Set Lookup
        start = high_resolution_clock::now();
        for(int i=0; i<ITERS*5; ++i) {
            auto blob = m_map.get_field<3>();
            myelin::std_set_view<uint32_t, true> sv;
            myelin::setify(blob, sv);
            bool has = sv.contains(1003);
            asm volatile(""::"r"(has):"memory");
        }
        print_row("Mmap Set Lookup", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS*5));

        start = high_resolution_clock::now();
        for(int i=0; i<ITERS/10; ++i) { std::string s = c_map.to_json(names); asm volatile(""::"r"(s.data()):"memory"); }
        print_row("Mmap JSON", (double)duration_cast<nanoseconds>(high_resolution_clock::now()-start).count()/(ITERS/10));

        std::filesystem::remove("./s.bin");
        std::filesystem::remove("./c.bin");
        std::filesystem::remove("./m.bin");
    }
}

int main() {
    run_all_benchmarks();
    return 0;
}