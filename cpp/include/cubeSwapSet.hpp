#pragma once
#ifndef OPENCUBES_CUBE_DISKSWAP_SET_HPP
#define OPENCUBES_CUBE_DISKSWAP_SET_HPP

#include <filesystem>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_set>

#include "cube.hpp"
#include "mapped_file.hpp"

/**
 * CubeSwapSet: Implement std::unordered_set<> that offloads XYZ data into a file:
 *
 * Cubes stored in the set have reduced cost of memory:
 * Only the std::unordered_set<> itself and the internal nodes are stored in RAM.
 * The element *data* (i.e. XYZ data) is stored in the file.
 * The performance cost is that each time the set element is accessed
 * the data is read back from the file.
 * (Iterating the entire CubeSwapSet involves reading the entire file)
 *
 * Features:
 * - XYZ data is recorded sequentially into the file and
 *   the Cube size is not saved in the storage file.
 * - Cube XYZ data length is constant in CubeStorage instance.
 * - Clearing the CubeSwapSet does not release the file managed by CubeStorage.
 *   (CubePtr(s) cannot be erased from CubeStorage)
 * - CubeStorage::read(const CubePtr&) caches up to 1024 Cubes for each thread.
 *   This read-cache is maintained by any thread that calls CubePtr::get().
 *   CubeStorage::discard() is used to begin writing the XYZ data at new file instance.
 * - CacheWriter utilizes the file instance from CubeStorage:
 *   the CubeSwapSet is not iterated through at all by CacheWriter
 *   and instead CubeStorage::getFile() is assigned into a copy job and then
 *   copied into the cache-file with mapped::file::copyAt().
 *   The source storage file is deleted once the copy is completed.
 *   This provides wait-free saving of the cache-file and uses
 *   minimal amount of system memory.
 */
class CubeStorage;

/**
 * CubePtr: "File Pointer to Cube" that reads the cube data from file.
 * CubePtr needs CubeStorage instance to be able to access
 * its contents with CubePtr::get().
 * The associated CubeStorage should always be available
 * in context where CubePtr(s) data is accessed.
 */
class CubePtr {
   protected:
    mapped::seekoff_t m_seek = 0;

   public:
    explicit CubePtr(mapped::seekoff_t offset) : m_seek(offset) {}
    CubePtr(const CubePtr& c) : m_seek(c.m_seek) {}

    /**
     * Get the Cube pointed by this instance.
     * @note The Cube is cached in the thread-local read-cache.
     * @warn
     *  The Cube object is local to calling thread and shall
     *  not be passed into other threads.
     */
    const Cube& get(const CubeStorage& storage) const;

    /**
     * Raw data copy. By-passes the thread-local cache.
     */
    void copyout(const CubeStorage& storage, size_t n, XYZ* out) const;

    template <typename Itr>
    void copyout(const CubeStorage& storage, size_t n, Itr out) const {
        std::vector<XYZ> buff(n);
        copyout(storage, n, buff.data());
        std::copy_n(buff.begin(), n, out);
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

    bool operator()(const CubePtr& a, const CubePtr& b) const {
        // todo: there is possibility that
        // a.get() returned cube is *deleted* from the cache by b.get()
        // The read-cache size must be at least 3 to avoid this.
        return a.get(*m_storage) == b.get(*m_storage);
    }
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

    size_t operator()(const CubePtr& x) const {
        auto& cube = x.get(*m_storage);
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
    mutable std::mutex m_mtx;
    std::filesystem::path m_fpath;
    std::shared_ptr<mapped::file> m_file;

    static std::atomic<int> m_init_num;
    int m_storage_version = 0;
    const size_t m_cube_size;

    mapped::seekoff_t m_alloc_seek;

   public:
    /**
     * Initialize Cube file storage
     * @param path directory where to write the storage file.
     * @param n The storage is written in n sized chunks of XYZ structs.
     *   This should be equal to Cube::size() that are passed into local()
     *   Different sized Cubes in same CubeStorage instance will not work.
     * @note the file creation is delayed until commit() is called first time.
     */
    CubeStorage(std::filesystem::path path, size_t n);
    ~CubeStorage();

    // not copyable
    CubeStorage(const CubeStorage&) = delete;
    CubeStorage& operator=(const CubeStorage&) = delete;
    // move constructible: but only if no allocations exists in mv
    CubeStorage(CubeStorage&& mv);
    CubeStorage& operator=(CubeStorage&& mv) = delete;

    size_t cubeSize() const { return m_cube_size; }

    /**
     * Make thread local CubePtr instance.
     * @note
     *  Other thread cannot access the returned CubePtr until commit() is called.
     */
    CubePtr local(const Cube& cube) const;

    /**
     * Publish the last local() returned CubePtr.
     * commit() writes this the data into the file storage
     * making it visible to all threads.
     */
    void commit();

    /**
     * Discard the last local() returned CubePtr.
     */
    void drop() const;

    /**
     * Retrieve the cube data from the backing file
     * and cache the result for the caller thread.
     */
    const Cube& read(const CubePtr& x) const;

    /**
     * Copy the cube data from the storage into destination buffer.
     */
    void copydata(const CubePtr& x, size_t n, XYZ* destination) const;

    /**
     * Explicitly clear the calling thread's read-cache.
     * @note this will initialize callers read-cache instance
     *  if the thread has not used the read-cache yet.
     *  So only call this from thread that has used to read().
     */
    void resetReadCache() const;

    /**
     * Get the file name CubeStorage is using.
     */
    std::filesystem::path fileName() const { return m_fpath; }

    /**
     * Get the mapped::file instance.
     * @note this can be null if nothing has been written to the storage yet.
     */
    std::shared_ptr<mapped::file> getFile() const { return m_file; }

    /**
     * Drop all stored data.
     */
    void discard();
};

/**
 * CubeStorage enabled std::unordered_set<>
 *
 * The CubeSwapSet must be constructed with already initialized
 * stateful instances of CubePtrEqual and CubePtrHash functors
 * that resolve the CubePtr(s) using the CubeStorage instance.
 */
using CubeSwapSet = std::unordered_set<CubePtr, CubePtrHash, CubePtrEqual>;

#endif