# Myelin
### Sub-12ns(Simple) | Sub-25ns(Complex) serialization for the Axon ecosystem.

**Myelin** is a high-velocity, zero-copy C++23 serialization engine. It wasn't built to be "feature-rich"; it was built to be fast enough that the serializer effectively disappears from your performance profile. By leveraging single-pass packing and compile-time reflection via `boost::pfr`, Myelin achieves mechanical sympathy with modern x86_64 pipelines.

---

### Official Benchmarks
Benchmarks performed on an AMD Ryzen 9 3900X (Zen 2 architecture @ 4.6GHz).  
All tests conducted and averaged over 5,000,000 iterations and `-O3` optimization.

<table style="width:100%">
<tr>
<th width="50%">Mem View (Pure RAM)</th>
<th width="50%">Map View (Disk-Backed Mmap)</th>
</tr>
<tr>
<td valign="top">

| Operation | Latency | Throughput |
| :--- | :--- | :--- |
| **Scalar Ser** | 5.31 ns | 188.3 Mops/s |
| **Complex Ser** | 9.08 ns | 110.1 Mops/s |
| **Messy Ser** | 33.10 ns | 30.2 Mops/s |
| **Mem Access** | 0.62 ns | 1612.9 Mops/s |
| **Map Lookup** | 4.18 ns | 239.2 Mops/s |
| **Set Lookup** | 2.83 ns | 353.4 Mops/s |
| **JSON Export** | 630.41 ns | 1.59 Mops/s |

</td>
<td valign="top">

| Operation | Latency | Throughput |
| :--- | :--- | :--- |
| **Scalar Ser** | 13.10 ns | 76.3 Mops/s |
| **Complex Ser** | 22.80 ns | 43.8 Mops/s |
| **Messy Ser** | 43.50 ns | 22.9 Mops/s |
| **Mmap Access** | 0.59 ns | 1694.9 Mops/s |
| **Mmap Map Lookup** | 4.10 ns | 243.9 Mops/s |
| **Mmap Set Lookup** | 2.75 ns | 363.6 Mops/s |
| **JSON Export** | 599.81 ns | 1.67 Mops/s |

</td>
</tr>
</table>

<details>
<summary><b>Speed Records</b></summary>

- Scalar Serialize: 4.95ns
- Complex Serialize: 11.96ns
- Messy Serialize: 32.22ns
- Mem Access: 0.51ns
- Map Lookup: 4.07ns
- Set Lookup: 2.72ns
- JSON Map: 565.21ns

</details>

### Performance vs. Other C++ Serializers

### 3-Field Struct

<img src="./resources/Myelin%20Performance%20Vs.%20Protobuf%20and%20FlatBuffers.svg" width="725" alt="Myelin vs. Protobuf and FlatBuffers">  

*Benchmark: 3-field struct (u64, double, string) | Zen 2 @ 4.6GHz | N=1,000,000*

| Engine | Latency (AVG) | Throughput (AVG) | Efficiency |
| --- | --- | --- | --- |
| **Myelin** | **6.44 ns** | **155.23 Mops/s** | **1.0x (Baseline)** |
| Protobuf | 25.86 ns | 38.67 Mops/s | ~4.0x |
| FlatBuffers | 64.69 ns | 15.46 Mops/s | ~10.0x |

##### Full Run Series

*~11,000,000 total iterations across 11 stress tests*

| Run # | Myelin | Protobuf | FlatBuffers |
| :--- | :--- | :--- | :--- |
| 1 | 6.30 | 26.68 | **62.99** |
| 2 | 6.30 | 26.23 | 64.87 |
| 3 | 6.25 | 25.82 | 64.07 |
| 4 | 6.20 | 25.13 | 63.29 |
| 5 | 6.23 | 26.23 | 66.05 |
| 6 | 6.47 | 26.01 | 63.70 |
| 7 | 6.18 | 26.05 | 63.44 |
| 8 | 7.75 | 27.58 | 68.45 |
| 9 | **6.15** | **24.77** | 64.26 |
| 10 | 6.72 | 24.97 | 65.28 |
| 11 | 6.25 | 25.78 | 63.46 |

