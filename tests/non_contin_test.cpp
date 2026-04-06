#include <iostream>
#include <vector>
#include <map>
#include <set>     // Added Set support
#include <list>
#include <deque>
#include <cassert>
#include "myelin.hpp" 

// 1. Define the struct for Myelin's Reflection (Boost.PFR)
struct DataPacket {
    std::map<uint32_t, float> sensors; // Field 0: SoA Blob
    std::list<int32_t> logs;           // Field 1: Flat Span
    std::deque<double> signals;        // Field 2: Flat Span
    std::set<uint32_t> codes;          // Field 3: Set Blob (New)
};

void test_log(const std::string& name, bool success) {
    std::cout << "[ " << (success ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m") 
              << " ] " << name << std::endl;
}

int main() {
    using namespace myelin;
    std::cout << "--- Myelin: Non-Contiguous Struct (Set Included) Round-Trip ---\n" << std::endl;

    // --- 1. Populate Source Data ---
    DataPacket packet;
    packet.sensors = {{101, 1.1f}, {202, 2.2f}, {303, 3.3f}};
    packet.logs    = {10, 20, 30, 40, 50};
    packet.signals = {0.123, 0.456};
    packet.codes   = {1001, 1002, 1003}; // New Set data

    // --- 2. Serialize ---
    mem_view<DataPacket> view;
    view.serialize(packet);
    
    test_log("Serialization: Struct Pack Success", view.act_size > 0);

    // --- 3. Extract Continuous Spans (List/Deque) ---
    auto logs_span = view.get_field<1>(); 
    test_log("Extraction: List -> Contiguous Span", logs_span.size() == 5 && logs_span[2] == 30);

    auto signals_span = view.get_field<2>();
    test_log("Extraction: Deque -> Contiguous Span", signals_span.size() == 2 && signals_span[1] == 0.456);

    // --- 4. The "Mapify" Manual Wrap ---
    auto raw_map_blob = view.get_field<0>(); 
    std_map_view<uint32_t, float, true> sensor_view;
    mapify(raw_map_blob, sensor_view);

    const float* val = sensor_view.find(202);
    test_log("Mapify Wrap: Zero-Copy Map Lookup", val && *val == 2.2f);

    // --- 5. The "Setify" Manual Wrap (New) ---
    auto raw_set_blob = view.get_field<3>(); // Field 3 is our Set
    std_set_view<uint32_t, true> code_view;
    setify(raw_set_blob, code_view);

    bool set_ok = code_view.contains(1002);
    bool set_miss = !code_view.contains(999);
    test_log("Setify Wrap: Zero-Copy Set Lookup", set_ok && set_miss);

    // --- 6. Final Integrity Check ---
    test_log("Mapify Wrap: Correct nullptr on Miss", sensor_view.find(999) == nullptr);

    std::cout << "\n--- Myelin Status: Systems Nominal ---" << std::endl;
    return 0;
}