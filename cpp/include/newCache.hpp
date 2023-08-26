#pragma once
#ifndef OPENCUBES_NEWCACHE_HPP
#define OPENCUBES_NEWCACHE_HPP
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <memory>

#include "cube.hpp"
#include "hashes.hpp"
#include "mapped_file.hpp"

namespace cacheformat {
static constexpr uint32_t MAGIC = 0x42554350;
static constexpr uint32_t XYZ_SIZE = 3;
static constexpr uint32_t ALL_SHAPES = -1;

struct Header {
    uint32_t magic = MAGIC;  // shoud be "PCUB" = 0x42554350
    uint32_t n;              // we will never need 32bit but it is nicely aligned
    uint32_t numShapes;      // defines length of the shapeTable
    uint64_t numPolycubes;   // total number of polycubes
};
struct ShapeEntry {
    uint8_t dim0;      // offset by -1
    uint8_t dim1;      // offset by -1
    uint8_t dim2;      // offset by -1
    uint8_t reserved;  // for alignment
    uint64_t offset;   // from beginning of file
    uint64_t size;     // in bytes should be multiple of XYZ_SIZE
};
};  // namespace cacheformat

/**
 * newCache.hpp: provide two versions of the cache:
 *
 * - FlatCache implements "memory-only" cache and is constructed from Hashy.
 *   It is needed for boot-strapping the cache files and computing
 *   cubes without writing any data into disk.
 *   FlatCache::getCubesByShape() return ShapeRange that points into the Cube data in memory.
 *   ShapeRange then provides the Cube range as CubeIterator(s).
 *
 * - CacheReader implements the actual cache file system.
 *   CacheReader::getCubesByShape() return FileShapeRange that
 *   defines subset shape range from the cache file.
 *   FileShapeRange then provides the Cube range as CubeFileIterator(s).
 */
class ICubeIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Cube;
    using pointer = Cube*;    // or also value_type*
    using reference = Cube&;  // or also value_type&

    virtual ~ICubeIterator(){};

    virtual std::unique_ptr<ICubeIterator> clone() const = 0;

    virtual const value_type operator*() const = 0;
    virtual uint64_t seek() const = 0;
    virtual ICubeIterator& operator++() = 0;
    virtual ICubeIterator& operator+=(int incr) = 0;

    friend bool operator==(const ICubeIterator& a, const ICubeIterator& b) { return a.seek() == b.seek(); };
    friend bool operator<(const ICubeIterator& a, const ICubeIterator& b) { return a.seek() < b.seek(); };
    friend bool operator>(const ICubeIterator& a, const ICubeIterator& b) { return a.seek() > b.seek(); };
    friend bool operator!=(const ICubeIterator& a, const ICubeIterator& b) { return a.seek() != b.seek(); };
};

/**
 * Iterator for Cubes stored in some memory area.
 */
class CubeIterator : public ICubeIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Cube;
    using pointer = Cube*;    // or also value_type*
    using reference = Cube&;  // or also value_type&

    // constructor
    CubeIterator(uint32_t _n, const XYZ* ptr) : n(_n), m_ptr(ptr) {}

    // invalid iterator (can't deference)
    explicit CubeIterator() : n(0), m_ptr(nullptr) {}

    std::unique_ptr<ICubeIterator> clone() const override { return std::make_unique<CubeIterator>(*this); }

    // derefecence
    const value_type operator*() const override { return Cube(m_ptr, n); }

    // pointer operator->() { return (pointer)m_ptr; }

    uint64_t seek() const override { return (uint64_t)m_ptr; }

    // Prefix increment
    ICubeIterator& operator++() override {
        m_ptr += n;
        return *this;
    }

    ICubeIterator& operator+=(int incr) override {
        m_ptr += n * incr;
        return *this;
    }

    // Postfix increment
    CubeIterator operator++(int) {
        CubeIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    friend bool operator==(const CubeIterator& a, const CubeIterator& b) { return a.m_ptr == b.m_ptr; };
    friend bool operator<(const CubeIterator& a, const CubeIterator& b) { return a.m_ptr < b.m_ptr; };
    friend bool operator>(const CubeIterator& a, const CubeIterator& b) { return a.m_ptr > b.m_ptr; };
    friend bool operator!=(const CubeIterator& a, const CubeIterator& b) { return a.m_ptr != b.m_ptr; };

   private:
    uint32_t n;
    const XYZ* m_ptr;
};

