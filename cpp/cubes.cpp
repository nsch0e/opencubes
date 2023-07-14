//#define DBG 1

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "results.hpp"
#include "structs.hpp"
#include "rotations.hpp"
#include "cache.hpp"

bool USE_CACHE = 1;

// JATothrim: changes:
// -Clean up "using namespace std;" fallout.
// -
// -Pre-allocate memory in few critical places
// -Use C++11 STL "emplace" generally to construct objects in place
// -Use move assign for Cube(s) in few critical places so no data is copied.

void expand(const Cube &c, Hashy &hashes) {
    std::unordered_set<XYZ, HashXYZ, std::equal_to<XYZ>> candidates;
    // Reserve enough memory for the set at once:
    candidates.reserve(c.size() * 6);

    for (const auto &p : c)
    {
        candidates.emplace(XYZ{p.x + 1, p.y, p.z});
        candidates.emplace(XYZ{p.x - 1, p.y, p.z});
        candidates.emplace(XYZ{p.x, p.y + 1, p.z});
        candidates.emplace(XYZ{p.x, p.y - 1, p.z});
        candidates.emplace(XYZ{p.x, p.y, p.z + 1});
        candidates.emplace(XYZ{p.x, p.y, p.z - 1});
    }
    for (const auto &p : c)
    {
        candidates.erase(p);
    }
#ifdef DBG
    std::printf("candidates: %lu\n\r", candidates.size());
#endif
    for (const auto &p : candidates)
    {
#ifdef DBG
        std::printf("(%2d %2d %2d)\n\r", p.x, p.y, p.z);
#endif
        int ax = (p.x < 0) ? 1 : 0;
        int ay = (p.y < 0) ? 1 : 0;
        int az = (p.z < 0) ? 1 : 0;
        Cube newCube;
        // Reserve memory for Cube at once:
        newCube.reserve(c.size() + 1);
        newCube.emplace_back(XYZ{p.x + ax, p.y + ay, p.z + az});
        std::array<int, 3> shape{p.x + ax, p.y + ay, p.z + az};
        for (const auto &np : c)
        {
            auto nx = np.x + ax;
            auto ny = np.y + ay;
            auto nz = np.z + az;
            if (nx > shape[0])
                shape[0] = nx;
            if (ny > shape[1])
                shape[1] = ny;
            if (nz > shape[2])
                shape[2] = nz;
            newCube.emplace_back(XYZ{nx, ny, nz});
        }
        // printf("shape %2d %2d %2d\n\r", shape[0], shape[1], shape[2]);
        // newCube.print();

        // check rotations
        Cube lowestHashCube;
        bool none_set = true;
        for (int i = 0; i < 24; ++i)
        {
            auto rotatedCube = Rotations::rotate(i, shape, newCube);
            if (rotatedCube.size() == 0)
                continue; // rotation generated violating shape

            // Note: ~18% run time spent here, use radix sort?
            std::sort(rotatedCube.begin(), rotatedCube.end());

            if (none_set || lowestHashCube < rotatedCube)
            {
                none_set = false;
                // printf("shape %2d %2d %2d\n\r", res.first.x, res.first.y, res.first.z);
                // move to avoid copy:
                lowestHashCube = std::move(rotatedCube);
            }
        }
        // move the cube into set:
        hashes.insert(std::move(lowestHashCube));
#ifdef DBG
        std::printf("=====\n\r");
        rotatedCube.print();
        std::printf("inserted! (num %2lu)\n\n\r", hashes.size());
#endif
    }
#ifdef DBG
    std::printf("new hashes: %lu\n\r", hashes.size());
#endif
}

