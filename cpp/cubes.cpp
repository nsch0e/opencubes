// #define DBG 1

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "cache.hpp"
#include "results.hpp"
#include "rotations.hpp"
#include "structs.hpp"

const int PERF_STEP = 500;

bool USE_CACHE = 1;
bool WRITE_CACHE = 1;

void expand(const Cube &c, Hashy &hashes) {
    XYZSet candidates;
    candidates.reserve(c.size() * 6);
    for (const auto &p : c) {
        candidates.emplace(XYZ(p.x() + 1, p.y(), p.z()));
        candidates.emplace(XYZ(p.x() - 1, p.y(), p.z()));
        candidates.emplace(XYZ(p.x(), p.y() + 1, p.z()));
        candidates.emplace(XYZ(p.x(), p.y() - 1, p.z()));
        candidates.emplace(XYZ(p.x(), p.y(), p.z() + 1));
        candidates.emplace(XYZ(p.x(), p.y(), p.z() - 1));
    }
    for (const auto &p : c) {
        candidates.erase(p);
    }
#ifdef DBG
    std::printf("candidates: %lu\n\r", candidates.size());
#endif
    for (const auto &p : candidates) {
#ifdef DBG
        std::printf("(%2d %2d %2d)\n\r", p.x(), p.y(), p.z());
#endif
        int ax = (p.x() < 0) ? 1 : 0;
        int ay = (p.y() < 0) ? 1 : 0;
        int az = (p.z() < 0) ? 1 : 0;
        Cube newCube;
        newCube.reserve(c.size() + 1);
        newCube.emplace_back(XYZ(p.x() + ax, p.y() + ay, p.z() + az));
        XYZ shape(p.x() + ax, p.y() + ay, p.z() + az);
        for (const auto &np : c) {
            auto nx = np.x() + ax;
            auto ny = np.y() + ay;
            auto nz = np.z() + az;
            if (nx > shape[0]) shape[0] = nx;
            if (ny > shape[1]) shape[1] = ny;
            if (nz > shape[2]) shape[2] = nz;
            newCube.emplace_back(XYZ(nx, ny, nz));
        }
        // std::printf("shape %2d %2d %2d\n\r", shape[0], shape[1], shape[2]);
        // newCube.print();

        // check rotations
        Cube lowestHashCube;
        XYZ lowestShape;
        bool none_set = true;
        for (int i = 0; i < 24; ++i) {
            auto res = Rotations::rotate(i, shape, newCube);
            if (res.second.size() == 0) continue;  // rotation generated violating shape
            Cube rotatedCube{std::move(res.second)};
            std::sort(rotatedCube.begin(), rotatedCube.end());

            if (none_set || lowestHashCube < rotatedCube) {
                none_set = false;
                // std::printf("shape %2d %2d %2d\n\r", res.first.x(), res.first.y(), res.first.z());
                lowestHashCube = std::move(rotatedCube);
                lowestShape = res.first;
            }
        }
        hashes.insert(std::move(lowestHashCube), lowestShape);
#ifdef DBG
        std::printf("=====\n\r");
        // lowestHashCube.print();
        std::printf("inserted! (num %2lu)\n\n\r", hashes.size());
#endif
    }
#ifdef DBG
    std::printf("new hashes: %lu\n\r", hashes.size());
#endif
}

void expandPart(std::vector<Cube> &base, Hashy &hashes, size_t start, size_t end) {
    auto t_start = std::chrono::steady_clock::now();
    auto t_last = t_start;
    auto total = end - start;
    for (auto i = start; i < end; ++i) {
        expand(base[i], hashes);
        auto count = i - start;
        if (start == 0 && (count % PERF_STEP == (PERF_STEP - 1))) {
            auto t_end = std::chrono::steady_clock::now();
            auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
            auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_last).count();
            t_last = t_end;
            auto perc = 100 * count / total;
            auto avg = 1000000.f * count / total_us;
            auto its = 1000000.f * PERF_STEP / dt_us;
            auto remaining = (end - i) / avg;
            std::printf(" %3ld%%, %5.0f avg baseCubes/s, %5.0f baseCubes/s, remaining: %.0fs\033[0K\r", perc, avg, its, remaining);
            std::flush(std::cout);
        }
    }
    auto t_end = std::chrono::steady_clock::now();
    auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    std::printf("  done took %.2f s [%7lu, %7lu]\033[0K\n\r", dt_ms / 1000.f, start, end);
}

