#pragma once
#include "utils.hpp"

namespace myelin {

    template <typename Derived, endian_policy Policy>
    struct recur_view;

    template <typename Derived, typename Impl, endian_policy Policy = endian_policy::native>
    struct basic_view {
        static constexpr uint32_t num_fields = boost::pfr::tuple_size_v<Derived>;
        static constexpr size_t header_size = num_fields * 5;
        static constexpr size_t num_nested = myelin::num_nests_v<Derived>;

        uint8_t* header_ptr = nullptr;
        uint8_t* body_ptr = nullptr;
        size_t   act_size = 0;


        std::array<myelin::recur_data, num_nested> nested_metadata;
        size_t nest_cursor = 0;

        inline size_t total_size() const { return header_size + act_size; }
        inline size_t body_size() const { return act_size; }
        inline uint8_t* h_data() { return header_ptr; }
        inline uint8_t* b_data() { return body_ptr; }

        inline uint8_t* h_tail() { return header_ptr + header_size; }
        inline uint8_t* b_tail() { return body_ptr + act_size; }

        inline size_t resize_calc(const Derived& obj, uint16_t* counts) {
            nest_cursor = 0;
            size_t req = 0;
            boost::pfr::for_each_field(obj, [&](const auto& field) {
                using T = std::decay_t<decltype(field)>;
                if constexpr (is_continuous_v<T>) {
                    ++counts[4];
                    if constexpr (is_flat_array<T>{}) req += (sizeof(T)) + 11;
                    else req += (std::ranges::size(field) * sizeof(std::ranges::range_value_t<T>)) + 16; 
                } else if constexpr (is_struct_v<T>) {
                    ++counts[4];
                    if constexpr (std::is_trivially_copyable_v<T>) {
                        req += sizeof(T) + 16;
                    } else {
                        this->nested_metadata[nest_cursor] = (myelin::recur_data());
                        req += recur_view<T, Policy>::size(field, this->nested_metadata[nest_cursor]) + 32; //safety buffer
                        ++nest_cursor;
                    }
                } else if constexpr (is_noncontinuous_range_v<T>) {
                    ++counts[4];
                    if constexpr (is_map_type_v<T>) {
                        using Pair = std::ranges::range_value_t<T>;
                        using K = std::remove_const_t<std::tuple_element_t<0, Pair>>;
                        using V = std::tuple_element_t<1, Pair>;
                        req += (std::ranges::size(field) * (sizeof(K) + sizeof(V))) + 16;
                    } else {
                        req += (std::ranges::size(field) * sizeof(std::ranges::range_value_t<T>)) + 16;
                    }
                } else {
                    constexpr size_t a = alignof(T);
                    if      constexpr (a >= 8) { ++counts[0]; req += 8; }
                    else if constexpr (a == 4) { ++counts[1]; req += 4; }
                    else if constexpr (a == 2) { ++counts[2]; req += 2; }
                    else                       { ++counts[3]; req += 1; }
                }
            });
            return req;
        }