class CubeReadIterator : public ICubeIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Cube;
    using pointer = Cube*;    // or also value_type*
    using reference = Cube&;  // or also value_type&

    // constructor
    CubeReadIterator(std::shared_ptr<mapped::file> file, uint32_t _n, mapped::seekoff_t offset) : n(_n), m_seek(offset), m_file(file) {}

    // invalid iterator (can't deference)
    explicit CubeReadIterator() : n(0), m_seek(-1) {}

    std::unique_ptr<ICubeIterator> clone() const override { return std::make_unique<CubeReadIterator>(*this); }

    // derefecence
    const value_type operator*() const override { return read(); }

    // pointer operator->() { return (pointer)m_seek; }

    uint64_t seek() const override { return (uint64_t)m_seek; }

    // Prefix increment
    ICubeIterator& operator++() override {
        m_seek += n * sizeof(XYZ);
        return *this;
    }

    ICubeIterator& operator+=(int incr) override {
        m_seek += n * incr * sizeof(XYZ);
        return *this;
    }

    // Postfix increment
    CubeReadIterator operator++(int) {
        CubeReadIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    friend bool operator==(const CubeReadIterator& a, const CubeReadIterator& b) { return a.m_seek == b.m_seek; };
    friend bool operator<(const CubeReadIterator& a, const CubeReadIterator& b) { return a.m_seek < b.m_seek; };
    friend bool operator>(const CubeReadIterator& a, const CubeReadIterator& b) { return a.m_seek > b.m_seek; };
    friend bool operator!=(const CubeReadIterator& a, const CubeReadIterator& b) { return a.m_seek != b.m_seek; };

   private:
    uint32_t n;
    mapped::seekoff_t m_seek;
    std::shared_ptr<mapped::file> m_file;

    // de-reference is implemented by read()
    Cube read() const;
};

/**
 * To avoid complicating the use of the ICubeIterator
 * CacheIterator provides type-erased wrapper that can be copied.
 */
class CacheIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Cube;
    using pointer = Cube*;    // or also value_type*
    using reference = Cube&;  // or also value_type&

    CacheIterator() {}

    template <typename Itr>
    explicit CacheIterator(Itr&& init) : proxy(std::make_unique<std::decay_t<Itr>>(std::forward<Itr>(init))) {}

    CacheIterator(const CacheIterator& copy) {
        if (copy.proxy) {
            proxy = copy.proxy->clone();
        }
    }
    CacheIterator& operator=(const CacheIterator& x) {
        CacheIterator tmp(x);
        std::swap(proxy, tmp.proxy);
        return *this;
    }
    CacheIterator(CacheIterator&& copy) =default;
    CacheIterator& operator=(CacheIterator&& x) =default;

    const value_type operator*() const { return **proxy; }

    uint64_t seek() const { return proxy->seek(); }

    CacheIterator& operator++() {
        ++(*proxy);
        return *this;
    }
    CacheIterator& operator+=(int incr) {
        (*proxy) += incr;
        return *this;
    }

    CacheIterator operator++(int) {
        CacheIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    friend bool operator==(const CacheIterator& a, const CacheIterator& b) { return a.seek() == b.seek(); };
    friend bool operator<(const CacheIterator& a, const CacheIterator& b) { return a.seek() < b.seek(); };
    friend bool operator>(const CacheIterator& a, const CacheIterator& b) { return a.seek() > b.seek(); };
    friend bool operator!=(const CacheIterator& a, const CacheIterator& b) { return a.seek() != b.seek(); };

   private:
    std::unique_ptr<ICubeIterator> proxy;
};

class IShapeRange {
   public:
    IShapeRange(){};
    virtual ~IShapeRange() {}

    virtual CacheIterator begin() const = 0;
    virtual CacheIterator end() const = 0;
    virtual XYZ& shape() = 0;
    virtual size_t size() const = 0;
};

class ShapeRange : public IShapeRange {
   public:
    ShapeRange(const XYZ* start, const XYZ* stop, uint64_t _cubeLen, XYZ _shape)
        : b(CubeIterator(_cubeLen, start)), e(CubeIterator(_cubeLen, stop)), size_(std::distance(start, stop) / _cubeLen), shape_(_shape) {}

    CacheIterator begin() const override { return b; }
    CacheIterator end() const override { return e; }

