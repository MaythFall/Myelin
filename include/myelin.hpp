#pragma once
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

    enum class TypeMap : uint8_t {
        U8 = 0x00, U16 = 0x01, U32 = 0x02, U64 = 0x03,
        CHAR = 0x04, BOOL = 0x05, FLOAT = 0x06, DOUBLE = 0x07,
        I8 = 0x0A, I16 = 0x0B, I32 = 0x0C, I64 = 0x0D,
        STR = 0x0F, VEC = 0x20, ARR = 0x10, SPAN = 0x30
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
    template <typename T>
    inline constexpr bool is_string_type_v = std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, std::string_view>;
    
    template <typename T>
    inline constexpr bool is_continuous_v = std::ranges::contiguous_range<T> && !std::is_array_v<std::decay_t<T>>;

    inline size_t align_to(size_t offset, size_t a) {
        return (a == 0) ? offset : (offset + (a - 1)) & ~(a - 1);
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
        if constexpr (std::ranges::contiguous_range<U>) return static_cast<TypeMap>(0x20 | (uint8_t)get_type_tag<std::ranges::range_value_t<U>>());
        if constexpr (std::is_same_v<U, uint64_t>) return TypeMap::U64;
        if constexpr (std::is_same_v<U, double>)   return TypeMap::DOUBLE;
        if constexpr (std::is_same_v<U, uint32_t>) return TypeMap::U32;
        if constexpr (std::is_same_v<U, float>)    return TypeMap::FLOAT;
        return TypeMap::U8;
    }

    template <typename T>
    inline void flipEndiannessBlob(T& blob) {
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

    // --- CRTP Base View ---
    template <typename Derived, typename Impl, endian_policy Policy = endian_policy::native>
    struct basic_view {
        static constexpr uint32_t num_fields = boost::pfr::tuple_size_v<Derived>;
        static constexpr size_t header_size = num_fields * 5;

        uint8_t* header_ptr = nullptr;
        uint8_t* body_ptr = nullptr;
        size_t   act_size = 0;

        inline size_t resize_calc(const Derived& obj, uint8_t* counts) const {
            size_t req = 0;
            boost::pfr::for_each_field(obj, [&](const auto& field) {
                using T = std::decay_t<decltype(field)>;
                if constexpr (is_continuous_v<T>) {
                    ++counts[4];
                    req += (std::ranges::size(field) * sizeof(std::ranges::range_value_t<T>)) + 11;
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

                if constexpr (!is_continuous_v<T>) {
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
                } else {
                    blob_cursor = align_to(blob_cursor, 4);
                    uint32_t write_off = (uint32_t)blob_cursor;
                    using E = std::ranges::range_value_t<T>;
                    uint32_t d_sz = (uint32_t)(std::ranges::size(field) * sizeof(E));
                    
                    uint32_t le_off = apply_policy<Policy>(write_off);
                    std::memcpy(&this->header_ptr[idx * 5 + 1], &le_off, 4);
                    
                    uint32_t le_sz = apply_policy<Policy>(d_sz);
                    std::memcpy(this->body_ptr + write_off, &le_sz, 4);
                    
                    size_t d_start = align_to(write_off + 4, alignof(E));
                    
                    std::memcpy(this->body_ptr + d_start, std::ranges::data(field), d_sz);
                    blob_cursor = d_start + d_sz;
    
                    // Only pay the loop tax if we are actually on a Network Policy
                    if constexpr (Policy == endian_policy::network) {
                        std::span<E> dest_span((E*)(this->body_ptr + d_start), d_sz / sizeof(E));
                        flipEndiannessBlob(dest_span);
                    }
                }
            });
            return blob_cursor;
        }

        template <std::size_t N>
        auto get_field() const {
            using T = boost::pfr::tuple_element_t<N, Derived>;
            uint32_t b_off; std::memcpy(&b_off, &header_ptr[N * 5 + 1], 4);
            b_off = apply_policy<Policy>(b_off);

            if constexpr (is_continuous_v<T>) {
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
            std::string output;
            output.reserve((act_size << 1) + (num_fields << 3));
            output += "[\n";
            size_t current = 0;
            boost::pfr::for_each_field(Derived{}, [&](const auto& dummy) {
                using T = std::decay_t<decltype(dummy)>;
                
                uint32_t b_off; 
                std::memcpy(&b_off, &this->header_ptr[current * 5 + 1], 4);
                b_off = apply_policy<Policy>(b_off);

                output += "   { \"" + myelin::to_string(current) + "\" : ";

                if constexpr (is_continuous_v<T>) {
                    uint32_t sz; 
                    std::memcpy(&sz, this->body_ptr + b_off, 4);
                    sz = apply_policy<Policy>(sz);

                    using E = std::ranges::range_value_t<T>;
                    size_t st = align_to(b_off + 4, alignof(E));

                    if constexpr (is_string_type_v<T>) {
                        output += "\"" + std::string((char*)(this->body_ptr + st), sz) + "\"";
                    } else {
                        output += dumpBlob<std::span<E>, Policy>(std::span<E>((E*)(this->body_ptr + st), sz / sizeof(E)));
                    }
                } else {
                    T val = *(T*)(this->body_ptr + b_off);
                    val = apply_policy<Policy>(val);

                    if constexpr (std::is_same_v<T, bool>) output += (val ? "true" : "false");
                    else if constexpr (std::is_integral_v<T> && sizeof(T) == 1) output += myelin::to_string((int)val);
                    else output += myelin::to_string(val);
                }

                output += (current < num_fields - 1) ? " },\n" : " }\n";
                current++;
            });
            output += "]";
            return output;
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

                if constexpr (is_continuous_v<T>) {
                    uint32_t sz; 
                    std::memcpy(&sz, this->body_ptr + b_off, 4);
                    sz = apply_policy<Policy>(sz);
                    
                    using E = std::ranges::range_value_t<T>;
                    size_t st = align_to(b_off + 4, alignof(E));
                    
                    if constexpr (is_string_type_v<T>) {
                        output += "\"" + std::string((char*)(this->body_ptr + st), sz) + "\"";
                    } else {
                        output += dumpBlob<std::span<E>, Policy>(std::span<E>((E*)(this->body_ptr + st), sz / sizeof(E)));
                    }
                } else {
                    T val = *(T*)(this->body_ptr + b_off);
                    val = apply_policy<Policy>(val); // Policy swap on the actual data
                    
                    if constexpr (std::is_same_v<T, bool>) {
                        output += (val ? "true" : "false");
                    } else if constexpr (std::is_integral_v<T> && sizeof(T) == 1) {
                        output += myelin::to_string((int)val);
                    } else {
                        output += myelin::to_string(val);
                    }
                }

                output += (current < num_fields - 1) ? " },\n" : " }\n";
                current++;
            });

            output += "]";
            output.resize(output.size());
            return output;
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
                
                // Offset by 4 bytes to skip the num_fields header
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