        inline size_t pack_data_bucketed(const Derived& obj, uint16_t* counts) {
            uint32_t cursors[4] = {0};
            cursors[1] = ((uint32_t)counts[0] << 3);
            cursors[2] = cursors[1] + ((uint32_t)counts[1] << 2);
            cursors[3] = cursors[2] + ((uint32_t)counts[2] << 1);

            size_t blob_cursor = cursors[3] + counts[3];
            nest_cursor = 0;
            boost::pfr::for_each_field(obj, [&](const auto& field, size_t idx) {
                using T = std::decay_t<decltype(field)>;
                this->header_ptr[idx * 5] = (uint8_t)get_type_tag<T>();

                if constexpr (is_struct_v<T>) {
                    blob_cursor = align_to(blob_cursor, 4);
                    uint32_t write_off = (uint32_t)blob_cursor;
                    
                    uint32_t d_sz;
                    if constexpr (!std::is_trivially_copyable_v<T>) {
                        d_sz = (uint32_t)(this->nested_metadata[nest_cursor].header_size + this->nested_metadata[nest_cursor].body_size);
                    } else {
                        d_sz = (uint32_t)sizeof(T);
                    }

                    uint32_t le_off = apply_policy<Policy>(write_off);
                    std::memcpy(&this->header_ptr[idx * 5 + 1], &le_off, 4);
                    
                    uint32_t le_sz = apply_policy<Policy>(d_sz);
                    std::memcpy(this->body_ptr + write_off, &le_sz, 4);
                    
                    size_t d_start = write_off + 4;
                    
                    if constexpr (!std::is_trivially_copyable_v<T>) {
                        recur_view<T, Policy>().serialize(field, this->nested_metadata[nest_cursor++], this->body_ptr + d_start);
                    } else {
                        d_start = align_to(d_start, alignof(T)); // FIX: Use alignof(T)
                        std::memcpy(this->body_ptr + d_start, &field, sizeof(field));
                    }
                    blob_cursor = d_start + d_sz;
                } else if constexpr (is_continuous_v<T>) {
                    blob_cursor = align_to(blob_cursor, 4);
                    uint32_t write_off = (uint32_t)blob_cursor;
                    
                    uint32_t d_sz;
                    size_t d_start;

                    if constexpr (is_flat_array<T>{}) {
                        // For multidimensional std::array, use raw sizeof and base type alignment
                        d_sz = (uint32_t)sizeof(T);
                        d_start = align_to(write_off + 4, alignof(get_base_type_t<T>));
                    } else {
                        // For std::vector/std::string, E is the element type (e.g., char)
                        using E = std::ranges::range_value_t<T>;
                        d_sz = (uint32_t)(std::ranges::size(field) * sizeof(E));
                        d_start = align_to(write_off + 4, alignof(E));
                    }

                    uint32_t le_off = apply_policy<Policy>(write_off);
                    std::memcpy(&this->header_ptr[idx * 5 + 1], &le_off, 4);
                    uint32_t le_sz = apply_policy<Policy>(d_sz);
                    std::memcpy(this->body_ptr + write_off, &le_sz, 4);

                    // Use safe_data helper or cast &field for flat arrays
                    const void* src = is_flat_array<T>{} ? (const void*)&field : (const void*)std::ranges::data(field);
                    std::memcpy(this->body_ptr + d_start, src, d_sz);
                    blob_cursor = d_start + d_sz;

                    if constexpr (Policy == endian_policy::network && !is_flat_array<T>{}) {
                        using E = std::ranges::range_value_t<T>;
                        if constexpr (sizeof(E) > 1) {
                            std::span<E> dest_span((E*)(this->body_ptr + d_start), d_sz / sizeof(E));
                            flipEndiannessBlob(dest_span);
                        }
                    }

                } else if constexpr (is_map_type_v<T>) {
                    using Pair = std::ranges::range_value_t<T>;
                    using K = std::remove_const_t<std::tuple_element_t<0, Pair>>;
                    using V = std::tuple_element_t<1, Pair>;
                    blob_cursor = align_to(blob_cursor, 4);
                    uint32_t write_off = (uint32_t)blob_cursor;

                    uint32_t count = (uint32_t)std::ranges::size(field);
                    uint32_t d_sz = 4 + (count * (sizeof(K) + sizeof(V)));

                    uint32_t le_off = apply_policy<Policy>(write_off);
                    std::memcpy(&this->header_ptr[idx * 5 + 1], &le_off, 4);
                    uint32_t le_sz = apply_policy<Policy>(d_sz);
                    std::memcpy(this->body_ptr + write_off, &le_sz, 4);

                    std::span<uint8_t> target(this->body_ptr + write_off + 4, d_sz);
                    flatten_map<T, Policy>(field, target);
                    blob_cursor = write_off + 4 + d_sz; 

                } else if constexpr (is_set_type_v<T>) {
                    using K = std::remove_const_t<typename T::value_type>;
                    blob_cursor = align_to(blob_cursor, 4);
                    uint32_t write_off = (uint32_t)blob_cursor;
                    
                    uint32_t d_sz = 4 + ((uint32_t)std::ranges::size(field) * sizeof(K));

                    uint32_t le_off = apply_policy<Policy>(write_off);
                    std::memcpy(&this->header_ptr[idx * 5 + 1], &le_off, 4);
                    uint32_t le_sz = apply_policy<Policy>(d_sz);
                    std::memcpy(this->body_ptr + write_off, &le_sz, 4);

                    std::span<uint8_t> target(this->body_ptr + write_off + 4, d_sz);
                    flatten_set<T, Policy>(field, target);
                    blob_cursor = write_off + 4 + d_sz;

                } else if constexpr (is_noncontinuous_range_v<T>) {
                    using E = std::ranges::range_value_t<T>;
                    blob_cursor = align_to(blob_cursor, 4);
                    uint32_t write_off = (uint32_t)blob_cursor;
                    uint32_t data_len = (uint32_t)std::ranges::size(field);
                    uint32_t d_sz = data_len * sizeof(E);

                    uint32_t le_off = apply_policy<Policy>(write_off);
                    std::memcpy(&this->header_ptr[idx * 5 + 1], &le_off, 4);
                    uint32_t le_sz = apply_policy<Policy>(d_sz);
                    std::memcpy(this->body_ptr + write_off, &le_sz, 4);

                    size_t d_start = align_to(write_off + 4, alignof(E));
                    std::span<E> target((E*)(this->body_ptr + d_start), data_len);
                    flatten<T>(field, target);
                    blob_cursor = d_start + d_sz;

                    if constexpr (Policy == endian_policy::network) {
                        std::span<E> dest_span((E*)(this->body_ptr + d_start), data_len);
                        flipEndiannessBlob(dest_span);
                    }
                } else {
                    // SCALARS FINALLY AT THE BOTTOM!
                    constexpr size_t a = alignof(T);
                    uint32_t write_off = 0;
                    if      constexpr (a >= 8) write_off = cursors[0], cursors[0] += 8;
                    else if constexpr (a == 4) write_off = cursors[1], cursors[1] += 4;
                    else if constexpr (a == 2) write_off = cursors[2], cursors[2] += 2;
                    else                       write_off = cursors[3], cursors[3] += 1;

                    uint32_t le_off = apply_policy<Policy>(write_off);
                    std::memcpy(&this->header_ptr[idx * 5 + 1], &le_off, 4);

                    T final_val = apply_policy<Policy>(field);
                    std::memcpy(this->body_ptr + write_off, &final_val, sizeof(T));
                }
            });
            return blob_cursor;
        }

