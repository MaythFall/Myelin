#pragma once
#include "traits.hpp"

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
        STR = 0x0F, VEC = 0x20, ARR = 0x10, SPAN = 0x30, MAP = 0x40, DEQ = 0x50, LIST = 0x60, SET = 0x70
    };

    enum class endian_policy {
        native,
        network // Force Big Endian (Network Byte Order)
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

}