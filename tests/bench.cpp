#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>
#include <iomanip>

// Your Engine
#include "myelin.hpp"

// Competitors (Generated headers)
#include "schema_generated.h" // from flatc
#include "schema.pb.h"        // from protoc

using namespace std::chrono;

struct BenchSchema {
    uint64_t id;
    double timestamp;
    std::string text;
};

void print_result(std::string name, nanoseconds total, size_t iterations) {
    double ns_op = static_cast<double>(total.count()) / iterations;
    double mops = 1000.0 / ns_op;
    std::cout << std::left << std::setw(20) << name 
              << "| " << std::setw(10) << std::fixed << std::setprecision(2) << ns_op << " ns/op "
              << "| " << std::setw(10) << mops << " Mops/s" << std::endl;
}

int main(int argc, char** argv) {
    size_t N = (argc > 1) ? std::stoull(argv[1]) : 1000000;
    BenchSchema msg{ 1337, 1712345678.9, "Velocity and Myelin" };

    std::cout << "Running Benchmarks (" << N << " iterations)\n";
    std::cout << "====================================================\n";

    // --- 1. Myelin Benchmark ---
    {
        myelin::mem_view<BenchSchema> view;
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            view.serialize(msg);
            asm volatile("" : : "g"(view.body_ptr) : "memory");
        }
        auto end = high_resolution_clock::now();
        print_result("Myelin", duration_cast<nanoseconds>(end - start), N);
    }

    // --- 2. FlatBuffers Benchmark ---
    {
        flatbuffers::FlatBufferBuilder builder(1024);
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            builder.Clear();
            auto text_off = builder.CreateString(msg.text);
            auto pkt_off = bench_fb::CreatePacket(builder, msg.id, msg.timestamp, text_off);
            builder.Finish(pkt_off);
            asm volatile("" : : "g"(builder.GetBufferPointer()) : "memory");
        }
        auto end = high_resolution_clock::now();
        print_result("FlatBuffers", duration_cast<nanoseconds>(end - start), N);
    }

    // --- 3. Protobuf Benchmark ---
    {
        bench_pb::Packet pkt;
        pkt.set_id(msg.id);
        pkt.set_timestamp(msg.timestamp);
        pkt.set_text(msg.text);
        
        size_t sz = pkt.ByteSizeLong();
        std::vector<uint8_t> buffer(sz);
        
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            pkt.SerializeToArray(buffer.data(), (int)sz);
            asm volatile("" : : "g"(buffer.data()) : "memory");
        }
        auto end = high_resolution_clock::now();
        print_result("Protobuf", duration_cast<nanoseconds>(end - start), N);
    }

    return 0;
}