#pragma once
#ifndef OPENCUBES_CUBE_DISKSWAP_SET_HPP
#define OPENCUBES_CUBE_DISKSWAP_SET_HPP

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#include "cube.hpp"
#include "mapped_file.hpp"

/**
 * Implement std::unordered_set<> that stores element data in a file.
 *
 * Cubes stored with size N in the set have constant cost of RAM memory:
 * Only the std::unordered_set<> itself and the internals nodes are stored in RAM.
 * The element *data* (i.e. XYZ data) is stored in the file.
 * The performance cost is that each time the element is accessed
 * the data has to be read back from the file.
 * (Iterating the entire CubeSwapSet involves reading the entire backing file)
 *
 * Clearing the CubeSwapSet does not release the backing file space managed by CubeStorage.
 * Call to CubeStorage::discard() is required after clearing or destructing
 * the CubeSwapSet instance to cleanup the file.
 * Elements cannot be removed one-by-one.
 */
class CubeStorage;

/**
 * Overlay that reads the cube data from the backing file.
 * CubePtr needs its associated CubeStorage instance to be able to
 * access its contents with CubePtr::get()
 * The associated CubeStorage owning the CubePtr
 * should always be available where CubePtr is used.
 */
class CubePtr {
   protected:
    mapped::seekoff_t m_seek = 0;

   public:
    explicit CubePtr(mapped::seekoff_t offset) : m_seek(offset) {}
    CubePtr(const CubePtr& c) : m_seek(c.m_seek) {}

    /**
     * Get the Cube pointed by this instance.
     */
    Cube get(const CubeStorage& storage) const;

    template <typename Itr>
    void copyout(const CubeStorage& storage, size_t n, Itr out) const {
        auto tmp = get(storage);
        std::copy_n(tmp.begin(), n, out);
    }

    mapped::seekoff_t seek() const { return m_seek; }
};

/**
 * Stateful comparator for Cubeptr
 */
class CubePtrEqual {
   protected:
    const CubeStorage* m_storage = nullptr;
   public:
	// C++20 feature:
    using is_transparent = void;

    CubePtrEqual(const CubeStorage* ctx) : m_storage(ctx) {}
    CubePtrEqual(const CubePtrEqual& ctx) : m_storage(ctx.m_storage) {}

    bool operator()(const CubePtr& a, const CubePtr& b) const { return a.get(*m_storage) == b.get(*m_storage); }

    bool operator()(const Cube& a, const CubePtr& b) const { return a == b.get(*m_storage); }

    bool operator()(const CubePtr& a, const Cube& b) const { return a.get(*m_storage) == b; }
};

class CubePtrHash {
   protected:
    const CubeStorage* m_storage = nullptr;
   public:
	// C++20 feature:
    using is_transparent = void;
    using transparent_key_equal = CubePtrEqual;

    CubePtrHash(const CubeStorage* ctx) : m_storage(ctx) {}
    CubePtrHash(const CubePtrHash& ctx) : m_storage(ctx.m_storage) {}

    size_t operator()(const Cube& x) const {
        std::size_t seed = x.size();
        for (auto& p : x) {
            auto x = HashXYZ()(p);
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }

    size_t operator()(const CubePtr& x) const {
        auto cube = x.get(*m_storage);
        std::size_t seed = cube.size();
        for (auto& p : cube) {
            auto x = HashXYZ()(p);
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

class CubeStorage {
   protected:
    std::mutex m_mtx;
    std::filesystem::path m_fpath;
    std::shared_ptr<mapped::file> m_file;
    std::unique_ptr<mapped::region> m_map;

    static std::atomic<int> m_init_num;
    const size_t m_cube_size;
    mapped::seekoff_t m_prev_seek = 0;
    mapped::seekoff_t m_alloc_seek = 0;

   public:
    /**
     * Initialize Cube file storage
     * @param fname directory where to store the backing file.
     * @param n The storage is reserved in n sized chunks.
     *   This should be equal to Cube::size() that are passed into allocate()
     *   as no other allocation size is supported.
     * @note the backing file creation is delayed until allocate() is called first time.
     */
    CubeStorage(std::filesystem::path path, size_t n);
    ~CubeStorage();

    // not copyable
    CubeStorage(const CubeStorage&) = delete;
    CubeStorage& operator=(const CubeStorage&) = delete;
    // move constructible: but only if no allocations exists
    CubeStorage(CubeStorage&& mv);
    CubeStorage& operator=(CubeStorage&& mv) = delete;

    size_t cubeSize() const { return m_cube_size; }

    /**
     * Store Cube data into the backing file.
     * Returns CubePtr that can be inserted into CubeSwapSet.
     * @note cube.size() must be equal to this->cubeSize()
     */
    CubePtr allocate(const Cube& cube);

    /**
     * Revert the effect of last allocate()
     */
    void cancel_allocation();

    /**
     * Retrieve the cube data from the backing file.
     */
    Cube read(const CubePtr& x) const;

    /**
     * Drop all stored data.
     * Shrinks the backing file to zero size and deletes it.
     */
    void discard();
};

/**
 * CubeStorage enabled std::unordered_set<>
 *
 * The CubeSwapSet must be constructed with already initialized
 * stateful instances of CubePtrEqual and CubePtrHash functors
 * that resolve the CubePtr instance using the CubeStorage instance.
 */
using CubeSwapSet = std::unordered_set<CubePtr, CubePtrHash, CubePtrEqual>;

#endif