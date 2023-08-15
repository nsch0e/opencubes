#pragma once
#ifndef OPENCUBES_HASHES_HPP
#define OPENCUBES_HASHES_HPP
#include <array>
#include <cstdio>
#include <map>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

#include "cube.hpp"
#include "utils.hpp"

struct HashCube {
    size_t operator()(const Cube &cube) const {
        // https://stackoverflow.com/questions/20511347/a-good-hash-function-for-a-vector/72073933#72073933
        std::size_t seed = cube.size();
        for (auto &p : cube) {
            auto x = HashXYZ()(p);
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

using CubeSet = std::unordered_set<Cube, HashCube, std::equal_to<Cube>>;

class Subsubhashy {
   protected:
    CubeSet set;
    mutable std::shared_mutex set_mutex;

   public:
    template <typename CubeT>
    void insert(CubeT &&c) {
        std::lock_guard lock(set_mutex);
        set.emplace(std::forward<CubeT>(c));
    }

    bool contains(const Cube &c) const {
        std::shared_lock lock(set_mutex);
        auto itr = set.find(c);
        if (itr != set.end()) {
            return true;
        }
        return false;
    }

    auto size() const {
        std::shared_lock lock(set_mutex);
        return set.size();
    }

    void clear() {
        std::lock_guard lock(set_mutex);
        set.clear();
        set.reserve(1);
    }

    auto begin() const { return set.begin(); }
    auto end() const { return set.end(); }
    auto begin() { return set.begin(); }
    auto end() { return set.end(); }
};

template <int NUM>
class Subhashy {
   protected:
    std::array<Subsubhashy, NUM> byhash;

   public:
    template <typename CubeT>
    void insert(CubeT &&c) {
        HashCube hash;
        auto idx = hash(c) % NUM;
        auto &set = byhash[idx];
        if (!set.contains(c)) set.insert(std::forward<CubeT>(c));
        // printf("new size %ld\n\r", byshape[shape].size());
    }

    auto size() const {
        size_t sum = 0;
        for (auto &set : byhash) {
            auto part = set.size();
            sum += part;
        }
        return sum;
    }

    auto begin() const { return byhash.begin(); }
    auto end() const { return byhash.end(); }
    auto begin() { return byhash.begin(); }
    auto end() { return byhash.end(); }
};

class Hashy {
   protected:
    std::map<XYZ, Subhashy<32>> byshape;
    mutable std::shared_mutex set_mutex;

   public:
    static std::vector<XYZ> generateShapes(int n) {
        std::vector<XYZ> out;
        for (int x = 0; x < n; ++x)
            for (int y = x; y < (n - x); ++y)
                for (int z = y; z < (n - x - y); ++z) {
                    if ((x + 1) * (y + 1) * (z + 1) < n)  // not enough space for n cubes
                        continue;
                    out.emplace_back(x, y, z);
                }
        return out;
    }

    void init(int n) {
        // create all subhashy which will be needed for N
        std::lock_guard lock(set_mutex);
        for (auto s : generateShapes(n)) byshape[s].size();
        std::printf("%ld sets by shape for N=%d\n\r", byshape.size(), n);
    }

    Subhashy<32> &at(XYZ shape) {
        std::shared_lock lock(set_mutex);
        auto itr = byshape.find(shape);
        if (itr != byshape.end()) {
            return itr->second;
        }
        lock.unlock();
        // Not sure if this is supposed to happen normally
        // if init() creates all subhashys required.
        std::lock_guard elock(set_mutex);
        return byshape[shape];
    }

    template <typename CubeT>
    void insert(CubeT &&c, XYZ shape) {
        at(shape).insert(std::forward<CubeT>(c));
    }

    auto size() const {
        std::shared_lock lock(set_mutex);
        size_t sum = 0;
        DEBUG1_PRINTF("%ld maps by shape\n\r", byshape.size());
        for (auto &set : byshape) {
            auto part = set.second.size();
            DEBUG1_PRINTF("bucket [%2d %2d %2d]: %ld\n", set.first.x(), set.first.y(), set.first.z(), part);
            sum += part;
        }
        return sum;
    }

    int numShapes() const {
        std::shared_lock lock(set_mutex);
        return byshape.size();
    }

    auto begin() const { return byshape.begin(); }
    auto end() const { return byshape.end(); }
    auto begin() { return byshape.begin(); }
    auto end() { return byshape.end(); }
};
#endif
