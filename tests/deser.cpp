#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>
#include "myelin_src/myelin.hpp"

// --- TEST STRUCTURES ---
struct InnerTrivial { 
    float x, y; 
    bool operator==(const InnerTrivial& other) const = default;
};

struct ComplexInner {
    uint64_t id;
    std::string label;
    bool operator==(const ComplexInner& other) const = default;
};

struct MegaPacket {
    uint32_t version;               // Scalar
    InnerTrivial point;             // Trivial Nest
    ComplexInner info;              // True Nest (Recursive)
    std::vector<int32_t> numbers;   // Dynamic Range
    double chance;                  // Scalar after dynamic
};

// --- HELPERS ---
template<typename T>
void verify(const std::string& field, const T& expected, const T& actual) {
    bool match = (expected == actual);
    std::cout << "  " << std::left << std::setw(15) << field << " | ";
    
    // Print logic for the 'Actual' side
    if constexpr (requires { actual.size(); } && !std::is_same_v<T, std::string>) {
        std::cout << "Size " << actual.size();
    } else {
        std::cout << "Val: " << actual;
    }
    
    if (match) {
        std::cout << " \033[1;32m[MATCH]\033[0m" << std::endl;
    } else {
        std::cout << " \033[1;31m[MISMATCH]\033[0m (Expected: ";
        // Special handling for printing the 'Expected' value if it's a vector
        if constexpr (requires { expected.begin(); } && !std::is_same_v<T, std::string>) {
            std::cout << "{ ";
            for(auto it = expected.begin(); it != expected.end(); ++it) {
                std::cout << *it << (std::next(it) != expected.end() ? ", " : "");
            }
            std::cout << " }";
        } else {
            std::cout << expected;
        }
        std::cout << ")" << std::endl;
    }
}

int main() {
    // 1. Initialize Source Data
    MegaPacket original {
        .version = 42,
        .point = { 1.5f, 2.5f },
        .info = { 0xABCDEF, "MYELIN_STRESS_TEST" },
        .numbers = { 100, 200, 300, 400 },
        .chance = 0.99
    };

    myelin::mem_view<MegaPacket> view;
    
    try {
        // 2. Serialize
        view.serialize(original);
        std::cout << "--- Myelin Layout Diagnostics ---" << std::endl;
        std::cout << "Total Body Size: " << view.body_size() << " bytes" << std::endl;

        // Print header map so we can spot the offsets
        for(size_t i = 0; i < 5; ++i) {
            uint32_t off;
            std::memcpy(&off, view.h_data() + (i * 5) + 1, 4);
            std::cout << "Field " << i << " Offset: " << off << std::endl;
        }

        // 3. Deserialize
        MegaPacket decoded;
        view.deserialize(decoded);

        // 4. Detailed Comparison
        std::cout << "\n--- Integrity Check ---" << std::endl;
        
        verify("Version", original.version, decoded.version);
        
        // Manual check for structs since we didn't write ostream operators
        bool p_match = (original.point == decoded.point);
        std::cout << "  InnerTrivial  | " << (p_match ? "\033[1;32m[MATCH]\033[0m" : "\033[1;31m[MISMATCH]\033[0m") << std::endl;

        verify("Info ID", original.info.id, decoded.info.id);
        verify("Info Label", original.info.label, decoded.info.label);
        verify("Vector", original.numbers, decoded.numbers);

        // Check vector elements specifically if size matches
        if (original.numbers.size() == decoded.numbers.size()) {
            for(size_t i = 0; i < decoded.numbers.size(); ++i) {
                std::string label = "  Vec[" + std::to_string(i) + "]";
                verify(label, original.numbers[i], decoded.numbers[i]);
            }
        }

        verify("Chance", original.chance, decoded.chance);

    } catch (const std::exception& e) {
        std::cerr << "CRITICAL FAILURE: " << e.what() << std::endl;
    }

    return 0;
}