<small>Units are ns/op</small>

### 10-Field Struct

<img src="./resources/Myelin%20Performance%20Vs.%20Protobuf%20and%20FlatBuffers%20(1).svg" width="725" alt="Myelin vs. Protobuf and FlatBuffers"> 

*Benchmark: 10-field struct (Scalars, 3x Double, 2x Variable Strings) | Zen 2 @ 4.6GHz | N=1,000,000*

| Engine | Latency (AVG) | Throughput (AVG) | Efficiency |
| --- | --- | --- | --- |
| **Myelin** (Native) | **22.80 ns** | **43.86 Mops/s** | **1.0x (Baseline)** |
| **Myelin** (Net) | **22.82 ns** | **43.82 Mops/s** | **1.0x (Baseline)** |
| Protobuf | 80.79 ns | 12.38 Mops/s | ~3.5x |
| FlatBuffers | 163.67 ns | 6.11 Mops/s | ~7.2x |

##### Full Run Series

*~6,000,000 total iterations across 11 stress tests*

| Run # | Myelin (Native) | Myelin (Net) | Protobuf | FlatBuffers |
| :--- | :--- | :--- | :--- | :--- |
| 1 | 23.70  | 25.25  | 84.19  | 168.31  |
| 2 | 22.97  | 23.10  | 77.01  | 159.95  |
| 3 | 22.32  | 23.44  | 77.98  | 161.89  |
| 4 | 22.47  | 23.60  | 79.09  | 164.59  |
| 5 | 22.51  | 24.32  | 85.73  | 167.34  |
| 6 | 22.85  | 23.23  | 80.75  | 159.91  |
<small>Units are ns/op</small>
## Why it's fast
Most serializers treat data like a tree. Myelin treats it like a **memory bus**.

* **Mechanical Sympathy**: The engine avoids O(N^2) field searching by pre-calculating alignment "buckets" during a single metadata pass.
* **Register Pinning**: The core loop is designed to let the compiler pin destination pointers to registers (like `%r14` and `%r15`), eliminating "stack trip" latency.
* **Branchless Avalanche**: By resolving type-tags and alignment gaps at compile-time, the CPU's branch predictor sees a flat, predictable stream of `mov` instructions.
* **Zero-Copy**: Calling `get_field<N>()` doesn't "parse" anything. It performs a single pointer addition and returns the data exactly where it sits.

### Internal Architecture: The Flattening
Standard C++ containers like `std::list` or `std::map` are "node-based," meaning their data is scattered across the heap. This is a cache-killer. 

Myelin solves this by **flattening** these structures into a contiguous **SoA (Structure of Arrays)** layout during serialization:

* **Lists/Deques**: Flattened into a single linear vector.
* **Maps/Sets**: Flattened into a dual-array layout (Keys are packed together, then Values are packed together).

This allows the CPU prefetcher to stream data into the L1 cache with zero pointer-chasing, achieving **~3.9ns** lookup times even for associative data.

---
 
## Installation
Myelin is header-only. Simply copy `myelin.hpp` into your project.
* **Dependencies:** C++23 (or higher)
* `boost/pfr.hpp` (Minimum 1.2.0)
  
> [!IMPORTANT]
> ### General Notes of Use
> #### Endianness
> When using `endian_policy::network`, Myelin ensures cross-platform compatibility by storing all scalars and blob-elements (e.g., `uint32_t` inside a `std::vector`) in **Big Endian** format.
>
> * **API Access**: Calling `get_field<N>()` handles the reverse-swap automatically for scalars. However, accessing a `std::vector` or other blob types requires manual reversal as the returned `std::span` points directly to the stored memory.
> * **Manual Access**: If you access the underlying `body_ptr` or `mmap` region directly, you must manually reverse the endianness of the elements.
> * **Strings/Bytes**: Standard `std::string` or `std::vector<uint8_t>` are stored as raw byte-streams and are unaffected by endianness policies.
>
> #### Sets and Maps
> When using a structure that contains a set or a map it is important to use the `setify()` or `mapify()` functions before attempting to access data in order to receive the expected results.

<br/>