void expandPart(std::vector<Cube> &base, Hashy &hashes, size_t start, size_t end)
{
    using namespace std::chrono;
    using std::printf;

    printf("  start from %lu to %lu\n\r", start, end);
    auto t_start = steady_clock::now();

    for (auto i = start; i < end; ++i)
    {
        expand(base[i], hashes);
        auto count = i - start;
        if (start == 0 && (count % 100 == 99))
        {
            auto t_end = steady_clock::now();
            auto dt_ms = duration_cast<milliseconds>(t_end - t_start).count();
            auto perc = 100 * count / (end - start);
            auto its = 1000.f * count / dt_ms;
            auto remaining = (end - i) / its;
            printf(" %3lu%% %5.0f baseCubes/s, remaining: %.0fs\033[0K\r", perc, its, remaining);
            std::flush(std::cout);
        }
    }
    auto t_end = steady_clock::now();
    auto dt_ms = duration_cast<milliseconds>(t_end - t_start).count();
    printf("  done from %lu to %lu: found %lu\n\r", start, end, hashes.size());
    printf("  took %.2f s\033[0K\n\r", dt_ms / 1000.f);
}

CubeSet gen(int n, int threads = 1)
{
    if (n < 1)
        return {};
    else if (n == 1)
        return {{{XYZ{0, 0, 0}}}};
    else if (n == 2)
        return {{{XYZ{0, 0, 0}, XYZ{0, 0, 1}}}};

    Hashy hashes;
    if (USE_CACHE)
    {
        hashes.set = load("cubes_" + std::to_string(n) + ".bin");

        if (hashes.size() != 0)
            return hashes.set;
    }

    using std::printf;
    using std::flush;

    auto base = gen(n - 1, threads);
    printf("N = %d || generating new cubes from %lu base cubes.\n\r", n, base.size());
    int count = 0;
    if (threads == 1 || base.size() < 100)
    {
        using namespace std::chrono;
        
        auto start = steady_clock::now();
        int total = base.size();
        for (const auto &b : base)
        {
            expand(b, hashes);
            count++;
            if (count % 100 == 99)
            {
                auto end = steady_clock::now();
                auto dt_ms = duration_cast<milliseconds>(end - start).count();
                auto perc = 100 * count / total;
                auto its = 1000.f * count / dt_ms;
                auto remaining = (total - count) / its;
                printf(" %3d%% %5.0f baseCubes/s, remaining: %.0fs\033[0K\r", perc, its, remaining);
                flush(std::cout);
            }
        }
        auto end = steady_clock::now();
        auto dt_ms = duration_cast<milliseconds>(end - start).count();
        printf("  took %.2f s\033[0K\n\r", dt_ms / 1000.f);
    }
    else
    {
        std::vector<Cube> baseCubes;
        printf("converting to vector\n\r");
        baseCubes.insert(baseCubes.end(), base.begin(), base.end());
        base.clear();
        base.reserve(1);
        printf("sorting vector\n\r");
        std::sort(baseCubes.begin(), baseCubes.end());
        printf("starting %d threads\n\r", threads);

        std::vector<std::thread> ts;
        // Threads can't really be moved once they are launched,
        ts.resize(threads);

        for (int i = 0; i < threads; ++i)
        {
            auto start = baseCubes.size() * i / threads;
            auto end = baseCubes.size() * (i + 1) / threads;

            ts[i] = std::thread(expandPart, std::ref(baseCubes), std::ref(hashes), start, end);
        }
        for (auto& thr : ts)
        {
            thr.join();
        }
    }
    printf("  num cubes: %lu\n\r", hashes.size());
    save("cubes_" + std::to_string(n) + ".bin", hashes.set);
    if (sizeof(results) / sizeof(results[0]) > (n - 1) && n > 1)
    {
        if (results[n - 1] != hashes.size())
        {
            printf("ERROR: result does not equal resultstable (%lu)!\n\r", results[n - 1]);
            exit(-1);
        }
    }
    return hashes.set;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s N [NUM_THREADS]\n\r", argv[0]);
        std::exit(-1);
    }
    int n = atoi(argv[1]);

    int threads = 1;
    if (argc > 2)
        threads = atoi(argv[2]);

    if (const char *p = getenv("USE_CACHE"))
        USE_CACHE = atoi(p);
    gen(n, threads);
    return 0;
}
