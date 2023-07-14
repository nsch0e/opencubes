#pragma once
#include <vector>
#include <unordered_set>
#include <shared_mutex>

using namespace std;
// #define DBG 1

template<int Offset>
struct XYZ_proxy {
    uint32_t &value;

    constexpr operator uint8_t() const { return (value >> Offset) & 0xff; }
    constexpr XYZ_proxy &operator=(uint8_t v) { value = (value & ~(0xff << Offset)) | (v << Offset); return *this; }
};

template<int Offset>
struct XYZ_proxy_const {
    const uint32_t &value;

    constexpr operator uint8_t() const { return (value >> Offset) & 0xff; }
};

struct XYZ
{
    uint32_t value;

    explicit constexpr XYZ() : value(0) {}
    explicit constexpr XYZ(auto a, auto b, auto c) : value(((uint8_t)a << 0) | ((uint8_t)b << 8) | ((uint8_t)c << 16)) {}

    constexpr auto operator<=>(const XYZ &rhs) const = default;

    constexpr uint8_t operator[](int Offset) const { return (value >> (8 * Offset)) & 0xff; }

    constexpr XYZ_proxy<0> x() { return {value}; }
    constexpr XYZ_proxy<8> y() { return {value}; }
    constexpr XYZ_proxy<16> z() { return {value}; }

    constexpr XYZ_proxy_const<0> x() const { return {value}; }
    constexpr XYZ_proxy_const<8> y() const { return {value}; }
    constexpr XYZ_proxy_const<16> z() const { return {value}; }
};

struct Cube
{
    vector<XYZ> sparse;
    bool operator==(const Cube &rhs) const { return this->sparse == rhs.sparse; }
    bool operator<(const Cube &b) const
    {
        if (sparse.size() != b.sparse.size())
            return sparse.size() < b.sparse.size();
        for (std::size_t i = 0; i < sparse.size(); ++i)
        {
            if (sparse[i] < b.sparse[i])
                return true;
            else if (sparse[i] > b.sparse[i])
                return false;
        }
        return false;
    }

    void print()
    {
        for (auto &p : sparse)
            printf("  (%2d %2d %2d)\n\r", (int)p.x(), (int)p.y(), (int)p.z());
    }
};

namespace std
{
    template <>
    struct hash<XYZ>
    {
        size_t operator()(const XYZ &x) const { return x.value; }
    };

    template <>
    struct hash<Cube>
    {
        size_t operator()(const Cube &cube) const
        {
            // https://stackoverflow.com/questions/20511347/a-good-hash-function-for-a-vector/72073933#72073933
            std::size_t seed = cube.sparse.size();
            for (auto &p : cube.sparse)
            {
                auto x = std::hash<XYZ>()(p);
                // x = ((x >> 16) ^ x) * 0x45d9f3b;
                // x = ((x >> 16) ^ x) * 0x45d9f3b;
                // x = (x >> 16) ^ x;
                seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
} // namespace std

struct Hashy
{
    unordered_set<Cube> set;

    shared_mutex set_mutex;
    void insert(const Cube &c)
    {
        lock_guard lock(set_mutex);
        set.insert(c);
    }
    auto size()
    {
        shared_lock lock(set_mutex);
        return set.size();
    }
};