> [!WARNING]
> ### Windows Support
> The Win32 MemoryMapper implementation is architecturally complete but has not yet undergone the same 10M+ iteration stress-testing as the Linux branch.
> #### Critical Testing Required for:
> * **Handle Management**: Ensure your environment handles Windows-specific mandatory file locking.
> * **64KB Granularity**: Verify offset-heavy mappings on your target hardware to prevent alignment faults.
> * **Performance Jitter**: Profile on raw Windows silicon; MapViewOfFile latency may vary from POSIX mmap under heavy I/O contention.

### Basic Serialization
```cpp
#include "myelin.hpp"

struct Packet {
    uint64_t id;
    double val;
    std::string payload;
};

void run() {
    //Endian policy is optional and default to Native(Little Endian)
    //Alternative is endian_policy::network (Big Endian)
    myelin::mem_view<Packet, myelin::endian_policy::native> view;
    Packet p{42, 3.14, "Velocity"};

    // Serialize to RAM in ~11ns
    view.serialize(p);

    // Access field 1 (double) instantly
    double v = view.get_field<1>();
}
```

### Loading & Mapping

```cpp
void load_data() {
    myelin::mem_view<Packet> view;
    myelin::map_view<Packet> map_view;

    // Load from disk or map to memory
    view.parse("file.path");
    map_view.map("file.path");

    // Access fields instantly
    double v = view.get_field<1>();
    double map_v = map_view.get_field<1>();
}
```

### Persistence with Metadata

```cpp
void save_with_notes() {
    myelin::mem_view<Packet> view;
    myelin::map_view<Packet> map_view;
    Packet p{42, 3.14, "Velocity"};

    view.serialize(p);
    view.pack_to_disk("file.path", "Optional Metadata/Note");

    map_view.map("file.path");
    
    // Retrieve attached note
    std::string note = map_view.get_note();
    view.parse("file.path", &note);
}
```

### Dump to Json

```cpp
void dump_to_json() {
    myelin::mem_view<Packet> view;
    Packet p{42, 3.14, "Velocity"};
    view.serialize(p);

    // Default: string contains { "0" : value, "1" : value ... }
    std::string json_idx = view.to_json(); 

    // With Names: string contains { "id" : 42, "val" : 3.14 ... }
    std::array<std::string_view, 3> field_names = {"id", "val", "payload"};
    std::string json_named = view.to_json(field_names); 
}
```

### Handling Associative Containers

```cpp
struct SocialPacket {
    uint32_t user_id;
    std::map<uint32_t, float> reputation_scores;
};

void process() {
    myelin::mem_view<SocialPacket> view;
    // ... serialization happens here ...

    // Get the raw span from the buffer
    auto raw_scores = view.get_field<1>(); 

    // Wrap it in a high-velocity View (No allocations!)
    myelin::std_map_view<uint32_t, float> scores_view;
    myelin::mapify(raw_scores, scores_view);

    // O(log N) lookup on flattened data
    if (const float* score = scores_view.find(101)) {
        printf("Score: %f\n", *score);
    }
}
```
> [!Warning]
> ### Unordered Maps with Duplicate Keys
> **Myelin** does not support quick mapify extraction for maps with duplicate keys. To extract them out the overloaded `mapify(data, Output_Map)` must be used. 

> [!NOTE]
> ### The "ASan Tax"  
> Running with **AddressSanitizer** will result in a *~13x* slowdown ($144ns$ vs $11ns$). This is expected due to shadow memory overhead. For production-grade telemetry, always profile on raw silicon.

## License
**Myelin** is dual-licensed to balance open-source growth with intellectual property protection:

1. **Open Source**: Licensed under the **GNU GPL v3**. Anyone is free to use, modify, and share this code, provided that any derivative works or distributions are also made open-source under the same license.
2. **Commercial/Proprietary**: For entities that wish to integrate Myelin into closed-source or proprietary commercial products without the "copyleft" requirements of the GPL, a **separate commercial license** is required.

For commercial licensing inquiries, please contact me through: **[linkedin](https://www.linkedin.com/in/adam-brazda-617976326/)**

Copyright (c) 2026 Adam Brazda.