        template <std::size_t N>
        auto get_field() const {
            using T = boost::pfr::tuple_element_t<N, Derived>;
            uint32_t b_off; 
            std::memcpy(&b_off, &header_ptr[N * 5 + 1], 4);
            b_off = apply_policy<Policy>(b_off);

            if constexpr (is_struct_v<T>) {
                uint32_t sz; std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                return std::span<uint8_t>((uint8_t*)(body_ptr + b_off + 4), sz);
            } else if constexpr (is_map_type_v<T> || is_set_type_v<T>) {
                uint32_t sz; std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                size_t start = align_to(b_off + 4, alignof(uint32_t));
                return std::span<uint8_t>((uint8_t*)(body_ptr + start), sz);
            } else if constexpr (is_flat_array<T>{}) {
                using Leaf = get_base_type_t<T>;
                size_t start = align_to(b_off + 4, alignof(Leaf));
                Leaf* raw_ptr = reinterpret_cast<Leaf*>(body_ptr + start);
                
                return array_traits<T>::template make_view<Leaf>(raw_ptr);
            } else if constexpr (is_range_v<T>) {
                uint32_t sz; std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                using E = std::ranges::range_value_t<T>;
                size_t start = align_to(b_off + 4, alignof(E));
                if constexpr (is_string_type_v<T>) return std::string_view((char*)(body_ptr + start), sz);
                else return std::span<E>((E*)(body_ptr + start), sz / sizeof(E));
            } else {
                T val = *(T*)(body_ptr + b_off);
                return apply_policy<Policy>(val);
            }
        }

        inline std::string to_json() const {
            return to_json({});
        }

