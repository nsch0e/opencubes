#include "cubeSwapSet.hpp"

#include <filesystem>
#include <list>
#include <unordered_map>

/**
 * thread-local read-cache for Cube(s)
 */
class ThreadCache {
   public:
    static ThreadCache& get();

    struct entry {
        // read-cache "key"
        const CubeStorage* storage;
        mapped::seekoff_t seek;
        int version;

        friend bool operator==(const entry& a, const entry& b) { return std::tie(a.storage, a.seek, a.version) == std::tie(b.storage, b.seek, b.version); }
    };

    struct state {
        // cached data.
        Cube cube;
        std::list<entry>::iterator lru;
    };

    struct entry_hash {
        size_t operator()(const entry& x) const {
            size_t seed = uintptr_t(x.storage);
            seed ^= x.seek + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= x.version + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        };
    };

    // Least-recently-used, LRU eviction policy list.
    std::list<entry> lru;
    // trick: make map with reference_wrapper<entry>
    // as key so we don't need to duplicate the data from the lru list.
    // surprisingly C++17 cache.find(entry) works.
    std::unordered_map<std::reference_wrapper<const entry>, state, entry_hash, std::equal_to<entry>> cache;

    bool local_enabled = false;
    mapped::seekoff_t local_seek = -1;
    Cube local;
};

ThreadCache& ThreadCache::get() {
    static thread_local ThreadCache instance;
    return instance;
}

std::atomic<int> CubeStorage::m_init_num(0);

CubeStorage::CubeStorage(std::filesystem::path path, size_t n) : m_cube_size(n) {
    // Generate file name:
    m_fpath = path / ("storage_" + std::to_string(m_init_num.fetch_add(1)) + ".bin");
}

CubeStorage::~CubeStorage() { discard(); }

CubeStorage::CubeStorage(CubeStorage&& mv)
    : m_fpath(std::move(mv.m_fpath)), m_file(std::move(mv.m_file)), m_cube_size(mv.m_cube_size), m_alloc_seek(mv.m_alloc_seek) {
    // no allocations can exist in the moved from object:
    assert(m_alloc_seek == 0);
}

CubePtr CubeStorage::local(const Cube& cube) const {
    auto& ctx = ThreadCache::get();
    ctx.local = cube;
    ctx.local_seek = m_alloc_seek;
    ctx.local_enabled = true;
    return CubePtr(ctx.local_seek);
}

void CubeStorage::commit() {
    std::lock_guard lock(m_mtx);

    if (!m_file) {
        using namespace mapped;
        // file not open yet.
        m_file = std::make_shared<file>();
        if (m_file->openrw(m_fpath.c_str(), 0, file::CREATE | file::RESIZE | file::FSTUNE)) {
            std::printf("CubeStorage::allocate() ERROR: Failed to create file: %s\n", m_fpath.c_str());
            std::abort();
        }
    }

    auto& ctx = ThreadCache::get();
    assert(ctx.local_enabled);
    assert(ctx.local_seek == m_alloc_seek);
    ctx.local_enabled = false;

    m_file->writeAt(m_alloc_seek, m_cube_size * sizeof(XYZ), ctx.local.data());
    m_alloc_seek += m_cube_size * sizeof(XYZ);
}

void CubeStorage::drop() const {
    auto& ctx = ThreadCache::get();
    assert(ctx.local_enabled);
    ctx.local_enabled = false;
    ctx.local_seek = -1;
}

const Cube& CubeStorage::read(const CubePtr& x) const {
    // Get thread's cache instance:
    auto& ctx = ThreadCache::get();

    // Check if x is actually the object returned by local():
    if (ctx.local_enabled && x.seek() == ctx.local_seek) {
        assert(ctx.local.size() == m_cube_size);
        return ctx.local;
    }

    ThreadCache::entry key{this, x.seek(), m_storage_version};
    auto itr = ctx.cache.find(key);
    if (itr != ctx.cache.end()) {
        // cache-hit.
        // LRU policy simply moves the element at back of the list:
        if (std::next(itr->second.lru) != ctx.lru.end()) {
            ctx.lru.splice(itr->second.lru, ctx.lru, ctx.lru.end());
        }
        return itr->second.cube;
    } else {
        // cache-miss.
        // Evict entry at front if read-cache is full:
        if (ctx.cache.size() >= 1024) {
            auto rm = ctx.cache.find(ctx.lru.front());
            ctx.cache.erase(rm);
            ctx.lru.pop_front();
        }

        // Read Cube data
        Cube tmp(m_cube_size);
        m_file->readAt(x.seek(), m_cube_size * sizeof(XYZ), tmp.data());

        // Move it into an new read-cache entry:
        auto nitr = ctx.lru.insert(ctx.lru.end(), key);
        auto [itr, ok] = ctx.cache.emplace(std::ref(*nitr), ThreadCache::state{std::move(tmp), nitr});
        assert(ok);
        return itr->second.cube;
    }
}

void CubeStorage::resetReadCache() const {
    auto& ctx = ThreadCache::get();
    ctx.cache.clear();
    ctx.lru.clear();
}

void CubeStorage::copydata(const CubePtr& x, size_t n, XYZ* destination) const {
    // copydata() doesn't use thread's read-cache
    // so local() cannot be active:
    assert(!ThreadCache::get().local_enabled);
    m_file->readAt(x.seek(), n * sizeof(XYZ), destination);
}

void CubeStorage::discard() {
    std::lock_guard lock(m_mtx);

    if (m_file) {
        // The backing file is kept intact
        // so that CacheWriter can process it.
        m_file.reset();
        m_alloc_seek = 0;
        // Thread read-cache problem:
        // discard() must cause eviction of all entries for each
        // thread's read cache that point into this.
        // This done by incrementing m_storage_version:
        // the entries can't simply be found as they are
        // made with m_storage_version - 1 value.
        // The entries are eventually evicted by
        // the read-cache this way.
        ++m_storage_version;
    }
}

const Cube& CubePtr::get(const CubeStorage& storage) const {
    // CubePtr::get() is really just an convenience function...
    // However this cannot be implemented in the header file because
    // CubeStorage definition is not known.
    return storage.read(*this);
}

void CubePtr::copyout(const CubeStorage& storage, size_t n, XYZ* out) const {
    // CubePtr::copyout() is really just an convenience function...
    // However this cannot be implemented in the header file because
    // CubeStorage definition is not known.
    storage.copydata(*this, n, out);
}