Hashy gen(uint n, int threads = 1) {
    Hashy hashes;
    if (n < 1)
        return hashes;
    else if (n == 1) {
        hashes.insert(Cube{{XYZ(0, 0, 0)}}, XYZ(0, 0, 0));
        std::printf("%ld elements for %d\n\r", hashes.size(), n);
        return hashes;
    } else if (n == 2) {
        hashes.insert(Cube{{XYZ(0, 0, 0), XYZ(0, 0, 1)}}, XYZ(0, 0, 1));
        std::printf("%ld elements for %d\n\r", hashes.size(), n);
        return hashes;
    }

    if (USE_CACHE) {
        hashes = load("cubes_" + std::to_string(n) + ".bin");

        if (hashes.size() != 0) return hashes;
    }

    auto base = gen(n - 1, threads);
    std::printf("N = %d || generating new cubes from %lu base cubes.\n\r", n, base.size());
    hashes.init(n);
    uint64_t totalsum = 0;
    for (const auto &targethash : hashes.byshape) {
        hashes.select(targethash.first);
        int count = 0;
        if (threads == 1 || base.size() < 100) {
            auto start = std::chrono::steady_clock::now();
            auto last = start;
            int total = base.size();

            for (const auto &s : base.byshape) {
                // std::printf("shapes %d %d %d\n\r", s.first.x(), s.first.y(), s.first.z());
                auto &shape = s.first;
                int diffx = hashes.target.x() - shape.x();
                int diffy = hashes.target.y() - shape.y();
                int diffz = hashes.target.z() - shape.z();
                int abssum = abs(diffx) + abs(diffy) + abs(diffz);
                if (abssum > 1 || diffx < 0 || diffy < 0 || diffz < 0) {
                    continue;
                }

                for (const auto &b : s.second.set) {
                    expand(b, hashes);
                    count++;
                    if (count % PERF_STEP == (PERF_STEP - 1)) {
                        auto end = std::chrono::steady_clock::now();
                        auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                        auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(end - last).count();
                        last = end;
                        auto perc = 100 * count / total;
                        auto avg = 1000000.f * count / total_us;
                        auto its = 1000000.f * PERF_STEP / dt_us;
                        auto remaining = (total - count) / avg;
                        std::printf(" %3d%%, %5.0f avg baseCubes/s, %5.0f baseCubes/s, remaining: %.0fs\033[0K\r", perc, avg, its, remaining);
                        std::flush(std::cout);
                    }
                }
            }
            auto end = std::chrono::steady_clock::now();
            auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::printf("  took %.2f s\033[0K\n\r", dt_ms / 1000.f);
        } else {
            std::vector<Cube> baseCubes;
            std::printf("converting to vector\n\r");
            for (auto &s : base.byshape) {
                auto &shape = s.first;
                int diffx = hashes.target.x() - shape.x();
                int diffy = hashes.target.y() - shape.y();
                int diffz = hashes.target.z() - shape.z();
                int abssum = abs(diffx) + abs(diffy) + abs(diffz);
                if (abssum > 1 || diffx < 0 || diffy < 0 || diffz < 0) {
                    continue;
                }
                baseCubes.insert(baseCubes.end(), s.second.set.begin(), s.second.set.end());
                // s.second.set.clear();
                // s.second.set.reserve(1);
            }
            std::printf("starting %d threads\n\r", threads);
            std::vector<std::thread> ts;
            ts.reserve(threads);
            for (int i = 0; i < threads; ++i) {
                auto start = baseCubes.size() * i / threads;
                auto end = baseCubes.size() * (i + 1) / threads;

                ts.emplace_back(expandPart, std::ref(baseCubes), std::ref(hashes), start, end);
            }
            for (int i = 0; i < threads; ++i) {
                ts[i].join();
            }
        }
        auto &bucket = hashes.byshape[targethash.first];
        std::printf("[%2d %2d %2d]  num cubes: %lu\n\r", targethash.first.x(), targethash.first.y(), targethash.first.z(), bucket.size());
        if (WRITE_CACHE) save("cubes_" + std::to_string(n) + ".bin", hashes, n);
        bucket.set.clear();
        bucket.set.reserve(1);
    }

    std::printf("  num cubes: %lu\n\r", totalsum);
    if (sizeof(results) / sizeof(results[0]) > (n - 1) && n > 1) {
        if (results[n - 1] != totalsum) {
            std::printf("ERROR: result does not equal resultstable (%lu)!\n\r", results[n - 1]);
            std::exit(-1);
        }
    }
    return hashes;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::printf("usage: %s N [NUM_THREADS]\n\r", argv[0]);
        std::exit(-1);
    }
    int n = atoi(argv[1]);

    int threads = 1;
    if (argc > 2) threads = atoi(argv[2]);

    if (const char *p = getenv("USE_CACHE")) USE_CACHE = atoi(p);
    if (const char *p = getenv("WRITE_CACHE")) WRITE_CACHE = atoi(p);
    gen(n, threads);
    return 0;
}
