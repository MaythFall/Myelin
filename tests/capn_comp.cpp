#include <iostream>
#include <chrono>
#include <vector>
#include <list>
#include <numeric>
#include <iomanip>
#include "myelin/myelin.hpp"
#include "heavy.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize.h>

using namespace std::chrono;

struct NativeHeavy {
    uint64_t id; uint64_t timestamp; bool active;
    double v1, v2, v3;
    std::string tag1, tag2;
    std::list<int32_t> data; 
};

int main() {
    const int iterations = 1000000;
    std::list<int32_t> bulk_data;
    for(int i = 0; i < 100; ++i) bulk_data.push_back(i);

    NativeHeavy p{ 1234567, 1712420000, true, 1.1, 2.2, 3.3, 
                   "Primary-Tag", "Secondary-Tag", bulk_data };

    // --- 1. MYELIN (LIST FLATTENING) ---
    myelin::mem_view<NativeHeavy> m_view;
    
    // Serialization (Now involves node-traversal and flattening)
    auto m_s_start = high_resolution_clock::now();
    for(int i = 0; i < iterations; ++i) m_view.serialize(p);
    auto m_s_end = high_resolution_clock::now();

    // Access (Reading from the flattened buffer)
    volatile int32_t m_vec_val;
    auto m_a_start = high_resolution_clock::now();
    for(int i = 0; i < iterations; ++i) {
        auto d = m_view.get_field<8>(); // Returns a contiguous span
        m_vec_val = d[50];
    }
    auto m_a_end = high_resolution_clock::now();

    // --- 2. CAP'N PROTO (MANAGED LIST) ---
    auto c_s_start = high_resolution_clock::now();
    kj::Array<capnp::word> flat_array;
    for(int i = 0; i < iterations; ++i) {
        ::capnp::MallocMessageBuilder message;
        auto packet = message.initRoot<HeavyPacket>();
        packet.setId(p.id);
        packet.setTag1(p.tag1);
        
        auto data_list = packet.initData(p.data.size());
        int j = 0;
        for (auto val : p.data) { data_list.set(j++, val); }
        
        flat_array = messageToFlatArray(message);
    }
    auto c_s_end = high_resolution_clock::now();

    // Access
    volatile int32_t c_vec_val;
    auto c_a_start = high_resolution_clock::now();
    for(int i = 0; i < iterations; ++i) {
        ::capnp::FlatArrayMessageReader reader(flat_array);
        auto packet = reader.getRoot<HeavyPacket>();
        auto d = packet.getData();
        c_vec_val = d[50];
    }
    auto c_a_end = high_resolution_clock::now();

    // Results
    auto m_ser = duration_cast<nanoseconds>(m_s_end - m_s_start).count() / (double)iterations;
    auto m_acc = duration_cast<nanoseconds>(m_a_end - m_a_start).count() / (double)iterations;
    auto c_ser = duration_cast<nanoseconds>(c_s_end - c_s_start).count() / (double)iterations;
    auto c_acc = duration_cast<nanoseconds>(c_a_end - c_a_start).count() / (double)iterations;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "--- List Flattening Shootout (Ryzen 3900X) ---\n";
    std::cout << "| Action         | Myelin (Flatten) | Cap'n (Native) | Gap      |\n";
    std::cout << "|----------------|------------------|----------------|----------|\n";
    std::cout << "| Serialization  | " << m_ser << " ns         | " << c_ser << " ns      | " << (c_ser/m_ser) << "x |\n";
    std::cout << "| Access (Read)  | " << m_acc << " ns         | " << c_acc << " ns      | " << (c_acc/m_acc) << "x |\n";

    return 0;
}