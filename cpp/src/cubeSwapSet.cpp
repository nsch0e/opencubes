#include "cubeSwapSet.hpp"

#include <filesystem>

std::atomic<int> CubeStorage::m_init_num(0);

CubeStorage::CubeStorage(std::filesystem::path path, size_t n) : m_cube_size(n) {
    // Generate file name:
    m_fpath = path / ("storage_" + std::to_string(m_init_num.fetch_add(1)) + ".bin");
}

CubeStorage::~CubeStorage() { discard(); }

CubeStorage::CubeStorage(CubeStorage&& mv)
    : m_fpath(std::move(mv.m_fpath)), m_file(std::move(mv.m_file)), m_map(std::move(mv.m_map)), m_cube_size(mv.m_cube_size), m_alloc_seek(mv.m_alloc_seek) {
    // no allocations can exist in the moved from object:
    assert(m_alloc_seek == 0);
}

CubePtr CubeStorage::allocate(const Cube& cube) {
    std::lock_guard lock(m_mtx);

    if (!m_file) {
        using namespace mapped;
        // file not open yet.
        m_file = std::make_shared<file>();
        if (m_file->openrw(m_fpath.c_str(), 0, file::CREATE | file::RESIZE | file::FSTUNE)) {
            std::printf("CubeStorage::allocate() ERROR: Failed to create backing file: %s\n", m_fpath.c_str());
            std::abort();
        }
        // Map some data.
        // todo: mapped::file could provide following:
        // m_file->readAt(offset,size,datain)
        // m_file->writeAt(offset,size,dataout)
        // so that we don't need this mapping for I/O.
        // However the mapped::region::readAt() will be faster if
        // the area fits in the region window and is accessed multiple times.
        m_map = std::make_unique<region>(m_file, 0, PAGE_SIZE);
    }

    if (m_cube_size != cube.size()) {
        std::printf("CubeStorage::allocate() ERROR: Cube size different than initialized");
        std::abort();
    }

    m_map->writeAt(m_alloc_seek, m_cube_size * sizeof(XYZ), cube.data());

    auto fpos = m_alloc_seek;
    m_prev_seek = m_alloc_seek;
    m_alloc_seek += m_cube_size * sizeof(XYZ);

    return CubePtr(fpos);
}

void CubeStorage::cancel_allocation() {
    std::lock_guard lock(m_mtx);
    // last allocation was mistake.
    if (m_alloc_seek >= m_cube_size * sizeof(XYZ)) m_alloc_seek -= m_cube_size * sizeof(XYZ);

    // allocate() -> cancel_allocation() must be serialized:
    assert(m_alloc_seek == m_prev_seek);
}

Cube CubeStorage::read(const CubePtr& x) const {
    // todo: How to speed up:
    // Option 1:
    // Memory-map the file in 2 MiB aligned chunks:
    // This would speed up reading the same data multiple times.
    // Chunk is mapped by rounding down the x.seek() to multiple of 2MiB
    // and creating 2MiB sized mapping at that file offset.
    // Caching the last file offset used we could detect
    // when we have do do jump() to the next "reading window".
    // -Plus: let the kernel do the caching for us.
    // -Plus: no memory overhead.
    // -Minus: if implemented with just single memory-map per CubeStorage
    //         threads can fight about what chunk is currently mapped.
    // Option 2:
    // Implement fine-grained read-cache with:
    // std::unordered_map<fileoffset, Cube>
    // And begin evicting them once the cache is full using
    // cache eviction policy. (E.g. least-recently-used LRU)
    // The cache should be made to be thread local
    // so it won't interfere with other workers.
    // -Plus: We decide how much data to keep in memory
    // -Plus: No need to remap the memory.
    // -Minus: complicated to implement.
    Cube tmp(m_cube_size);
    m_map->readAt(x.seek(), m_cube_size * sizeof(XYZ), tmp.data());
    return tmp;
}

void CubeStorage::discard() {
    std::lock_guard lock(m_mtx);

    if (m_file) {
        // avoid flushing any more data to disk:
        m_map->discard(0, m_map->regionSize());
        m_map.reset();
        m_file->truncate(0);
        m_file.reset();
        m_alloc_seek = 0;

        // Try remove the file created...
        std::error_code ec;
        auto stat = std::filesystem::status(m_fpath, ec);
        if (!ec && std::filesystem::is_regular_file(stat)) {
            if (!std::filesystem::remove(m_fpath, ec)) {
                std::printf("WARN: failed to remove file: %s", m_fpath.c_str());
            }
        } else {
            std::printf("WARN: failed to get file status: %s", m_fpath.c_str());
        }
    }
}

Cube CubePtr::get(const CubeStorage& storage) const {
    // CubePtr::get() is really just an convenience function...
    // However this cannot be implemented in the header file because
    // CubeStorage definition is not known.
    return storage.read(*this);
}
