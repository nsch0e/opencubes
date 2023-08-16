#pragma once
#ifndef OPENCUBES_HASHES_HPP
#define OPENCUBES_HASHES_HPP
#include <array>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

#include "cube.hpp"
#include "cubeSwapSet.hpp"
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
    CubeStorage set_storage;
    CubeSwapSet set;
    mutable std::shared_mutex set_mutex;

   public:
    explicit Subsubhashy(std::filesystem::path path, size_t n) : set_storage(path, n), set(1, CubePtrHash(&set_storage), CubePtrEqual(&set_storage)) {}

    template <typename CubeT>
    void insert(CubeT &&c) {
        std::lock_guard lock(set_mutex);
        auto [itr, isnew] = set.emplace(set_storage.allocate(std::forward<CubeT>(c)));
        if (!isnew) {
            set_storage.cancel_allocation();
        }
    }

#if __cplusplus > 201703L
// todo: need C++17 equivalent for *generic*
// contains() or find() that accepts both Cube and CubePtr types
    bool contains(const Cube &c) const {
        std::shared_lock lock(set_mutex);
        return set.contains<Cube>(c);
    }
#endif

    auto size() const {
        std::shared_lock lock(set_mutex);
        return set.size();
    }

    void clear() {
        std::lock_guard lock(set_mutex);
        set.clear();
        set.reserve(1);
    }

    // Get CubeStorage instance.
    // [this->begin(), this->end()] iterated CubePtr's
    // Can be resolved with CubePtr::get(this->storage())
    // that returns copy of the data as Cube.
    const CubeStorage &storage() const { return set_storage; }

    auto begin() const { return set.begin(); }
    auto end() const { return set.end(); }
    auto begin() { return set.begin(); }
    auto end() { return set.end(); }
};

class Subhashy {
   protected:
    std::deque<Subsubhashy> byhash;

   public:
    Subhashy(int NUM, size_t N, std::filesystem::path path) {
        for (int i = 0; i < NUM; ++i) {
            byhash.emplace_back(path, N);
        }
    }

    template <typename CubeT>
    void insert(CubeT &&c) {
        HashCube hash;
        auto idx = hash(c) % byhash.size();
        auto &set = byhash[idx];
#if __cplusplus > 201703L
        if (set.contains(c)) return;
#endif
        set.insert(std::forward<CubeT>(c));
        // printf("new size %ld\n\r", byshape[shape].size());
    }

    void clear() {
        for (auto &set : byhash) set.clear();
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
    std::map<XYZ, Subhashy> byshape;
    std::filesystem::path base_path;
    int N;
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

    explicit Hashy(std::string path = ".") : base_path(path) {}

    void init(int n) {
        // create all subhashy which will be needed for N
        N = n;
        for (auto s : generateShapes(n)) {
            initSubHashy(n, s);
        }
        std::printf("%ld sets by shape for N=%d\n\r", byshape.size(), n);
    }

    Subhashy &initSubHashy(int n, XYZ s) {
        assert(N == n);

        auto itr = byshape.find(s);
        if (itr == byshape.end()) {
            auto [itr, isnew] = byshape.emplace(s, Subhashy(32, n, base_path));
            assert(isnew);
            itr->second.size();
            return itr->second;
        } else {
            return itr->second;
        }
    }

    Subhashy &at(XYZ shape) {
        std::shared_lock lock(set_mutex);
        auto itr = byshape.find(shape);
        if (itr != byshape.end()) {
            return itr->second;
        }
        // should never get here...
        std::printf("BUG: missing shape [%2d %2d %2d]:\n\r", shape.x(), shape.y(), shape.z());
        std::abort();
        return *((Subhashy *)0);
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
