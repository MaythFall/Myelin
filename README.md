# Myelin
### Sub-12ns serialization for the Axon ecosystem.

**Myelin** is a high-velocity, zero-copy C++20 serialization engine. It wasn't built to be "feature-rich"; it was built to be fast enough that the serializer effectively disappears from your performance profile. By leveraging Single-Pass Bucket Packing and compile-time reflection via `boost::pfr`, Myelin achieves mechanical sympathy with modern x86_64 pipelines.

---

## Benchmarks
Benchmarks performed on an AMD Ryzen 9 3900X (Zen 2 architecture @ 4.6GHz).  
|Operation | Latency | Throughput | Notes |
| --- | --- | --- | --- |
|Mem Serialize| $11.2ns$ | $89.3$ $Mops/s$| Linear $O(N)$ walk| 
| Field Access | $0.65ns$| $\infty$ | Direct pointer arithmetic |
| Mmap Write | $17.4ns$ | $57.6$ $Mops/s$ | Kernel page-cache bound | 

### Performance vs. Other C++ Serializers

![Myelin vs. Protobuf and FlatBuffers](./comparison/Myelin%20Performance%20Vs.%20Protobuf%20and%20FlatBuffers.svg)

*Benchmark: 3-field struct (u64, double, string) | Zen 2 @ 4.6GHz | N=1,000,000*

| Engine | Latency (AVG) | Throughput (AVG) | Efficiency |
| --- | --- | --- | --- |
| **Myelin** | **6.44 $ns$** | **155.23 $Mops/s$** | **$1.0x$ (Baseline)** |
| Protobuf | 25.86 $ns$ | 38.67 $Mops/s$ | $~4.0x$ |
| FlatBuffers | 64.69 $ns$ | 15.46 $Mops/s$ | $~10.0x$ |

##### Full Run Series

*~11,000,000 total iterations across 11 stress tests*

| Run # | Myelin (ns/op) | Protobuf (ns/op) | FlatBuffers (ns/op) |
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

## Why it's fast
Most serializers treat data like a tree. Myelin treats it like a **memory bus**.

* **Mechanical Sympathy**: The engine avoids O(N^2) field searching by pre-calculating alignment "buckets" during a single metadata pass.
* **Register Pinning**: The core loop is designed to let the compiler pin destination pointers to registers (like `%r14` and `%r15`), eliminating "stack trip" latency.
* **Branchless Avalanche**: By resolving type-tags and alignment gaps at compile-time, the CPU's branch predictor sees a flat, predictable stream of `mov` instructions.
* **Zero-Copy**: Calling `get_field<N>()` doesn't "parse" anything. It performs a single pointer addition and returns the data exactly where it sits.

---
 
 ### Usage
Myelin is header-only. Define your schema as a standard C++ `struct` and let the compiler do the work. No IDL, no macros, no boilerplate.


### Basic Serialization
```cpp
#include "myelin.hpp"

struct Packet {
    uint64_t id;
    double val;
    std::string payload;
};

void run() {
    myelin::mem_view<Packet> view;
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

> ### The "ASan Tax"  
> Note: Running with **AddressSanitizer** will result in a *~13x* slowdown ($144ns$ vs $11ns$). This is expected due to shadow memory overhead. For production-grade telemetry, always profile on raw silicon.

## License
**MIT**. Do whatever you want with it, just don't blame me if your code can't keep up.  
Copyright (c) 2026 Adam Brazda.