#include "cubes.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#include "cache.hpp"
#include "cube.hpp"
#include "hashes.hpp"
#include "newCache.hpp"
#include "results.hpp"
#include "rotations.hpp"

const int PERF_STEP = 500;

struct Workset {
    std::mutex mu;
    XYZ shape;
    int n;
    Workset(XYZ shape, int n) : shape(shape), n(n) {}

    XYZ fromId(uint64_t id) { return XYZ(id / ((shape.y() + 1) * (shape.z() + 1)), (id / (shape.z() + 1)) % (shape.y() + 1), id % (shape.z() + 1)); }

    uint64_t recurse(Cube &c, uint8_t step = 0, uint8_t bounds = 0, uint64_t pos = 0) {
        // std::printf("step %d\n", step);
        // c.print();
        if (step == n) {
            if (bounds != 0x3f) return 0;
            // check rotations
            Cube rotatedCube(n);
            bool none_set = true;
            for (int i = 0; i < 24; ++i) {
                auto [res, ok] = Rotations::rotate(i, shape, c, rotatedCube);
                if (!ok) continue;  // rotation generated violating shape

                std::sort(rotatedCube.begin(), rotatedCube.end());

                if (rotatedCube < c) {
                    // std::printf("not smallest rotation\n");
                    // rotatedCube.print();
                    // std::printf("c:\n");
                    // c.print();
                    return 0;
                }
            }
            auto [noerror, compr] = CompressedCube::encode(c);
            if (noerror) {
                // std::printf("new cube\n");
                // c.print();
                return 1;
            }
            return 0;
        }
        uint64_t sum = 0;
        uint64_t max = ((shape.x() + 1) * (shape.y() + 1) * (shape.z() + 1));
        if (step <= shape.x()) {
            max = (step + 1) * (shape.y() + 1) * (shape.z() + 1);
        }
        for (uint64_t id = pos; id < max; ++id) {
            auto p = fromId(id);
            auto newbounds = bounds;
            if (p.x() == shape.x()) newbounds |= 1 << 0;
            if (p.x() == 0) newbounds |= 1 << 1;

            if (p.y() == shape.y()) newbounds |= 1 << 2;
            if (p.y() == 0) newbounds |= 1 << 3;

            if (p.z() == shape.z()) newbounds |= 1 << 4;
            if (p.z() == 0) newbounds |= 1 << 5;

            c.data()[step] = p;
            sum += recurse(c, step + 1, newbounds, id + 1);
        }
        return sum;
    }
};

struct Worker {
    Workset &ws;
    int id;
    Worker(Workset &ws_, int id_) : ws(ws_), id(id_) {}
    void run() {
        // std::printf("start %d\n", id);
        Cube c(ws.n);
        auto num = ws.recurse(c);
        std::printf("rec num %ld\n", num);
        // std::printf("finished %d\n", id);
    }
};

void gen(int n, int threads, bool use_cache, bool write_cache, bool split_cache, bool use_split_cache) {
    std::printf("N = %d.\n\r", n);
    uint64_t totalSum = 0;
    auto start = std::chrono::steady_clock::now();
    uint32_t outShapeCount = 0;
    auto shapes = Hashy::generateShapes(n);
    uint32_t totalOutputShapes = shapes.size();
    for (auto &targetShape : shapes) {
        outShapeCount++;
        std::printf("process output shape %3d/%d [%2d %2d %2d]\n\r", outShapeCount, totalOutputShapes, targetShape.x(), targetShape.y(), targetShape.z());

        // std::printf("starting %d threads\n\r", threads);
        Workset ws(targetShape, n);
        /*
                std::vector<std::thread> ts;
                std::vector<Worker> workers;
                ts.reserve(threads);
                workers.reserve(threads);
                for (int i = 0; i < threads; ++i) {
                    workers.emplace_back(ws, i);
                    ts.emplace_back(&Worker::run, std::ref(workers[i]));
                }
                for (int i = 0; i < threads; ++i) {
                    ts[i].join();
                }
        */

        Cube c(ws.n);
        uint64_t num = 0;
        num = ws.recurse(c);
        std::printf("  num: %lu\n\r", num);
        totalSum += num;
    }
    auto end = std::chrono::steady_clock::now();
    auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::printf("took %.2f s\033[0K\n\r", dt_ms / 1000.f);
    std::printf("num total cubes: %lu\n\r", totalSum);
    checkResult(n, totalSum);
}