        inline std::string to_json(const std::array<std::string_view, num_fields>& field_names) const {
            std::string output;
            output.reserve((act_size << 1) + (num_fields << 3) + (num_fields << 3));
            output += "[\n";
            size_t current = 0;

            boost::pfr::for_each_field(Derived{}, [&](const auto& dummy) {
                using T = std::decay_t<decltype(dummy)>;
                uint32_t b_off; 
                std::memcpy(&b_off, &this->header_ptr[current * 5 + 1], 4);
                b_off = apply_policy<Policy>(b_off);

                std::string key = field_names[current].empty() ? myelin::to_string(current) : std::string(field_names[current]);
                output += "   { \"" + key + "\" : ";

                if constexpr (is_struct_v<T>) {
                    uint32_t sz; 
                    std::memcpy(&sz, this->body_ptr + b_off, 4);
                    sz = apply_policy<Policy>(sz);
                    
                    auto child_span = std::span<const uint8_t>((const uint8_t*)(this->body_ptr + b_off + 4), sz);
                    if constexpr(!std::is_trivially_copyable_v<T>) {
                        nested_view<T, Policy> t;
                        myelin::structify<T, Policy>(child_span, t);
                        output += t.to_json();
                    } else {
                        const T& t = *reinterpret_cast<const T*>(child_span.data());
                        output += struct_to_json<T>(t);
                    }
                } else if constexpr (is_map_type_v<T>) {
                    using Pair = std::ranges::range_value_t<T>;
                    using K = std::remove_const_t<std::tuple_element_t<0, Pair>>;
                    using V = std::tuple_element_t<1, Pair>;
                    
                    uint32_t sz; 
                    std::memcpy(&sz, this->body_ptr + b_off, 4);
                    sz = apply_policy<Policy>(sz);
                    
                    std_map_view<K, V> map;
                    myelin::mapify(std::span<const uint8_t>((const uint8_t*)(this->body_ptr + b_off + 4), sz), map);
                    
                    output += "{ ";
                    for (size_t i = 0; i < map.size(); ++i) {
                        auto p = map.pair_at(i);
                        output += "\"" + myelin::to_string(std::get<0>(p)) + "\": " + myelin::to_string(std::get<1>(p));
                        if (i != map.size() - 1) output += ", ";
                    }
                    output += " }";
                } else if constexpr (is_range_v<T>) {
                    uint32_t sz; 
                    std::memcpy(&sz, this->body_ptr + b_off, 4);
                    sz = apply_policy<Policy>(sz);
                    
                    using E = std::ranges::range_value_t<T>;
                    size_t st = b_off + 4;
                    
                    if constexpr (is_string_type_v<T>) {
                        output += "\"" + std::string((char*)(this->body_ptr + st), sz) + "\"";
                    } else if constexpr (is_flat_array<T>{}) {
                        const T& arr = *(const T*)(this->body_ptr + st);
                        output += dump_mult_array<T, Policy>(arr);
                    } else {
                        output += dumpBlob<std::span<E>, Policy>(std::span<E>((E*)(this->body_ptr + st), sz / sizeof(E)));
                    }
                } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, bool>) {
                    T val;
                    std::memcpy(&val, this->body_ptr + b_off, sizeof(T));
                    val = apply_policy<Policy>(val); 
                    
                    if constexpr (std::is_same_v<T, bool>) {
                        output += (val ? "true" : "false");
                    } else if constexpr (std::is_integral_v<T> && sizeof(T) == 1) {
                        output += myelin::to_string((int)val);
                    } else {
                        output += myelin::to_string(val);
                    }
                } else {
                    output += "\"[Unsupported JSON Type]\"";
                }

                output += (current < num_fields - 1) ? " },\n" : " }\n";
                ++current;
            });

            output += "]";
            output.resize(output.size());
            return output;
        }

