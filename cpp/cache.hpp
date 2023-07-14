#pragma once
#ifndef OPENCUBE_CACHE_HPP
#define OPENCUBE_CACHE_HPP
#include <unordered_set>
#include <fstream>
#include <string>
#include "structs.hpp"

CubeSet load(std::string path) {
    auto ifs = std::ifstream(path, std::ios::binary);
    if (!ifs.is_open())
        return {};
    uint8_t cubelen = 0;
    uint filelen = ifs.tellg();
    ifs.seekg(0, std::ios::end);
    filelen = (uint)ifs.tellg() - filelen;
    ifs.seekg(0, std::ios::beg);
    ifs.read((char *)&cubelen, 1);
    printf("loading cache file \"%s\" (%u bytes) with N = %d\n\r", path.c_str(), filelen, cubelen);

    auto cubeSize = 4 * (int)cubelen;
    auto numCubes = (filelen - 1) / cubeSize;
    if (numCubes * cubeSize + 1 != filelen)
    {
        printf("error reading file, size does not match");
        return {};
    }
    printf("  num polycubes loading: %d\n\r", numCubes);
    CubeSet cubes;
    for (size_t i = 0; i < numCubes; ++i)
    {
        Cube next;
        next.sparse.resize(cubelen);
        for (int k = 0; k < cubelen; ++k)
        {
            ifs.read((char *)&next.sparse[k].joined, 4);
        }
        cubes.insert(next);
    }
    printf("  loaded %lu cubes\n\r", cubes.size());
    return cubes;
}

void save(std::string path, CubeSet &cubes) {
    if (cubes.size() == 0)
        return;
    std::ofstream ofs(path, std::ios::binary);
    ofs << (uint8_t)cubes.begin()->sparse.size();
    for (const auto &c : cubes)
    {
        for (const auto &p : c.sparse)
        {
            ofs.write((const char *)&p.joined, sizeof(p.joined));
        }
    }
}

#endif
