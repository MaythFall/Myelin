#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <cstring>
#include "myelin/myelin.hpp"

// --- DATA MODEL ---
namespace MyelinTest {
    // We keep the struct simple to ensure Boost.PFR can reflect it.
    // Myelin works best when the struct itself is an aggregate.
    struct Monster {
        std::vector<float> pos;       // Dynamic pos
        int16_t mana;
        int16_t hp;
        std::string name;             // Dynamic string
        std::vector<uint8_t> inventory; 
        uint8_t color;
        std::vector<float> path;      // Dynamic path
    };
}

void runMyelinSTLTest(size_t monster_count, size_t iterations) {
    // 1. PERSISTENCE LAYER
    // We must ensure this vector does not reallocate during the test,
    // otherwise Myelin's internal pointers will break.
    std::vector<MyelinTest::Monster> source_data;
    source_data.reserve(monster_count); 

    for (uint32_t i = 0; i < monster_count; ++i) {
        MyelinTest::Monster m;
        m.pos = {1.1f, 2.2f, 3.3f};
        m.mana = 50;
        m.hp = 100;
        m.name = "Monster_" + std::to_string(i);
        m.inventory = {1, 2, 3, 4};
        m.color = 1;
        m.path = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
        source_data.push_back(std::move(m));
    }

    // 2. VIEW LAYER (The Handles)
    std::vector<myelin::mem_view<MyelinTest::Monster>> views(monster_count);

    std::cout << "--- Myelin STL Architecture Test ---" << std::endl;
    std::cout << "Using std::string and std::vector..." << std::endl;

    // --- STEP 1: MAPPING (Serialization) ---
    auto start = std::chrono::steady_clock::now();
    for (size_t iter = 0; iter < iterations; ++iter) {
        for (size_t i = 0; i < monster_count; ++i) {
            // Myelin maps the internal pointers of the STL containers
            views[i].serialize(source_data[i]);
        }
    }
    auto end = std::chrono::steady_clock::now();
    auto map_diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // --- STEP 2: ACCESS (Deserialization) ---
    double hp_sum = 0;
    start = std::chrono::steady_clock::now();
    for (size_t iter = 0; iter < iterations; ++iter) {
        for (size_t i = 0; i < monster_count; ++i) {
            // Field 2 is 'hp' (int16_t)
            // Field 3 is 'name' (returns string_view)
            hp_sum += views[i].get_field<2>();
            
            // Touch the string to ensure it's mapped correctly
            if (views[i].get_field<3>().empty()) { 
                /* Should not happen */ 
            }
        }
    }
    end = std::chrono::steady_clock::now();
    auto access_diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "* Mapping Time (Serialize): " << map_diff.count() << " ms" << std::endl;
    std::cout << "* Access Time (Deserialize):  " << access_diff.count() << " ms" << std::endl;
    std::cout << "* Checksum (HP Sum):        " << hp_sum << std::endl;
}

int main() {
    try {
        runMyelinSTLTest(1000, 10000);
    } catch (const std::exception& e) {
        std::cerr << "Caught: " << e.what() << std::endl;
    }
    return 0;
}