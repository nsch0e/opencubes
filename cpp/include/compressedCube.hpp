#pragma once
#ifndef OPENCUBES_COMPRESSEDCUBE_HPP
#define OPENCUBES_COMPRESSEDCUBE_HPP
#include <array>
#include <cstdio>

#include "cube.hpp"
class HashCompressedCube;
struct CompressedCube {
    friend HashCompressedCube;

    static constexpr uint8_t NUM_DIRS = 6;
    static constexpr int8_t dirs[NUM_DIRS][3] = {
        {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0},

    };

    std::array<uint8_t, sizeof(size_t) * 3> enc;
    uint8_t& encodedLen() { return enc[0]; };
    const uint8_t& encodedLen() const { return enc[0]; };
    uint8_t* begin() { return enc.data() + 1; };
    uint8_t* end() { return enc.data() + 1 + enc[0]; };
    const uint8_t* begin() const { return enc.data() + 1; };
    const uint8_t* end() const { return enc.data() + 1 + enc[0]; };

    bool operator==(const CompressedCube& rhs) const {
        size_t* ptrA = (size_t*)enc.data();
        size_t* ptrB = (size_t*)rhs.enc.data();
        for (uint64_t i = 0; i < sizeof(enc) / sizeof(size_t); ++i) {
            if (ptrA[i] != ptrB[i]) return false;
        }
        return true;
    }

    static CompressedCube encode(const Cube& cube) {
        CompressedCube out;
        std::fill(out.enc.begin(), out.enc.end(), 0x88);
        out.encodedLen() = 0;
        uint8_t nibbles = 0;
        uint32_t nibbleCount = 0;
        auto put = out.enc.data();
        auto push = [&nibbles, &nibbleCount, &out, &put](uint8_t inst) {
            if (nibbleCount & 1) {
                nibbles <<= 4;
                nibbles |= 0xf & inst;
                *put++ = (nibbles);
                out.encodedLen()++;
            } else {
                nibbles = 0xf & inst;
            }
            nibbleCount += 1;
        };

        std::vector<XYZ> left(cube.size());
        {
            auto put = left.begin();
            for (auto& c : cube) *put++ = c;
        }
        XYZ last = left[0];
        std::vector<XYZ> done = {last};
        left.erase(left.begin());
        done.reserve(cube.size());
        while (left.size() > 0) {
            bool found = false;
            for (uint32_t i = 0; i < NUM_DIRS; ++i) {
                auto next = last + XYZ(dirs[i][0], dirs[i][1], dirs[i][2]);
                auto pos = std::find(left.begin(), left.end(), next);
                if (pos != left.end()) {
                    left.erase(pos);
                    done.push_back(next);
                    push(i);
                    last = next;
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (auto jumpcand_it = left.begin(); jumpcand_it < left.end(); jumpcand_it++) {
                    for (uint32_t i = 0; i < NUM_DIRS; ++i) {
                        auto jumpcand = *jumpcand_it;
                        auto refcand = jumpcand + XYZ(dirs[i][0], dirs[i][1], dirs[i][2]);
                        for (uint32_t j = 0; j < done.size(); ++j) {
                            if (done[j] == refcand) {
                                // std::printf("jump to [%d %d %d]\n", refcand.x(), refcand.y(), refcand.z());
                                uint32_t revidx = done.size() - j - 1;
                                left.erase(jumpcand_it);
                                done.push_back(jumpcand);
                                last = jumpcand;
                                if (revidx >= 0x8) {
                                    auto tmp = (revidx >> 3) & 0x7;
                                    revidx &= 0x7;
                                    push(0x8 | tmp);
                                }
                                push(0x8 | revidx);
                                push(i ^ 1);
                                found = true;
                                break;
                            }
                        }
                        if (found) break;
                    }
                }
                if (!found) {
                    printf("ERROR unconnected\n");
                    cube.print();
                    break;
                }
            }
        }
        if (nibbleCount % 2) {
            push(0x8);  // dummy jump
        }
        return out;
    }

    Cube decode(uint8_t sizehint, XYZ start = XYZ(0, 0, 0)) const {
        // std::printf("%d\n", size);
        Cube cube(sizehint);
        const uint32_t noJump = 0x80000000;
        uint32_t jumpoff = noJump;
        XYZ last = start;
        auto put = cube.begin();
        *put++ = last;
        uint32_t currSize = 1;
        for (uint32_t i = 0; i < encodedLen() * 2; ++i) {
            uint8_t inst = enc[i >> 1];
            // get lower or upper nibble
            if (i & 1)
                inst &= 0xf;
            else
                inst >>= 4;

            // process nibble instruction
            if (inst & 0x8) {  // jump
                jumpoff <<= 3;
                jumpoff |= inst & 0x7;
            } else {
                if (jumpoff != noJump) {
                    // std::printf("jump %u\n", jumpoff);
                    last = cube.data()[currSize - 1 - jumpoff];
                    // std::printf("jump %u [%2d %2d %2d]\n", jumpoff, last.x(), last.y(), last.z());
                    jumpoff = noJump;
                }
                last += XYZ(dirs[inst][0], dirs[inst][1], dirs[inst][2]);
                // std::printf("dir %d, insert [%2d %2d %2d]\n", inst, last.x(), last.y(), last.z());
                *put++ = last;
                currSize++;
                if (currSize == sizehint) {
                    // std::printf("break");
                    break;
                }
            }
        }
        std::sort(cube.begin(), cube.end());
        return cube;
    }
    void print() {
        for (auto c : enc) std::printf("%02x ", c);
        std::printf("\n");
    }
};

#endif