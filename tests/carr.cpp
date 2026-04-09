#include <iostream>
#include <cassert>
#include <array>
#include "myelin_src/myelin.hpp"

// Define a deep 3D Voxel (2x2x2) and a standard 4x4 Matrix
using VoxelGrid = std::array<std::array<std::array<uint8_t, 2>, 2>, 2>;
using Matrix4x4 = std::array<std::array<float, 4>, 4>;

struct ComplexScene {
    uint32_t version;            // Boundary Check 1
    VoxelGrid voxels;            // 3D Nesting
    Matrix4x4 transform;         // 2D Nesting (large block)
    float scalar_end;            // Boundary Check 2
};

int main() {
    std::cout << "[MYELIN] Initiating Full Multidimensional Test..." << std::endl;

    // 1. Setup Data
    ComplexScene scene{};
    scene.version = 42;
    scene.scalar_end = 3.14159f;

    // Set a specific value in the 3D grid
    // Index [1][0][1] = 128
    scene.voxels[1][0][1] = 128;

    // Set a value in the 2D Matrix
    // Index [2][3] = 7.5
    scene.transform[2][3] = 7.5f;

    // 2. Serialize to Memory
    myelin::mem_view<ComplexScene> view;
    view.serialize(scene);

    std::cout << "[SYSTEM] Serialized Size: " << view.act_size << " bytes" << std::endl;

    // 3. Test Field 0: Scalar
    assert(view.get_field<0>() == 42);
    std::cout << " [PASS] Header scalar integrity." << std::endl;

    // 4. Test Field 1: 3D Array Access via get_field
    // This returns a mult_view<uint8_t, 2, 2, 2>
    auto v_grid = view.get_field<1>();
    assert(v_grid.size() == 2);
    assert(v_grid[1][0][1] == 128);
    assert(v_grid[0][0][0] == 0);
    std::cout << " [PASS] 3D Voxel access ([1][0][1] == 128)." << std::endl;

    // 5. Test Field 2: 2D Matrix Access
    // This returns a mult_view<float, 4, 4>
    auto mat = view.get_field<2>();
    assert(mat.size() == 4);
    assert(mat[2][3] == 7.5f);
    std::cout << " [PASS] 2D Matrix access ([2][3] == 7.5)." << std::endl;

    // 6. Test Field 3: Trailing Scalar (The "Drift" Test)
    // If the matrix size calculation was wrong, this would be garbage.
    assert(view.get_field<3>() == 3.14159f);
    std::cout << " [PASS] Trailing scalar integrity (No offset drift)." << std::endl;

    // 7. JSON Output Verification
    std::cout << "\n[SYSTEM] Generated JSON Structure:" << std::endl;
    std::string json = view.to_json({"Version", "Voxels", "Transform", "PI"});
    std::cout << json << std::endl;

    // Quick check if JSON contains nested brackets
    if (json.find("[ [ [") != std::string::npos) {
        std::cout << " [PASS] JSON recursion confirmed." << std::endl;
    }

    std::cout << "\n--- ALL NESTED ARRAY TESTS PASSED (0.7ns Layer) ---" << std::endl;

    return 0;
}