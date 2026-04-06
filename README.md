# Myelin
### Sub-12ns(Simple)/Sub-25ns(Complex) serialization for the Axon ecosystem.

**Myelin** is a high-velocity, zero-copy C++23 serialization engine. It wasn't built to be "feature-rich"; it was built to be fast enough that the serializer effectively disappears from your performance profile. By leveraging single-pass packing and compile-time reflection via `boost::pfr`, Myelin achieves mechanical sympathy with modern x86_64 pipelines.

---

### Official Benchmarks
Benchmarks performed on an AMD Ryzen 9 3900X (Zen 2 architecture @ 4.6GHz).  
All tests conducted and averaged over 5,000,000 iterations and `-O3` optimization.

| Operation | Mode | Latency | Throughput | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Scalar Serialize** | `mem_view` | 5.21 $ns$ | 191.94 $Mops/s$ | CPU L1 Cache bound |
| **Complex Serialize** | `mem_view` | 14.56 $ns$ | 68.68 $Mops/s$ | Incl. strings & vectors |
| **Field Access** | `mem_view` | 0.65 $ns$ | 1,538.46 $Mops/s$ | Direct L1 cache hit |
| **JSON Export** | `mem_view` | 630.41 $ns$ | 1.59 $Mops/s$ | `to_chars` optimized |
| **Scalar Serialize** | `map_view` | 12.04 $ns$ | 83.06 $Mops/s$ | Persistent (Page Cache) |
| **Complex Serialize** | `map_view` | 22.02 $ns$ | 45.41 $Mops/s$ | Persistent (Page Cache) |
| **Field Access** | `map_view` | 0.53 $ns$ | 1,886.79 $Mops/s$ | Direct pointer arithmetic |
| **JSON Export** | `map_view` | 599.81 $ns$ | 1.67 $Mops/s$ | Zero-copy persistence |

### Performance vs. Other C++ Serializers

### 3-Field Struct

<img src="./resources/Myelin%20Performance%20Vs.%20Protobuf%20and%20FlatBuffers.svg" width="725" alt="Myelin vs. Protobuf and FlatBuffers">  

*Benchmark: 3-field struct (u64, double, string) | Zen 2 @ 4.6GHz | N=1,000,000*

| Engine | Latency (AVG) | Throughput (AVG) | Efficiency |
| --- | --- | --- | --- |
| **Myelin** | **6.44 $ns$** | **155.23 $Mops/s$** | **$1.0x$ (Baseline)** |
| Protobuf | 25.86 $ns$ | 38.67 $Mops/s$ | $~4.0x$ |
| FlatBuffers | 64.69 $ns$ | 15.46 $Mops/s$ | $~10.0x$ |

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
| **Myelin** (Native) | **22.80 $ns$** | **43.86 $Mops/s$** | **$1.0x$ (Baseline)** |
| **Myelin** (Net) | **22.82 $ns$** | **43.82 $Mops/s$** | **$1.0x$ (Baseline)** |
| Protobuf | 80.79 $ns$ | 12.38 $Mops/s$ | $~3.5x$ |
| FlatBuffers | 163.67 $ns$ | 6.11 $Mops/s$ | $~7.2x$ |

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

---
 
## Installation
Myelin is header-only. Simply copy `myelin.hpp` into your project.
* **Dependencies:** C++23 (or higher)
* `boost/pfr.hpp` (Minimum 1.2.0)
  
> [!IMPORTANT]
> ### Manual Access & Endianness
> When using `endian_policy::network`, Myelin ensures cross-platform compatibility by storing all scalars and blob-elements (e.g., `uint32_t` inside a `std::vector`) in **Big Endian** format.
>
> * **API Access**: Calling `get_field<N>()` handles the reverse-swap automatically for scalars. However, accessing a `std::vector` or other blob types requires manual reversal as the returned `std::span` points directly to the stored memory.
> * **Manual Access**: If you access the underlying `body_ptr` or `mmap` region directly, you must manually reverse the endianness of the elements.
> * **Strings/Bytes**: Standard `std::string` or `std::vector<uint8_t>` are stored as raw byte-streams and are unaffected by endianness policies.


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
    view.parse("file.path", note);
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

> [!NOTE]
> ### The "ASan Tax"  
> Note: Running with **AddressSanitizer** will result in a *~13x* slowdown ($144ns$ vs $11ns$). This is expected due to shadow memory overhead. For production-grade telemetry, always profile on raw silicon.

## License
**Myelin** is dual-licensed to balance open-source growth with intellectual property protection:

1. **Open Source**: Licensed under the **GNU GPL v3**. Anyone is free to use, modify, and share this code, provided that any derivative works or distributions are also made open-source under the same license.
2. **Commercial/Proprietary**: For entities that wish to integrate Myelin into closed-source or proprietary commercial products without the "copyleft" requirements of the GPL, a **separate commercial license** is required.

For commercial licensing inquiries, please contact me through: **[linkedin](https://www.linkedin.com/in/adam-brazda-617976326/)**

Copyright (c) 2026 Adam Brazda.
