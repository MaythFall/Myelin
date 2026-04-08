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
    inline constexpr bool is_continuous_v = std::ranges::contiguous_range<T> && !std::is_array_v<std::decay_t<T>>;

    template <typename T>
    inline constexpr bool is_noncontinuous_range_v = !std::ranges::contiguous_range<T> && !std::is_scalar_v<T>;

    template <typename T>
    inline constexpr bool is_struct_v = std::is_class_v<T> && std::is_aggregate_v<T>;

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

}