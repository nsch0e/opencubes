#pragma once
#ifndef OPENCUBES_CUBE_HPP
#define OPENCUBES_CUBE_HPP

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "config.hpp"
#include "utils.hpp"

struct XYZ {
    int8_t data[3];
    explicit XYZ(int8_t a = 0, int8_t b = 0, int8_t c = 0) : data{a, b, c} {}
    constexpr bool operator==(const XYZ &b) const { return (uint32_t) * this == (uint32_t)b; }
    constexpr bool operator<(const XYZ &b) const { return (uint32_t) * this < (uint32_t)b; }
    constexpr operator uint32_t() const { return ((uint8_t)data[0] << 16) | ((uint8_t)data[1] << 8) | ((uint8_t)data[2]); }

    constexpr int8_t &x() { return data[0]; }
    constexpr int8_t &y() { return data[1]; }
    constexpr int8_t &z() { return data[2]; }
    constexpr int8_t x() const { return data[0]; }
    constexpr int8_t y() const { return data[1]; }
    constexpr int8_t z() const { return data[2]; }
    constexpr int8_t &operator[](int offset) { return data[offset]; }
    constexpr int8_t operator[](int offset) const { return data[offset]; }
    friend XYZ operator+(const XYZ &a, const XYZ &b) {
        XYZ ret = a;
        ret += b;
        return ret;
    }
    void operator+=(const XYZ &b) {
        data[0] += b.data[0];
        data[1] += b.data[1];
        data[2] += b.data[2];
    }
};

struct HashXYZ {
    size_t operator()(const XYZ &p) const { return (uint32_t)p; }
};

using XYZSet = std::unordered_set<XYZ, HashXYZ, std::equal_to<XYZ>>;

struct Cube {
   private:
    // cube memory is stored two ways:
    // normal, new'd buffer: is_shared == false
    // shared, external memory: is_shared == true
#if CUBES_PACK_CUBE_XYZ_ADDR == 1
    struct bits_t {
        uint64_t is_shared : 1;
        uint64_t size : 7;   // MAX 127
        uint64_t addr : 56;  // low 56-bits of memory address.
    };
    static_assert(sizeof(bits_t) == sizeof(void *));
#else
    struct bits_t {
        uint64_t addr;
        uint8_t is_shared : 1;
        uint8_t size : 7;  // MAX 127
    };
#endif
    // fields
    bits_t fields;
    // extract the pointer from bits_t
    static XYZ *get(bits_t key) {
        // pointer bit-hacking:
        uint64_t addr = key.addr;
#if CUBES_PACK_CUBE_XYZ_ADDR == 1
// todo: on x86-64 depending if 5-level-paging is enabled
// either 47-bit or 56-bit should be replicated to the high
// part of the address. Don't know how to do this check yet,
// so the high 8-bits is left zeroed.
// If we get segfaults dereferencing get(fields)
// then CUBES_PACK_CUBE_XYZ_ADDR must be disabled.
#endif
        return reinterpret_cast<XYZ *>(addr);
    }

    static bits_t put(bool is_shared, int size, XYZ *addr) {
#if CUBES_PACK_CUBE_XYZ_ADDR == 1
        // pack the memory address into 56-bits
        // on x86-64 it is not used by the hardware (yet).
        // This hack actually saves 8 bytes because previously
        // the uint8_t caused padding to 16 bytes.
        uint64_t tmp = reinterpret_cast<uint64_t>((void *)addr);
        assert((tmp & ~0xffffffffffffff) == 0 && "BUG: CUBES_PACK_CUBE_XYZ_ADDR should be disabled");
        tmp &= 0xffffffffffffff;
        bits_t bits;
        bits.addr = tmp;
        bits.is_shared = is_shared;
        bits.size = size;
        return bits;
#else
        bits_t bits;
        bits.addr = reinterpret_cast<uint64_t>((void *)addr);
        bits.is_shared = is_shared;
        bits.size = size;
        return bits;
#endif
    }

   public:
    // Empty cube
    Cube() : fields{put(0, 0, nullptr)} {}

    // Cube with N capacity
    explicit Cube(uint8_t N) : fields{put(0, N, new XYZ[N])} {}

    // Construct from pieces
    Cube(std::initializer_list<XYZ> il) : Cube(il.size()) { std::copy(il.begin(), il.end(), begin()); }

    // Construct from range.
    Cube(const XYZ *start, const XYZ *end) : Cube(std::distance(start, end)) { std::copy(start, end, begin()); }

    // Construct from external source.
    // Cube shares this the memory until modified.
    // Caller guarantees the memory given will live longer than *this
    Cube(const XYZ *start, uint8_t n) : fields{put(1, n, const_cast<XYZ *>(start))} {}

    // Copy ctor.
    Cube(const Cube &copy) : Cube(copy.size()) { std::copy(copy.begin(), copy.end(), begin()); }

    ~Cube() {
        bits_t bits = fields;
        if (!bits.is_shared) {
            delete[] get(bits);
        }
    }
    friend void swap(Cube &a, Cube &b) {
        using std::swap;
        bits_t abits = a.fields;
        bits_t bbits = b.fields;
        a.fields = bbits;
        b.fields = abits;
    }

    Cube(Cube &&mv) : Cube() { swap(*this, mv); }

    Cube &operator=(const Cube &copy) {
        Cube tmp(copy);
        swap(*this, tmp);
        return *this;
    }

    Cube &operator=(Cube &&mv) {
        swap(*this, mv);
        return *this;
    }

    size_t size() const { return fields.size; }

    XYZ *data() { return get(fields); }

    const XYZ *data() const { return get(fields); }

    XYZ *begin() { return data(); }

    XYZ *end() { return data() + size(); }

    const XYZ *begin() const { return data(); }

    const XYZ *end() const { return data() + size(); }

    bool operator==(const Cube &rhs) const {
        if (size() != rhs.size()) return false;
        return std::mismatch(begin(), end(), rhs.begin()).first == end();
    }

    bool operator<(const Cube &b) const {
        if (size() != b.size()) return size() < b.size();
        auto [aa, bb] = std::mismatch(begin(), end(), b.begin());
        if (aa == end()) {
            return false;
        } else {
            return *aa < *bb;
        }
    }

    void print() const {
        for (auto &p : *this) std::printf("  (%2d %2d %2d)\n\r", p.x(), p.y(), p.z());
    }

    /**
     * Copy cube data into destination buffer.
     */
    void copyout(int num, XYZ *dest) const {
        assert(num <= (signed)size());
        std::copy_n(begin(), num, dest);
    }
};

#if CUBES_PACK_CUBE_XYZ_ADDR == 1
static_assert(sizeof(Cube) == 8, "Unexpected sizeof(Cube) for Cube");
#endif
static_assert(std::is_move_assignable_v<Cube>, "Cube must be moveable");
static_assert(std::is_swappable_v<Cube>, "Cube must swappable");

#endif
