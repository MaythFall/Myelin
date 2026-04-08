#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <iomanip>
#include "myelin_src/myelin.hpp" 

// --- 1. Define the Telemetry Hierarchy ---
struct Transform {
    float x;
    float y;
    float z;
};

struct Player {
    uint64_t account_id;
    Transform position;                        
    std::string handle;                        
    std::map<uint32_t, float> damage_log;      
};

struct MatchState {
    uint32_t match_id;
    Player mvp;                                
    std::vector<uint32_t> active_perks;        
    bool is_ranked;
};

// Optimization barrier to prevent -O3 from deleting our loops
volatile double black_hole = 0.0;

int main() {
    std::cout << "====================================================\n";
    std::cout << "[MYELIN ENGINE] MICROBENCHMARK SUITE\n";
    std::cout << "Target: Level 3 Recursive Structs & Zero-Copy Maps\n";
    std::cout << "====================================================\n\n";

    MatchState state{
        .match_id = 998877,
        .mvp = {
            .account_id = 0x7FFFFFFFFFFFFFFF,
            .position = { .x = 105.5f, .y = 22.1f, .z = -8.4f },
            .handle = "Maythfall",
            .damage_log = { {101, 45.5f}, {205, 120.0f}, {308, 12.2f} }
        },
        .active_perks = { 10, 15, 42, 99 },
        .is_ranked = true
    };

    myelin::mem_view<MatchState> view;
    view.serialize(state);
    
    const int SERIALIZE_ITERS = 1'000'000;
    const int READ_ITERS = 10'000'000;

    // ---------------------------------------------------------
    // TEST 1: BULK SERIALIZATION (Write Throughput)
    // ---------------------------------------------------------
    std::cout << "[TEST 1] Serializing MatchState (" << SERIALIZE_ITERS << " iterations)...\n";
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < SERIALIZE_ITERS; ++i) {
        // Mutate slightly to defeat perfect branch prediction caching
        state.mvp.position.x += 0.01f; 
        view.serialize(state);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    
    double ns_per_serialize = (diff.count() * 1e9) / SERIALIZE_ITERS;
    double mb_per_sec = (view.act_size * SERIALIZE_ITERS) / (1024.0 * 1024.0) / diff.count();

    std::cout << "  -> Latency:    " << std::fixed << std::setprecision(2) << ns_per_serialize << " ns / write\n";
    std::cout << "  -> Throughput: " << mb_per_sec << " MB/s\n\n";

    // ---------------------------------------------------------
    // TEST 2: DEEP RECURSIVE READ (Level 3 Nesting)
    // ---------------------------------------------------------
    std::cout << "[TEST 2] Deep Read: MatchState -> Player -> Transform.x (" << READ_ITERS << " iterations)...\n";
    start = std::chrono::high_resolution_clock::now();
    
    double sum = 0;
    myelin::nested_view<Player> player_view;
    myelin::nested_view<Transform> transform_view;

    for (int i = 0; i < READ_ITERS; ++i) {
        myelin::structify(view.get_field<1>(), player_view);
        myelin::structify(player_view.get_field<1>(), transform_view);
        sum += transform_view.get_field<0>(); // Read 'x'
    }
    black_hole = sum; // Consume result

    end = std::chrono::high_resolution_clock::now();
    diff = end - start;
    double ns_per_read = (diff.count() * 1e9) / READ_ITERS;

    std::cout << "  -> Latency:    " << std::fixed << std::setprecision(3) << ns_per_read << " ns / read\n\n";

    // ---------------------------------------------------------
    // TEST 3: ZERO-COPY MAP BINARY SEARCH
    // ---------------------------------------------------------
    std::cout << "[TEST 3] Zero-Copy Map: O(log N) Binary Search (" << READ_ITERS << " iterations)...\n";
    start = std::chrono::high_resolution_clock::now();
    
    sum = 0;
    myelin::std_map_view<uint32_t, float> damage_view;

    for (int i = 0; i < READ_ITERS; ++i) {
        myelin::structify(view.get_field<1>(), player_view);
        myelin::mapify(player_view.get_field<3>(), damage_view);
        
        const float* dmg = damage_view.find(205);
        if (dmg) sum += *dmg;
    }
    black_hole = sum;

    end = std::chrono::high_resolution_clock::now();
    diff = end - start;
    double ns_per_map_read = (diff.count() * 1e9) / READ_ITERS;

    std::cout << "  -> Latency:    " << std::fixed << std::setprecision(3) << ns_per_map_read << " ns / lookup\n\n";

    std::cout << "====================================================\n";
    std::cout << " BENCHMARK COMPLETE.\n";
    std::cout << "====================================================\n";

    return 0;
}