    XYZ& shape() override { return shape_; }
    size_t size() const override { return size_; }

   private:
    CacheIterator b, e;
    uint64_t size_;
    XYZ shape_;
};

class FileShapeRange : public IShapeRange {
   public:
    FileShapeRange(std::shared_ptr<mapped::file> file, mapped::seekoff_t start, mapped::seekoff_t stop, uint64_t _cubeLen, XYZ _shape)
        : b(CubeReadIterator(file, _cubeLen, start)),
        e(CubeReadIterator(file, _cubeLen, stop)),
        size_((stop - start) / _cubeLen), shape_(_shape) {}

    CacheIterator begin() const override { return b; }
    CacheIterator end() const override { return e; }

    XYZ& shape() override { return shape_; }
    size_t size() const override { return size_; }

   private:
    CacheIterator b, e;
    uint64_t size_;
    XYZ shape_;
};

class ICache {
   public:
    virtual ~ICache(){};
    virtual IShapeRange& getCubesByShape(uint32_t i) = 0;
    virtual uint32_t numShapes() = 0;
    virtual size_t size() = 0;
};

class CacheReader : public ICache {
   public:
    // constructor
    explicit CacheReader();
    // destuctor
    ~CacheReader();

    // methods
    void printHeader();
    int printShapes();
    int loadFile(const std::string path);
    void unload();

    size_t size() override { return header->numPolycubes; };
    uint32_t numShapes() override { return header->numShapes; };
    operator bool() { return fileLoaded_; }

    // get shapes at index [0, numShapes()[
    IShapeRange& getCubesByShape(uint32_t i) override;

   private:
    std::shared_ptr<mapped::file> file_;
    std::unique_ptr<const mapped::struct_region<cacheformat::Header>> header_;
    std::unique_ptr<const mapped::array_region<cacheformat::ShapeEntry>> shapes_;

    std::vector<FileShapeRange> shapeRanges;

    std::string path_;
    bool fileLoaded_;
    const cacheformat::Header dummyHeader;
    const cacheformat::Header* header;
    const cacheformat::ShapeEntry* shapes;
};

class FlatCache : public ICache {
    std::vector<XYZ> allXYZs;
    std::vector<ShapeRange> shapes;
    uint8_t n = 0;

   public:
    FlatCache() {}
    FlatCache(Hashy& hashes, uint8_t n) : n(n) {
        allXYZs.reserve(hashes.size() * n);
        shapes.reserve(hashes.numShapes());
        // std::printf("Flatcache %d %p %p\n", n, (void*)allXYZs.data(), (void*)shapes.data());
        for (auto& [shape, set] : hashes) {
            auto begin = allXYZs.data() + allXYZs.size();
            for (auto& subset : set) {
                for (auto& cubeptr : subset) cubeptr.copyout(subset.storage(), n, std::back_inserter(allXYZs));
            }
            auto end = allXYZs.data() + allXYZs.size();
            // std::printf("  SR %p %p\n", (void*)begin, (void*)end);
            shapes.emplace_back(begin, end, n, shape);
        }

        // Add dummy shape range at back:
        shapes.emplace_back(nullptr, nullptr, n, XYZ(0, 0, 0));
    }
    IShapeRange& getCubesByShape(uint32_t i) override {
        if (i >= shapes.size() - 1) return shapes.back();
        return shapes[i];
    };
    uint32_t numShapes() override { return shapes.size(); };
    size_t size() override { return allXYZs.size() / n / sizeof(XYZ); }
};

class CacheWriter {
   protected:
    std::mutex m_mtx;
    std::condition_variable m_run;
    std::condition_variable m_wait;
    bool m_active = true;

    // Jobs that flush and finalize the written file.
    size_t m_num_flushes = 0;
    std::deque<std::function<void(void)>> m_flushes;

    // Temporary copy jobs into the memory mapped file.
    size_t m_num_copys = 0;
    std::deque<std::function<void(void)>> m_copy;

    // thread pool executing the jobs.
    std::deque<std::thread> m_flushers;

    void run();

   public:
    CacheWriter(int num_threads = 8);
    ~CacheWriter();

    /**
     * Capture snapshot of the Hashy and write cache file.
     * The data may not be entirely flushed before save() returns.
     */
    void save(std::string path, Hashy& hashes, uint8_t n);

    /**
     * Complete all flushes immediately.
     */
    void flush();
};

#endif
