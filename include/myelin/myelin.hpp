#pragma once

// Myelin - High-Velocity C++20 Serialization Engine
// Distributed as a single-header library.

// --- Start of traits.hpp ---
/*
 * Myelin - High-Velocity C++20 Serialization Engine
 * Copyright (C) 2026 Adam Brazda
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include <bit>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <type_traits>
#include <span>
#include <array>
#include <cstring>
#include <ranges>
#include <concepts>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <charconv>
#include <deque>
#include <map>
#include <unordered_map>
#include <list>
#include <set>
#include <algorithm>
#include <unordered_set>
#include "boost/pfr.hpp"

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <cstring>
#endif

namespace myelin {

    template <typename T>
    inline constexpr bool is_string_type_v = std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, std::string_view>;

    template <typename T>
    struct is_flat_array : std::false_type {
        // Inherits operator bool() from std::false_type (returns false)
    };

    template <typename T, std::size_t N>
    struct is_flat_array<std::array<T, N>> : 
        std::bool_constant<std::is_arithmetic_v<T> || is_flat_array<T>::value> 
    {};
    
    template <typename T>
    struct flat_array_count : std::integral_constant<size_t, 1> {};

    template <typename T, std::size_t N>
    struct flat_array_count<std::array<T, N>> : 
        std::integral_constant<size_t, N * flat_array_count<T>::value> {};

    template <typename T>
    inline constexpr bool is_continuous_v = (std::ranges::contiguous_range<T> || is_flat_array<T>{}) && !std::is_array_v<std::decay_t<T>>;

    template <typename T>
    inline constexpr bool is_noncontinuous_range_v = !std::ranges::contiguous_range<T> && !std::is_scalar_v<T>;

    template <typename T>
    inline constexpr bool is_struct_v = std::is_class_v<T> && std::is_aggregate_v<T> && !is_flat_array<T>{};

    template <typename T>
    inline constexpr bool is_range_v = is_continuous_v<T> || is_noncontinuous_range_v<T>;

    template <typename T> struct is_vector : std::false_type {};
    template <typename T, typename Alloc> 
    struct is_vector<std::vector<T, Alloc>> : std::true_type {};
    template <typename T> 
    inline constexpr bool is_vector_v = is_vector<std::remove_cvref_t<T>>::value;

    template <typename T> struct is_std_array : std::false_type {};
    template <typename T, std::size_t N> 
    struct is_std_array<std::array<T, N>> : std::true_type {};
    template <typename T> 
    inline constexpr bool is_std_array_v = is_std_array<std::remove_cvref_t<T>>::value;

    template <typename T> struct is_map : std::false_type {};
    template <typename K, typename V, typename C, typename A>
    struct is_map<std::map<K, V, C, A>> : std::true_type {};

    template <typename T> struct is_unordered_map : std::false_type {};
    template <typename K, typename V, typename H, typename P, typename A>
    struct is_unordered_map<std::unordered_map<K, V, H, P, A>> : std::true_type {};

    template <typename T> struct is_list : std::false_type {};
    template <typename T, typename Alloc> 
    struct is_list<std::list<T, Alloc>> : std::true_type {};
    template <typename T> 
    inline constexpr bool is_list_v = is_list<std::remove_cvref_t<T>>::value;

    template <typename T> struct is_deque : std::false_type {};
    template <typename T, typename Alloc> 
    struct is_deque<std::deque<T, Alloc>> : std::true_type {};
    template <typename T> 
    inline constexpr bool is_deque_v = is_deque<std::remove_cvref_t<T>>::value;

    template <typename T>
    inline constexpr bool is_map_type_v = 
        is_map<std::remove_cvref_t<T>>::value || 
        is_unordered_map<std::remove_cvref_t<T>>::value;

    template <typename T> struct is_set : std::false_type {};
    template <typename K, typename C, typename A>
    struct is_set<std::set<K, C, A>> : std::true_type {};

    template <typename T> struct is_unordered_set : std::false_type {};
    template <typename K, typename H, typename P, typename A>
    struct is_unordered_set<std::unordered_set<K, H, P, A>> : std::true_type {};

    template <typename T>
    inline constexpr bool is_set_type_v = 
        is_set<std::remove_cvref_t<T>>::value || 
        is_unordered_set<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct get_base_type { using type = T; };

    template <typename T, std::size_t N>
    struct get_base_type<std::array<T, N>> { 
        using type = typename get_base_type<T>::type; 
    };

    template <typename T>
    using get_base_type_t = typename get_base_type<T>::type;
    
}
// --- End of traits.hpp ---

// --- Start of utils.hpp ---

namespace myelin {

    struct MappedRegion {
        void* addr       = nullptr;
        void* base       = nullptr;
        size_t size      = 0;       
    };

    class MemoryMapper {
    public:
        static MappedRegion map(int fd, size_t size, size_t offset = 0) {
            MappedRegion region;

            #ifdef _WIN32
                // Windows Finer Touch: 64KB Granularity
                SYSTEM_INFO si;
                GetSystemInfo(&si);
                const size_t align = si.dwAllocationGranularity;
                
                const size_t aligned_offset = (offset / align) * align;
                const size_t diff = offset - aligned_offset;
                region.size = size + diff;

                HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
                if (hFile == INVALID_HANDLE_VALUE) throw std::runtime_error("Invalid file handle");

                HANDLE hMapping = CreateFileMapping(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
                if (!hMapping) throw std::runtime_error("CreateFileMapping failed");

                region.base = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 
                                        static_cast<DWORD>(aligned_offset >> 32), 
                                        static_cast<DWORD>(aligned_offset & 0xFFFFFFFF), 
                                        region.size);
                CloseHandle(hMapping);

                if (!region.base) throw std::runtime_error("MapViewOfFile failed");
                region.addr = static_cast<char*>(region.base) + diff;

            #else
                const size_t align = sysconf(_SC_PAGESIZE);
                
                const size_t aligned_offset = (offset / align) * align;
                const size_t diff = offset - aligned_offset;
                region.size = size + diff;

                region.base = ::mmap(nullptr, region.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, aligned_offset);
                
                if (region.base == MAP_FAILED) {
                    throw std::runtime_error("mmap failed: " + std::string(strerror(errno)));
                }
                madvise(region.base, region.size, MADV_SEQUENTIAL);
                
                region.addr = static_cast<char*>(region.base) + diff;
            #endif

            return region;
        }

        static void unmap(MappedRegion& region) {
            if (!region.base) return;

            #ifdef _WIN32
                if (!UnmapViewOfFile(region.base)) {
                    throw std::runtime_error("UnmapViewOfFile failed");
                }
            #else
                if (::munmap(region.base, region.size) == -1) {
                    throw std::runtime_error("munmap failed");
                }
            #endif
            
            region = {}; // Reset
        }
    };

    template <typename K, typename V, bool Ordered = true>
    class std_map_view {
        std::span<const K> _keys;
        std::span<const V> _values;

    public:
        void set_views(std::span<const K> ks, std::span<const V> vs) {
            _keys = ks;
            _values = vs;
        }

        inline const V* find(const K& key) const {
            if constexpr (Ordered) {
                auto it = std::lower_bound(_keys.begin(), _keys.end(), key);
                if (it != _keys.end() && *it == key) {
                    return &_values[std::distance(_keys.begin(), it)];
                }
                return nullptr;
            } else {
                for (auto it = _keys.begin(); it != _keys.end(); ++it) {
                    if (*it == key) {
                        return &_values[std::distance(_keys.begin(), it)];
                    }
                }
                return nullptr;
            }
        }

        inline const std::pair<K,V> pair_at(size_t i) {
            return std::make_pair(_keys[i], _values[i]);
        }

        inline size_t size() const {
            return _keys.size();
        }
    };

    template <typename K, bool Ordered = true>
    class std_set_view {
        std::span<const K> _keys;
    public:
        void set_view(std::span<const K> ks) { _keys = ks; }
        
        inline bool contains(const K& key) const {
            if constexpr (Ordered) {
                return std::binary_search(_keys.begin(), _keys.end(), key);
            } else {
                for (const auto& k : _keys) if (k == key) return true;
                return false;
            }
        }
        inline std::span<const K> data() const { return _keys; }
    };

    enum class TypeMap : uint8_t {
        U8 = 0x00, U16 = 0x01, U32 = 0x02, U64 = 0x03,
        CHAR = 0x04, BOOL = 0x05, FLOAT = 0x06, DOUBLE = 0x07,
        I8 = 0x0A, I16 = 0x0B, I32 = 0x0C, I64 = 0x0D,
        STR = 0x0F, VEC = 0x20, ARR = 0x10, SPAN = 0x30, MAP = 0x40, DEQ = 0x50, LIST = 0x60, SET = 0x70, STRUCT = 0x80
    };

    enum class endian_policy {
        native,
        network // Force Big Endian (Network Byte Order)
    };

    struct recur_data {
        size_t header_size = 0;
        size_t body_size = 0;
        uint8_t count[5] = {0,0,0,0,0};
        std::vector<recur_data> nested;
    };

    template <typename T>
    inline std::string to_string(T val) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec == std::errc{}) return std::string(buf, ptr - buf);
        return "";
    }

    // --- Helpers ---
    

    inline size_t align_to(size_t offset, size_t a) {
        return (a == 0) ? offset : (offset + (a - 1)) & ~(a - 1);
    }

    template <typename T>
    inline void flatten(const T& data, std::span<std::ranges::range_value_t<T>> out) {
        static_assert(is_noncontinuous_range_v<T>, "Range must be non-continuous");
        std::ranges::copy(data, out.begin());
    }

    template <typename T>
    inline constexpr T flipEndianness(const T& val) {
        if constexpr (sizeof(T) <= 1) return val;
        if constexpr (std::is_integral_v<T>) {
            return std::byteswap(val);
        } else if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) {
                return std::bit_cast<T>(std::byteswap(std::bit_cast<uint32_t>(val)));
            } else if constexpr (sizeof(T) == 8) {
                return std::bit_cast<T>(std::byteswap(std::bit_cast<uint64_t>(val)));
            }
        }
        return val;
    }


    template <typename T, endian_policy Policy = endian_policy::native>
    inline void flatten_map(const T& data, std::span<uint8_t>& out) {
        using Pair = std::ranges::range_value_t<T>;
        using K = std::remove_const_t<std::tuple_element_t<0, Pair>>;
        using V = std::tuple_element_t<1, Pair>;
        static_assert(std::is_trivially_copyable_v<K> && std::is_trivially_copyable_v<V>, "Keys and Values must be copyable");

        uint32_t map_size = static_cast<uint32_t>(data.size());
        size_t total_bytes = 4 + (map_size * (sizeof(K) + sizeof(V)));
        
        //out.resize(total_bytes);
        uint8_t* ptr = out.data();

        *reinterpret_cast<uint32_t*>(ptr) = map_size;
        
        K* key_ptr = reinterpret_cast<K*>(ptr + 4);
        V* val_ptr = reinterpret_cast<V*>(ptr + 4 + (map_size * sizeof(K)));

        size_t i = 0;
        for (const auto& [k, v] : data) {
            if constexpr (Policy == endian_policy::network) {
                key_ptr[i] = flipEndianness<K>(k);
                val_ptr[i] = flipEndianness<V>(v);
                ++i;
            } else {
                key_ptr[i] = k;
                val_ptr[i] = v;
                ++i;
            }
        }
    }

    template <typename T, endian_policy Policy = endian_policy::native>
    inline void flatten_set(const T& data, std::span<uint8_t>& out) {
        using K = std::remove_const_t<typename T::value_type>;
        static_assert(std::is_trivially_copyable_v<K>, "Set keys must be copyable");

        uint32_t set_size = static_cast<uint32_t>(data.size());
        size_t total_bytes = 4 + (set_size * sizeof(K));
        
        //out.resize(total_bytes);
        *reinterpret_cast<uint32_t*>(out.data()) = set_size;
        
        K* key_ptr = reinterpret_cast<K*>(out.data() + 4);
        size_t i = 0;
        for (const auto& k : data) {
            key_ptr[i++] = (Policy == endian_policy::network) ? flipEndianness<K>(k) : k;
        }
    }

    // Helper to conditionally swap bytes based on policy
    template <endian_policy Policy, typename T>
    inline constexpr T apply_policy(T val) {
        if constexpr (Policy == endian_policy::network && std::endian::native == std::endian::little) {
            if constexpr (std::is_integral_v<T> && sizeof(T) > 1) {
                return std::byteswap(val);
            } 
            else if constexpr (std::is_floating_point_v<T>) {
                // Bit-cast to int, swap, then cast back to float/double
                if constexpr (sizeof(T) == 4) {
                    return std::bit_cast<T>(std::byteswap(std::bit_cast<uint32_t>(val)));
                } else if constexpr (sizeof(T) == 8) {
                    return std::bit_cast<T>(std::byteswap(std::bit_cast<uint64_t>(val)));
                }
            }
        }
        return val;
    }

    template <typename T>
    inline constexpr TypeMap get_type_tag() {
        using U = std::decay_t<T>;

        if constexpr (is_string_type_v<U>) return TypeMap::STR;

        if constexpr (is_continuous_v<U>) {
            constexpr uint8_t inner = (uint8_t)get_type_tag<std::ranges::range_value_t<U>>();
            
            if constexpr (std::is_array_v<U> || is_std_array_v<U>) {
                return static_cast<TypeMap>((uint8_t)TypeMap::ARR | inner);
            } else if constexpr (is_vector_v<U>) {
                return static_cast<TypeMap>((uint8_t)TypeMap::VEC | inner);
            } else {
                return static_cast<TypeMap>((uint8_t)TypeMap::SPAN | inner);
            }
        }

        if constexpr (is_list_v<U>) {
            constexpr uint8_t inner = (uint8_t)get_type_tag<std::ranges::range_value_t<U>>();
            return static_cast<TypeMap>((uint8_t)TypeMap::LIST | inner);
        }
        
        if constexpr (is_deque_v<U>) {
            constexpr uint8_t inner = (uint8_t)get_type_tag<std::ranges::range_value_t<U>>();
            return static_cast<TypeMap>((uint8_t)TypeMap::DEQ | inner);
        }

        if constexpr (is_map_type_v<U>) return TypeMap::MAP;
        if constexpr (is_set_type_v<U>) return TypeMap::SET;

        if constexpr (std::is_same_v<U, uint64_t>) return TypeMap::U64;
        if constexpr (std::is_same_v<U, int64_t>)  return TypeMap::I64;
        if constexpr (std::is_same_v<U, uint32_t>) return TypeMap::U32;
        if constexpr (std::is_same_v<U, int32_t>)  return TypeMap::I32;
        if constexpr (std::is_same_v<U, uint16_t>) return TypeMap::U16;
        if constexpr (std::is_same_v<U, int16_t>)  return TypeMap::I16;
        if constexpr (std::is_same_v<U, float>)    return TypeMap::FLOAT;
        if constexpr (std::is_same_v<U, double>)   return TypeMap::DOUBLE;
        if constexpr (std::is_same_v<U, bool>)     return TypeMap::BOOL;
        if constexpr (std::is_same_v<U, char>)     return TypeMap::CHAR;

        return TypeMap::U8; // Default fallback
    }

    template <typename T>
    inline constexpr void flipEndiannessBlob(T& blob) {
        if constexpr (!is_continuous_v<T>) return;
        using E = std::ranges::range_value_t<T>;
        if constexpr (sizeof(E) <= 1) return;

        for (auto& v : blob) {
            if constexpr (std::is_integral_v<E>) {
                v = std::byteswap(v);
            } else if constexpr (std::is_floating_point_v<E>) {
                // Using bit_cast to pun floats/doubles to ints for the swap
                if constexpr (sizeof(E) == 4) {
                    v = std::bit_cast<E>(std::byteswap(std::bit_cast<uint32_t>(v)));
                } else if constexpr (sizeof(E) == 8) {
                    v = std::bit_cast<E>(std::byteswap(std::bit_cast<uint64_t>(v)));
                }
            }
        }
    }

    template <typename SpanType, endian_policy Policy = endian_policy::native>
    inline std::string dumpBlob(const SpanType& blob) {        
        if (blob.empty()) return "[]";
        
        using E = typename SpanType::value_type;
        std::string out;
        
        if constexpr (std::is_same_v<E, char> || std::is_same_v<E, unsigned char>) {
            out.reserve(blob.size() + 2);
            out = "\"";
            out.append(reinterpret_cast<const char*>(std::ranges::data(blob)), std::ranges::size(blob));
            out += "\"";
            return out;
        } 

        out.reserve((blob.size() << 2) + 2);
        out = "[ ";
        
        for (size_t i = 0; i < blob.size(); ++i) {
            if constexpr (std::is_arithmetic_v<E> && !std::is_same_v<E, bool>) {
                out += myelin::to_string(apply_policy<Policy>(blob[i]));
            } 
            else if constexpr (std::is_same_v<E, bool>) {
                out += (apply_policy<Policy>(blob[i]) ? "true" : "false");
            }
            if (i < blob.size() - 1) out += ", ";
        }
        
        out += " ]";
        return out;
    }

    template <typename T, endian_policy Policy>
    inline std::string dump_mult_array(const T& data) {
        using I = std::ranges::range_value_t<T>;
        std::string out = "[ ";
        
        size_t count = std::ranges::size(data);
        size_t i = 0;

        for (const auto& element : data) {
            if constexpr (std::is_arithmetic_v<I>) {
                I val = apply_policy<Policy>(element);
                if constexpr (std::is_same_v<I, bool>) out += (val ? "true" : "false");
                else if constexpr (std::is_integral_v<I> && sizeof(I) == 1) out += myelin::to_string((int)val);
                else out += myelin::to_string(val);
            } else {
                out += dump_mult_array<I, Policy>(element);
            }

            if (++i < count) out += ", ";
        }

        out += " ]";
        return out;
    }

    template <typename Derived, endian_policy Policy = endian_policy::native>
    class nested_view {
        static constexpr uint32_t num_fields = boost::pfr::tuple_size_v<Derived>;
        static constexpr size_t header_size = num_fields * 5;
        std::span<const uint8_t> _data; 

        public:
        nested_view() = default;
        nested_view(std::span<const uint8_t> s) : _data(s) {}

        template <std::size_t N>
        auto get_field() const {
            using T = boost::pfr::tuple_element_t<N, Derived>;
            const uint8_t* body_ptr = _data.data() + header_size;
            
            uint32_t b_off; 
            std::memcpy(&b_off, &_data[N * 5 + 1], 4); 
            b_off = apply_policy<Policy>(b_off);

            if constexpr (is_struct_v<T>) {
                uint32_t sz; std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                return std::span<const uint8_t>(body_ptr + b_off + 4, sz);
            } else if constexpr (is_map_type_v<T> || is_set_type_v<T>) {
                uint32_t sz; std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                size_t start = align_to(b_off + 4, alignof(uint32_t));
                return std::span<const uint8_t>((const uint8_t*)(body_ptr + start), sz);
            } else if constexpr (is_range_v<T>) {
                uint32_t sz; std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                using E = std::ranges::range_value_t<T>;
                size_t start = align_to(b_off + 4, alignof(E));
                if constexpr (is_string_type_v<T>) return std::string_view((const char*)(body_ptr + start), sz);
                else return std::span<const E>((const E*)(body_ptr + start), sz / sizeof(E));
            } else {
                T val;
                std::memcpy(&val, body_ptr + b_off, sizeof(T));
                return apply_policy<Policy>(val);
            }
        }

        // DECLARE ONLY, NO LOGIC HERE!
        inline std::string to_json() const;
        inline std::string to_json(const std::array<std::string_view, num_fields>& field_names) const;
    };

    // Allocate a map for the serialized map object
    template <typename T, typename M>
    inline void mapify(const T& data, M& out) {
        static_assert(is_continuous_v<T>, "Source must be continuous");
        static_assert(is_map_type_v<M>, "Output must be a map");
        
        using Pair = std::ranges::range_value_t<M>;
        using K = std::remove_const_t<std::tuple_element_t<0, Pair>>;
        using V = std::tuple_element_t<1, Pair>;

        uint32_t map_size = *reinterpret_cast<const uint32_t*>(data.data());
        //out.reserve(map_size);

        const uint8_t* key_start = data.data() + 4;
        const uint8_t* val_start = key_start + (map_size * sizeof(K));

        std::span<const K> keys(reinterpret_cast<const K*>(key_start), map_size);
        std::span<const V> values(reinterpret_cast<const V*>(val_start), map_size);

        for (size_t i = 0; i < map_size; ++i) {
            out.emplace(keys[i], values[i]);
        }
    }

    // Read-only reference "map" object: DO NOT USE FOR unordered_maps with REPEATING KEYS
    // K = Key Type, V = Value Type, T = Map type Ordered = map vs unordered_map
    template <typename K, typename V, typename T, bool Ordered = true>
    inline void mapify(const T& data, myelin::std_map_view<K, V, Ordered>& out) {
        static_assert(is_continuous_v<T>, "Source buffer must be contiguous (Mmap/Vector)");
        
        uint32_t map_size = *reinterpret_cast<const uint32_t*>(data.data());
        
        const uint8_t* key_start = data.data() + 4;
        const uint8_t* val_start = key_start + (map_size * sizeof(K));

        out.set_views(
            std::span<const K>(reinterpret_cast<const K*>(key_start), map_size),
            std::span<const V>(reinterpret_cast<const V*>(val_start), map_size)
        );
    }

    template <typename K, typename T, bool Ordered = true>
    inline void setify(const T& data, myelin::std_set_view<K, Ordered>& out) {
        static_assert(is_continuous_v<T>, "Source must be continuous");
        uint32_t set_size = *reinterpret_cast<const uint32_t*>(data.data());
        out.set_view(std::span<const K>(reinterpret_cast<const K*>(data.data() + 4), set_size));
    }

    template <typename Derived, endian_policy Policy = endian_policy::native>
    inline void structify(std::span<const uint8_t> data, nested_view<Derived, Policy>& nest) {
        static_assert(is_struct_v<Derived>, "Must be a struct");
        nest = nested_view<Derived, Policy>(data); 
    }

    template <typename Derived, endian_policy Policy>
    inline std::string nested_view<Derived, Policy>::to_json() const {
        return to_json({}); // Delegate to the version with field names, using an empty array
    }

    template <typename Derived, endian_policy Policy>
    inline std::string nested_view<Derived, Policy>::to_json(const std::array<std::string_view, num_fields>& field_names) const {
        std::string output;
        size_t act_size = _data.size(); 
        output.reserve((act_size << 1) + (num_fields << 3) + (num_fields << 3));
        output += "{\n"; // Use { for objects, not [
        size_t current = 0;
        const uint8_t* body_ptr = _data.data() + header_size;

        boost::pfr::for_each_field(Derived{}, [&](const auto& dummy) {
            using T = std::decay_t<decltype(dummy)>;
            uint32_t b_off; 
            std::memcpy(&b_off, &_data[current * 5 + 1], 4);
            b_off = apply_policy<Policy>(b_off);

            std::string key = (field_names.empty() || field_names[current].empty()) ? myelin::to_string(current) : std::string(field_names[current]);
            output += "   \"" + key + "\" : ";

            if constexpr (is_struct_v<T>) {
                uint32_t sz; 
                std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                
                auto child_span = std::span<const uint8_t>((const uint8_t*)(body_ptr + b_off + 4), sz);
                nested_view<T, Policy> t;
                myelin::structify<T, Policy>(child_span, t);
                output += t.to_json(); // Recursive call!
            } else if constexpr (is_map_type_v<T>) {
                using Pair = std::ranges::range_value_t<T>;
                using K = std::remove_const_t<std::tuple_element_t<0, Pair>>;
                using V = std::tuple_element_t<1, Pair>;
                
                uint32_t sz; 
                std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                
                std_map_view<K, V> map;
                myelin::mapify(std::span<const uint8_t>((const uint8_t*)(body_ptr + b_off + 4), sz), map);
                
                output += "{ ";
                for (size_t i = 0; i < map.size(); ++i) {
                    auto p = map.pair_at(i);
                    // JSON strictly requires keys to be strings
                    output += "\"" + myelin::to_string(std::get<0>(p)) + "\": " + myelin::to_string(std::get<1>(p));
                    if (i != map.size() - 1) output += ", ";
                }
                output += " }";
            } else if constexpr (is_range_v<T>) {
                uint32_t sz; 
                std::memcpy(&sz, body_ptr + b_off, 4);
                sz = apply_policy<Policy>(sz);
                
                using E = std::ranges::range_value_t<T>;
                size_t st = align_to(b_off + 4, alignof(E));
                
                if constexpr (is_string_type_v<T>) {
                    output += "\"" + std::string((const char*)(body_ptr + st), sz) + "\"";
                } else {
                    output += dumpBlob<std::span<const E>, Policy>(std::span<const E>((const E*)(body_ptr + st), sz / sizeof(E)));
                }
            } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, bool>) {
                    T val;
                    std::memcpy(&val, body_ptr + b_off, sizeof(T));
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

            output += (current < num_fields - 1) ? ",\n" : "\n";
            current++;
        });

        output += "}";
        return output;
    }

    template <typename T, size_t... Dims>
    struct mult_view;

    template <typename T, size_t N, size_t... NextDims>
    struct mult_view<T, N, NextDims...> {
        T* ptr;
        static constexpr size_t stride = (NextDims * ...);

        inline auto operator[](size_t i) { return mult_view<T, NextDims...>{ ptr + (i * stride) }; }
        inline const auto operator[](size_t i) const { return mult_view<T, NextDims...>{ ptr + (i * stride) }; }

        inline size_t size() const { return N; }
        inline T* data() const { return ptr; }
    };

    template <typename T, size_t N>
    struct mult_view<T, N> {
        T* ptr;
        inline T& operator[](size_t i) { return ptr[i]; }
        inline const T& operator[](size_t i) const { return ptr[i]; }
        
        inline size_t size() const { return N; }
        inline T* data() const { return ptr; }
    };

    template <typename T>
    struct array_traits {
        template <typename P, size_t... D>
        static auto make_view(P* p) { 
            return mult_view<P, D...>{ p }; 
        }
    };

    template <typename T, size_t N>
    struct array_traits<std::array<T, N>> {
        template <typename P, size_t... D>
        static auto make_view(P* p) {
            return array_traits<T>::template make_view<P, D..., N>(p);
        }
    };

}
// --- End of utils.hpp ---

// --- Start of views.hpp ---

namespace myelin {

    template <typename Derived, endian_policy Policy>
    struct recur_view;

    template <typename Derived, typename Impl, endian_policy Policy = endian_policy::native>
    struct basic_view {
        static constexpr uint32_t num_fields = boost::pfr::tuple_size_v<Derived>;
        static constexpr size_t header_size = num_fields * 5;

        uint8_t* header_ptr = nullptr;
        uint8_t* body_ptr = nullptr;
        size_t   act_size = 0;

        size_t nest_idx = 0;
        std::vector<myelin::recur_data> nested;

        inline size_t resize_calc(const Derived& obj, uint8_t* counts) {
            size_t req = 0;
            boost::pfr::for_each_field(obj, [&](const auto& field) {
                using T = std::decay_t<decltype(field)>;
                if constexpr (is_continuous_v<T>) {
                    ++counts[4];
                    if constexpr (is_flat_array<T>{}) req += (sizeof(T)) + 11;
                    else req += (std::ranges::size(field) * sizeof(std::ranges::range_value_t<T>)) + 11; 
                } else if constexpr (is_struct_v<T>) {
                    ++counts[4];
                    nested.push_back(myelin::recur_data());
                    req += recur_view<T, Policy>().size(field, nested.back()) + 11; 
                } else if constexpr (is_noncontinuous_range_v<T>) {
                    ++counts[4];
                    if constexpr (is_map_type_v<T>) {
                        using Pair = std::ranges::range_value_t<T>;
                        using K = std::remove_const_t<std::tuple_element_t<0, Pair>>;
                        using V = std::tuple_element_t<1, Pair>;
                        req += (std::ranges::size(field) * (sizeof(K) + sizeof(V))) + 11;
                    } else {
                        req += (std::ranges::size(field) * sizeof(std::ranges::range_value_t<T>)) + 11;
                    }
                } else {
                    constexpr size_t a = alignof(T);
                    if (a >= 8) ++counts[0]; else if (a == 4) ++counts[1];
                    else if (a == 2) ++counts[2]; else ++counts[3];
                    req += sizeof(T); 
                }
            });
            return req;
        }

        inline size_t pack_data_bucketed(const Derived& obj, uint8_t* counts) {
            uint32_t cursors[4] = {0};
            cursors[1] = counts[0] << 3;
            cursors[2] = cursors[1] + (counts[1] << 2);
            cursors[3] = cursors[2] + (counts[2] << 1);
            
            size_t blob_cursor = cursors[3] + counts[3]; 

            boost::pfr::for_each_field(obj, [&](const auto& field, size_t idx) {
                using T = std::decay_t<decltype(field)>;
                this->header_ptr[idx * 5] = (uint8_t)get_type_tag<T>();

                if constexpr (is_struct_v<T>) {
                    blob_cursor = align_to(blob_cursor, 4);
                    uint32_t write_off = (uint32_t)blob_cursor;
                    uint32_t d_sz = (uint32_t)(nested[nest_idx].header_size + nested[nest_idx].body_size);

                    uint32_t le_off = apply_policy<Policy>(write_off);
                    std::memcpy(&this->header_ptr[idx * 5 + 1], &le_off, 4);
                    
                    uint32_t le_sz = apply_policy<Policy>(d_sz);
                    std::memcpy(this->body_ptr + write_off, &le_sz, 4);
                    
                    size_t d_start = write_off + 4;
                    recur_view<T, Policy>().serialize(field, nested[nest_idx++], this->body_ptr + d_start);
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
                    nested_view<T, Policy> t;
                    myelin::structify<T, Policy>(child_span, t);
                    output += t.to_json();
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
                    size_t st = align_to(b_off + 4, alignof(get_base_type_t<T>));
                    
                    if constexpr (is_string_type_v<T>) {
                        output += "\"" + std::string((char*)(this->body_ptr + st), sz) + "\"";
                    } else if constexpr (is_flat_array<T>{}) {
                        // --- THE FIX ---
                        // Cast the raw blob back to the multidimensional array type
                        const T& arr = *(const T*)(this->body_ptr + st);
                        output += dump_mult_array<T, Policy>(arr);
                    } else {
                        // Normal flat vector/span logic
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
                current++;
            });

            output += "]";
            output.resize(output.size());
            return output;
        }

    };


    template <typename Derived, endian_policy Policy = endian_policy::native>
    struct recur_view : public basic_view<Derived, recur_view<Derived, Policy>, Policy> {

        static inline size_t size(const Derived& obj, myelin::recur_data& store) {
            basic_view<Derived, myelin::recur_view<Derived, Policy>, Policy> temp;
            
            store.body_size = temp.resize_calc(obj, store.count);
            store.nested = std::move(temp.nested);
            
            for (auto &i : store.count) store.header_size += i;
            return store.body_size + (store.header_size *= 5);
        }

        inline void serialize(const Derived& obj, const myelin::recur_data& data, uint8_t* dest) {
            this->header_ptr = dest;
            this->body_ptr = dest + this->header_size;
            
            this->nested = data.nested;
            
            this->act_size = this->pack_data_bucketed(obj, (uint8_t*)(data.count));
        }

    };

    // --- MEM VIEW ---
    template <typename Derived, endian_policy Policy = endian_policy::native>
    struct mem_view : public basic_view<Derived, mem_view<Derived, Policy>, Policy> {
        std::array<uint8_t, basic_view<Derived, mem_view<Derived, Policy>, Policy>::header_size> h_buf{};
        size_t capacity = 0;
        mem_view() { this->header_ptr = h_buf.data(); }
        ~mem_view() { if (this->body_ptr) free(this->body_ptr); }

        inline void serialize(const Derived& obj) {
            uint8_t counts[5] = {0}; 
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
            uint8_t counts[5] = {0}; 
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
// --- End of views.hpp ---

