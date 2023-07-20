#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <vector>

#include "compressedCube.hpp"
#include "cube.hpp"
#include "newCache.hpp"

int main(int argc, const char** argv) {
    if (argc < 2) return -1;
    std::printf("Start\n");
    CacheReader cr(argv[1]);
    cr.printHeader();
    uint64_t counter = 0;
    for (auto& cv : cr) {
        counter += 1;
        if ((counter % 10000) == 0) {
            std::printf("%8ld\r", counter);
            std::flush(std::cout);
        }
        // cv.print();
        Cube c(cv);
        auto enc = CompressedCube::encode(c);
        auto cout = enc.decode(c.size(), c.data()[0]);
        if (!(c == cout)) {
            enc.print();
            std::printf("ERROR!\n");
            c.print();
            std::printf("-- VS --\n");
            cout.print();
            return -1;
        }
    }
    std::printf("%8ld\n", counter);

    return 0;
}