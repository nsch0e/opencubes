#pragma once
#ifndef OPENCUBES_STRUCTS_HPP
#define OPENCUBES_STRUCTS_HPP
#include <cstdio>
#include <map>
#include <shared_mutex>
#include <unordered_set>
#include <vector>
#include <map>

// #define DBG 1
struct XYZ {
    union {
        struct {
            int8_t x, y, z, res;
        };
        int8_t data[4];
        int32_t joined;
    };
    explicit XYZ(int a = 0, int b = 0, int c = 0) : x(a), y(b), z(c), res(0) {}
    bool operator==(const XYZ &rhs) const { return joined == rhs.joined; }
    bool operator<(const XYZ &b) const { return joined < b.joined; }
};

struct HashXYZ {
    size_t operator()(const XYZ &p) const { return p.joined; }
};

using XYZSet = std::unordered_set<XYZ, HashXYZ, std::equal_to<XYZ>>;

struct Cube {
    std::vector<XYZ> sparse;
    /**
     * Define subset of vector operations for Cube
     * This simplifies the code everywhere else.
     */
    std::vector<XYZ>::iterator begin() { return sparse.begin(); }

    std::vector<XYZ>::iterator end() { return sparse.end(); }

    std::vector<XYZ>::const_iterator begin() const { return sparse.begin(); }

    std::vector<XYZ>::const_iterator end() const { return sparse.end(); }

    size_t size() const { return sparse.size(); }

    void reserve(size_t N) { sparse.reserve(N); }

    template <typename T>
    T &emplace_back(T &&p) {
        return sparse.emplace_back(std::forward<T>(p));
    }

    bool operator==(const Cube &rhs) const { return this->sparse == rhs.sparse; }

    bool operator<(const Cube &b) const {
        if (size() != b.size()) return size() < b.size();
        for (int i = 0; i < size(); ++i) {
            if (sparse[i].joined < b.sparse[i].joined)
                return true;
            else if (sparse[i].joined > b.sparse[i].joined)
                return false;
        }
        return false;
    }

    void print() const {
        for (auto &p : sparse) std::printf("  (%2d %2d %2d)\n\r", p.x, p.y, p.z);
    }
};

struct HashCube {
    size_t operator()(const Cube &cube) const {
        // https://stackoverflow.com/questions/20511347/a-good-hash-function-for-a-vector/72073933#72073933
        std::size_t seed = cube.size();
        for (auto &p : cube) {
            auto x = HashXYZ()(p);
            // x = ((x >> 16) ^ x) * 0x45d9f3b;
            // x = ((x >> 16) ^ x) * 0x45d9f3b;
            // x = (x >> 16) ^ x;
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

using CubeSet = std::unordered_set<Cube, HashCube, std::equal_to<Cube>>;

struct Hashy {
    struct Subhashy {
        CubeSet set;
        std::shared_mutex set_mutex;

        template <typename CubeT>
        void insert(CubeT &&c) {
            std::lock_guard lock(set_mutex);
            set.emplace(std::forward<CubeT>(c));
        }

        auto size() {
            std::shared_lock lock(set_mutex);
            return set.size();
        }
    };

    std::map<uint, Subhashy> byhash;
    uint n_subhashes = 0;
    void init(int n)
    {
        n_subhashes = (1 << n);
        for (int i = 0; i < n_subhashes; i++)
            byhash[i].size();
        printf("%ld maps by shape\n\r", byhash.size());
    }

    template <typename CubeT>
    uint hash(CubeT &&c){
        uint x = 0;
        for(const auto &p : std::forward<CubeT>(c).sparse){
            x ^= p.joined;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
        }
        return x;
    }

    template <typename CubeT>
    void insert(CubeT &&c) {
        if(n_subhashes == 0){
            n_subhashes = 1;
        }
        auto &set = byhash[hash(c) % n_subhashes];
        set.insert(std::forward<CubeT>(c));
    }

    auto size() {
        size_t sum = 0;
        for (auto &set : byhash) sum += set.second.size();
        return sum;
    }
};
#endif