        inline void deserialize(Derived& dest) {
            size_t current = 0;
            boost::pfr::for_each_field(dest, [&](auto& dummy) {
                using T = std::decay_t<decltype(dummy)>;
                uint32_t b_off; 
                std::memcpy(&b_off, &this->header_ptr[current * 5 + 1], 4);
                b_off = apply_policy<Policy>(b_off);

                if constexpr (is_struct_v<T>) {
                    uint32_t sz; 
                    std::memcpy(&sz, this->body_ptr + b_off, 4);
                    sz = apply_policy<Policy>(sz);

                    if constexpr (!std::is_trivially_copyable_v<T>) {
                        recur_view<T, Policy> v;
                        v.header_ptr = this->body_ptr + b_off + 4;
                        v.body_ptr = v.header_ptr + v.header_size;
                        v.deserialize(std::span<uint8_t>((uint8_t*)(body_ptr + b_off + 4), sz), dummy); 
                    } else {
                        size_t st = align_to(b_off + 4, alignof(T));
                        std::memcpy(&dummy, this->body_ptr + st, sizeof(T));
                    }
                } else if constexpr (is_map_type_v<T>) {
                    using Pair = std::ranges::range_value_t<T>;
                    using K = std::remove_const_t<std::tuple_element_t<0, Pair>>;
                    using V = std::tuple_element_t<1, Pair>;
                    
                    uint32_t sz; 
                    std::memcpy(&sz, this->body_ptr + b_off, 4);
                    sz = apply_policy<Policy>(sz);
                    
                    myelin::mapify(std::span<const uint8_t>((const uint8_t*)(this->body_ptr + b_off + 4), sz), dummy);
                } else if constexpr (is_range_v<T>) {
                    uint32_t sz; 
                    std::memcpy(&sz, this->body_ptr + b_off, 4);
                    sz = apply_policy<Policy>(sz);

                    using E = std::ranges::range_value_t<T>;
                    size_t st = b_off + 4; 
                    
                    if constexpr (is_string_type_v<T>) {
                        dummy = std::string((char*)(this->body_ptr + st), sz);
                    } else {
                        auto s = std::span<E>((E*)(this->body_ptr + st), sz / sizeof(E));
                        dummy.assign(s.begin(), s.end());
                    }
                } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, bool>) {
                    T val;
                    std::memcpy(&val, this->body_ptr + b_off, sizeof(T));
                    val = apply_policy<Policy>(val); 
                    dummy = val;
                } else {
                    throw std::runtime_error("Deserialize Error");
                }
                ++current;
            });
        }
    };


    template <typename Derived, endian_policy Policy = endian_policy::native>
    struct recur_view : public basic_view<Derived, recur_view<Derived, Policy>, Policy> {

        static inline size_t size(const Derived& obj, myelin::recur_data& store) {
            basic_view<Derived, myelin::recur_view<Derived, Policy>, Policy> temp;
            
            store.body_size = temp.resize_calc(obj, store.count);
            //store = std::move(temp.nested);
            
            for (auto &i : store.count) store.header_size += i;
            return store.body_size + (store.header_size *= 5);
        }

        inline void serialize(const Derived& obj, const myelin::recur_data& data, uint8_t* dest) {
            this->header_ptr = dest;
            this->body_ptr = dest + this->header_size;
            
            //this->nested = data.nested;
            
            this->act_size = this->pack_data_bucketed(obj, (uint16_t*)(data.count));
        }

        inline void deserialize(std::span<const uint8_t> data, Derived& dest) {
            // 1. Point the view's internal pointers at the provided span
            this->header_ptr = const_cast<uint8_t*>(data.data());
            this->body_ptr   = this->header_ptr + this->header_size;

            // 2. Call the base basic_view::deserialize(dest)
            // This uses the current++ logic we just fixed
            basic_view<Derived, recur_view<Derived, Policy>, Policy>::deserialize(dest);
        }

    };

    // --- MEM VIEW ---
    template <typename Derived, endian_policy Policy = endian_policy::native>
    struct mem_view : public basic_view<Derived, mem_view<Derived, Policy>, Policy> {
        std::array<uint8_t, basic_view<Derived, mem_view<Derived, Policy>, Policy>::header_size> h_buf{};
        size_t capacity = 0;
        mem_view() { this->header_ptr = h_buf.data();}
        ~mem_view() { if (this->body_ptr) free(this->body_ptr); }

        inline void serialize(const Derived& obj) {
            uint16_t counts[5] = {0}; 
            size_t req = this->resize_calc(obj, counts);
            if (req > capacity) {
                capacity = req;
                this->body_ptr = (uint8_t*)realloc(this->body_ptr, capacity);
            }
            this->act_size = this->pack_data_bucketed(obj, counts);
        }

        inline void pack_to_disk(const std::string& path, std::string_view note = "") {
            std::ofstream ofs(path, std::ios::binary);
            uint32_t f = apply_policy<Policy>((uint32_t)this->num_fields); ofs.write((char*)&f, 4);
            ofs.write((char*)this->header_ptr, this->header_size);
            uint32_t s = apply_policy<Policy>((uint32_t)this->act_size); ofs.write((char*)&s, 4);
            ofs.write((char*)this->body_ptr, this->act_size);
            if (!note.empty()) {
                uint32_t n = (uint32_t)note.size(); ofs.write((char*)note.data(), n);
                uint32_t le_n = apply_policy<Policy>(n);
                ofs.write((char*)&le_n, 4); ofs.write("MYELIN_", 7);
            }
        }

        inline void parse(const std::string& path, std::string* note_out = nullptr) {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs) return;
            uint32_t f; ifs.read((char*)&f, 4); // Check fields if needed
            ifs.read((char*)this->header_ptr, this->header_size);
            uint32_t sz; ifs.read((char*)&sz, 4);
            sz = apply_policy<Policy>(sz);
            if (this->body_ptr) free(this->body_ptr);
            this->body_ptr = (uint8_t*)malloc(sz); ifs.read((char*)this->body_ptr, sz);
            this->act_size = sz;
            if (note_out) {
                ifs.seekg(-7, std::ios::end); char maj[8] = {0}; ifs.read(maj, 7);
                if (std::string_view(maj) == "MYELIN_") {
                    uint32_t n_sz; ifs.seekg(-11, std::ios::end); ifs.read((char*)&n_sz, 4);
                    n_sz = apply_policy<Policy>(n_sz);
                    ifs.seekg(-(11 + (long)n_sz), std::ios::end); note_out->resize(n_sz);
                    ifs.read(note_out->data(), n_sz);
                }
            }
        }
    };

    // --- MAP VIEW ---
    template <typename Derived, endian_policy Policy = endian_policy::native>
    struct map_view : public basic_view<Derived, map_view<Derived, Policy>, Policy> {
        int fd = -1; 
        MappedRegion region;

        ~map_view() { 
            MemoryMapper::unmap(region); 
            if (fd != -1) {
                #ifdef _WIN32
                    _close(fd);
                #else
                    close(fd);
                #endif
            }
        }

        inline void map(const std::string& path) {
            #ifdef _WIN32
                // Windows-specific open with binary flag to prevent newline translation
                fd = _open(path.c_str(), _O_RDWR | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE);
                if (fd == -1) throw std::runtime_error("MYELIN: _open failed");
                size_t file_size = _lseeki64(fd, 0, SEEK_END);
            #else
                fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
                if (fd == -1) throw std::runtime_error("MYELIN: open failed");
                size_t file_size = lseek(fd, 0, SEEK_END);
            #endif

            if (file_size > 0) {
                region = MemoryMapper::map(fd, file_size, 0);
                
                this->header_ptr = (uint8_t*)region.addr + 4;
                
                uint32_t b_sz; 
                std::memcpy(&b_sz, this->header_ptr + this->header_size, 4);
                this->act_size = apply_policy<Policy>(b_sz);
                this->body_ptr = this->header_ptr + this->header_size + 4;
            }
        }

        inline std::string get_note() const {
            if (!region.addr || region.size < 11) return "";
            const uint8_t* footer = (uint8_t*)region.addr + region.size - 7;
            if (std::memcmp(footer, "MYELIN_", 7) != 0) return "";
            
            uint32_t n_sz; 
            std::memcpy(&n_sz, (uint8_t*)region.addr + region.size - 11, 4);
            n_sz = apply_policy<Policy>(n_sz);
            return std::string((char*)region.addr + region.size - 11 - n_sz, n_sz);
        }

        inline void serialize(const Derived& obj, const std::string& new_note = "") {
            if (fd == -1) return;
            std::string note = new_note.empty() ? get_note() : new_note; 
            uint16_t counts[5] = {0}; 
            size_t req_body = this->resize_calc(obj, counts);
            
            size_t note_total = note.empty() ? 0 : (note.size() + 11);
            size_t total_req = 8 + this->header_size + req_body + note_total;

            if (total_req > region.size) {
                MemoryMapper::unmap(region);

            #ifdef _WIN32
                if (_chsize_s(fd, total_req) != 0) throw std::runtime_error("MYELIN: _chsize_s failed");
            #else
                if (ftruncate(fd, total_req) == -1) throw std::runtime_error("MYELIN: ftruncate failed");
            #endif
                
                region = MemoryMapper::map(fd, total_req, 0);
                
                // Re-write the field count header after resizing
                uint32_t f = apply_policy<Policy>((uint32_t)this->num_fields); 
                std::memcpy(region.addr, &f, 4);
                
                this->header_ptr = (uint8_t*)region.addr + 4;
                this->body_ptr = this->header_ptr + this->header_size + 4;
            }

            this->act_size = this->pack_data_bucketed(obj, counts);
            
            uint32_t s = apply_policy<Policy>((uint32_t)this->act_size);
            std::memcpy(this->header_ptr + this->header_size, &s, 4);

            if (!note.empty()) {
                uint8_t* n_ptr = (uint8_t*)this->body_ptr + req_body;
                std::memcpy(n_ptr, note.data(), note.size());
                
                uint32_t n_sz_swapped = apply_policy<Policy>((uint32_t)note.size());
                std::memcpy(n_ptr + (uint32_t)note.size(), &n_sz_swapped, 4);
                std::memcpy(n_ptr + (uint32_t)note.size() + 4, "MYELIN_", 7);
            }
        }
    };

}