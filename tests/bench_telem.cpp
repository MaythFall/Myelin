#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <span>
#include <bit>

// Myelin Header
#include "myelin.hpp"

// Protobuf Generated Header
#include "telemetry.pb.h"

// FlatBuffers Generated Header
#include "telemetry_generated.h"

using namespace std;
using namespace std::chrono;

// Renamed to avoid collision with generated 'Telemetry' classes
struct RawTelemetry {
    uint64_t timestamp;
    uint32_t session_id;
    double pos_x, pos_y, pos_z;
    float health;
    int32_t ammo;
    bool is_active;
    string username;
    string action_log;
};

// Generate 1000 unique packets to stress the CPU branch predictor
vector<RawTelemetry> generate_heavy_data(size_t count) {
    vector<RawTelemetry> data;
    for (size_t i = 0; i < count; ++i) {
        data.push_back({
            1712345678 + i, 
            (uint32_t)(100 + i),
            10.5 + i, 20.5 + i, 30.5 + i,
            99.0f - (i % 10),
            (int32_t)(30 - (i % 30)),
            (i % 2 == 0),
            "User_ID_Sequence_" + to_string(i),
            "LOG_EVENT_CRITICAL_LATENCY_AVX2_TEST_" + to_string(i)
        });
    }
    return data;
}

// --- Myelin Benchmark ---
template<myelin::endian_policy Policy = myelin::endian_policy::native>
void bench_myelin(const vector<RawTelemetry>& data, int iterations) {
    myelin::mem_view<RawTelemetry, Policy> view;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        const auto& d = data[i % data.size()];
        view.serialize(d);
        
        // The "Snap": Grabbing the usable memory region
        std::span<uint8_t> snap(view.body_ptr, view.act_size);
        asm volatile("" : : "r"(snap.data()) : "memory");
    }
    auto end = high_resolution_clock::now();
    double ns = (double)duration_cast<nanoseconds>(end - start).count() / iterations;
    printf("Myelin (%s): %8.2f ns/op\n", 
           (Policy == myelin::endian_policy::native ? "Native" : "Network"), ns);
}

// --- Protobuf Benchmark ---
void bench_protobuf(const vector<RawTelemetry>& data, int iterations) {
    // Uses the generated 'Telemetry' class from protoc
    Telemetry proto_msg; 
    string buffer;
    buffer.reserve(512);
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        const auto& d = data[i % data.size()];
        proto_msg.set_timestamp(d.timestamp);
        proto_msg.set_session_id(d.session_id);
        proto_msg.set_pos_x(d.pos_x);
        proto_msg.set_pos_y(d.pos_y);
        proto_msg.set_pos_z(d.pos_z);
        proto_msg.set_health(d.health);
        proto_msg.set_ammo(d.ammo);
        proto_msg.set_is_active(d.is_active);
        proto_msg.set_username(d.username);
        proto_msg.set_action_log(d.action_log);
        
        proto_msg.SerializeToString(&buffer);
        
        // The "Snap": Accessing the string data
        std::string_view snap(buffer);
        asm volatile("" : : "r"(snap.data()) : "memory");
    }
    auto end = high_resolution_clock::now();
    printf("Protobuf:        %8.2f ns/op\n", (double)duration_cast<nanoseconds>(end - start).count() / iterations);
}

// --- FlatBuffers Benchmark ---
void bench_flatbuffers(const vector<RawTelemetry>& data, int iterations) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        builder.Clear();
        const auto& d = data[i % data.size()];
        
        auto u_name = builder.CreateString(d.username);
        auto a_log = builder.CreateString(d.action_log);
        
        // Uses the generated MyBench namespace from flatc
        auto pkt = MyBench::CreateTelemetry(builder, d.timestamp, d.session_id, 
                                            d.pos_x, d.pos_y, d.pos_z, 
                                            d.health, d.ammo, d.is_active, 
                                            u_name, a_log);
        builder.Finish(pkt);
        
        // Hard-coded Snap: Forcing the resolution of the backwards-built buffer
        std::span<uint8_t> snap(builder.GetBufferPointer(), builder.GetSize());
        asm volatile("" : : "r"(snap.data()) : "memory");
    }
    auto end = high_resolution_clock::now();
    printf("FlatBuffers:     %8.2f ns/op\n", (double)duration_cast<nanoseconds>(end - start).count() / iterations);
}

int main() {
    const int ITERS = 1000000;
    auto test_data = generate_heavy_data(1000);

    cout << "Heavy 10-Field Benchmark (" << ITERS << " ops) | Ryzen 3900X" << endl;
    cout << "------------------------------------------------------------" << endl;

    bench_myelin<myelin::endian_policy::native>(test_data, ITERS);
    bench_myelin<myelin::endian_policy::network>(test_data, ITERS);
    bench_protobuf(test_data, ITERS);
    bench_flatbuffers(test_data, ITERS);

    return 0;
}