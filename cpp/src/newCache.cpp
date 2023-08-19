#include "newCache.hpp"

#include <iostream>

#include "cubeSwapSet.hpp"

CacheReader::CacheReader() : path_(""), fileLoaded_(false), dummyHeader{0, 0, 0, 0}, header(&dummyHeader), shapes(nullptr) {}

void CacheReader::printHeader() {
    if (fileLoaded_) {
        std::printf("magic: %x ", header->magic);
        std::printf("n: %d ", header->n);
        std::printf("numShapes: %d ", header->numShapes);
        std::printf("numPolycubes: %ld\n", header->numPolycubes);
    } else {
        std::printf("no file loaded!\n");
    }
}

int CacheReader::printShapes(void) {
    if (fileLoaded_) {
        for (uint64_t i = 0; i < header->numShapes; i++) {
            std::printf("%d\t%d\t%d\n", shapes[i].dim0, shapes[i].dim1, shapes[i].dim2);
        }
        return 1;
    }
    return 0;
}

int CacheReader::loadFile(const std::string path) {
    unload();
    path_ = path;

    // open read-only backing file:
    file_ = std::make_shared<mapped::file>();
    if (file_->open(path.c_str())) {
        std::printf("error opening file\n");
        return 1;
    }

    // map the header struct
    header_ = std::make_unique<const mapped::struct_region<cacheformat::Header>>(file_, 0);
    header = header_->get();

    if (header->magic != cacheformat::MAGIC) {
        std::printf("error opening file: file not recognized\n");
        return 1;
    }

    // map the ShapeEntry array:
    shapes_ = std::make_unique<const mapped::array_region<cacheformat::ShapeEntry>>(file_, header_->getEndSeek(), (*header_)->numShapes);
    shapes = shapes_->get();

    size_t datasize = 0;
    for (unsigned int i = 0; i < header->numShapes; ++i) {
        datasize += shapes[i].size;
    }

    // map rest of the file as XYZ data:
    if (file_->size() != shapes_->getEndSeek() + datasize) {
        std::printf("warn: file size does not match expected value\n");
    }
    xyz_ = std::make_unique<const mapped::array_region<XYZ>>(file_, shapes_->getEndSeek(), datasize);

    fileLoaded_ = true;

    return 0;
}

ShapeRange CacheReader::getCubesByShape(uint32_t i) {
    if (i >= header->numShapes) {
        return ShapeRange{nullptr, nullptr, 0, XYZ(0, 0, 0)};
    }
    if (shapes[i].size <= 0) {
        return ShapeRange{nullptr, nullptr, header->n, XYZ(shapes[i].dim0, shapes[i].dim1, shapes[i].dim2)};
    }
    // get section start
    // note: shapes[i].offset may have bogus offset
    // if any earlier shape table entry was empty before i
    // so we ignore the offset here.
    size_t offset = 0;
    for (unsigned int k = 0; k < i; ++k) {
        offset += shapes[k].size;
    }
    auto index = offset / cacheformat::XYZ_SIZE;
    auto num_xyz = shapes[i].size / cacheformat::XYZ_SIZE;
    // pointers to Cube data:
    auto start = xyz_->get() + index;
    auto end = xyz_->get() + index + num_xyz;
    return ShapeRange{start, end, header->n, XYZ(shapes[i].dim0, shapes[i].dim1, shapes[i].dim2)};
}

void CacheReader::unload() {
    // unload file from memory
    if (fileLoaded_) {
        xyz_.reset();
        shapes_.reset();
        header_.reset();
        file_.reset();
        fileLoaded_ = false;
    }
    header = &dummyHeader;
    shapes = nullptr;
}

CacheReader::~CacheReader() { unload(); }

CacheWriter::CacheWriter(int num_threads) {
    for (int i = 0; i < num_threads; ++i) {
        m_flushers.emplace_back(&CacheWriter::run, this);
    }
}

CacheWriter::CacheWriter::~CacheWriter() {
    flush();
    // stop the threads.
    std::unique_lock lock(m_mtx);
    m_active = false;
    m_run.notify_all();
    lock.unlock();
    for (auto &thr : m_flushers) thr.join();
}

void CacheWriter::run() {
    std::unique_lock lock(m_mtx);
    while (m_active) {
        // do copy jobs:
        if (!m_copy.empty()) {
            auto task = std::move(m_copy.front());
            m_copy.pop_front();
            lock.unlock();

            task();

            lock.lock();
            --m_num_copys;
            continue;
        }
        // file flushes:
        if (!m_flushes.empty()) {
            auto task = std::move(m_flushes.front());
            m_flushes.pop_front();
            lock.unlock();

            task();

            lock.lock();
            --m_num_flushes;
            continue;
        }
        // notify that we are done here.
        m_wait.notify_one();
        // wait for jobs.
        m_run.wait(lock);
    }
    m_wait.notify_one();
}

void CacheWriter::save(std::string path, Hashy &hashes, uint8_t n) {
    if (hashes.size() == 0) return;

    using namespace mapped;
    using namespace cacheformat;

    auto file_ = std::make_shared<file>();
    if (file_->openrw(path.c_str(), 0)) {
        std::printf("error opening file\n");
        return;
    }

    // Write header:
    auto header = std::make_shared<struct_region<Header>>(file_, 0);
    (*header)->magic = cacheformat::MAGIC;
    (*header)->n = n;
    (*header)->numShapes = hashes.numShapes();
    (*header)->numPolycubes = hashes.size();
    header->flush();

    std::vector<XYZ> keys;
    keys.reserve((*header)->numShapes);
    for (auto &pair : hashes) keys.push_back(pair.first);
    std::sort(keys.begin(), keys.end());

    // Write shape table:
    auto shapeEntry = std::make_shared<array_region<ShapeEntry>>(file_, header->getEndSeek(), (*header)->numShapes);
    header.reset();

    static_assert(XYZ_SIZE == sizeof(XYZ), "XYZ_SIZE differs from sizeof(XYZ)");

    uint64_t offset = shapeEntry->getEndSeek();
    size_t num_cubes = 0;
    int i = 0;
    for (auto &key : keys) {
        auto &se = (*shapeEntry)[i++];
        se.dim0 = key.x();
        se.dim1 = key.y();
        se.dim2 = key.z();
        se.reserved = 0;
        se.offset = offset;
        auto count = hashes.at(key).size();
        num_cubes += count;
        se.size = count * XYZ_SIZE * n;
        offset += se.size;
    }
    shapeEntry->flush();

    // put XYZs
    // Schedule merging of the cache file.
    // CubeSwapSet enables massive optimizations in how
    // CacheWriter can merge the SubsubHashy's data into the final cache file:
    // - copystorage lambda takes the source file and it's file name from the
    //   SubsubHashy::storage() returned CubeStorage.
    // - mapped::file::copyAt() is used to efficiently copy the source file contents into this cache file
    // - Finally the copystorage lambda *deletes* the source storage file
    // The main program does not need to wait for this process to complete.

    // copystorage takes shared ownership of the file_
    auto copystorage = [n, file = file_](std::shared_ptr<mapped::file> src, std::filesystem::path rmname, size_t num, mapped::seekoff_t dest) -> void {
        file->copyAt(src, 0, num * n * sizeof(XYZ), dest);
        src.reset();

        // Try remove the source storage file.
        std::error_code ec;
        auto stat = std::filesystem::status(rmname, ec);
        if (!ec && std::filesystem::is_regular_file(stat)) {
            if (!std::filesystem::remove(rmname, ec)) {
                std::printf("WARN: failed to remove file: %s", rmname.c_str());
            }
        } else {
            std::printf("WARN: failed to get file status: %s", rmname.c_str());
        }
    };

    mapped::seekoff_t fileEnd = shapeEntry->getEndSeek();
    auto time_start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < keys.size(); ++i) {
        auto put = (*shapeEntry)[i].offset;
        for (auto &subset : hashes.at(keys[i])) {
            ptrdiff_t num = subset.size();
            if (num) {
                // By pass iterating the Subsubhashy entirely
                // and copy the data from CubeStorage file *directly* into this file.
                // the Cube data does end up in different order than when copying one-by-one.
                // But we don't care as the order is random already.
                // the copy job also deletes the CubeStorage::fileName() file from the disk
                // once the data copy completes.
                std::unique_lock lock(m_mtx);
                m_copy.emplace_back(std::bind(copystorage, subset.storage().getFile(), subset.storage().fileName(), num, put));
                ++m_num_copys;
                m_run.notify_all();
                std::printf("scheduled copy jobs: %*d ...  \r", 3, (int)m_num_copys);
                std::flush(std::cout);
            }
            put += num * n * XYZ_SIZE;
        }
        fileEnd = std::max(fileEnd, put);
    }
    shapeEntry.reset();

    // sync up a bit.
    // don't allow the copy job queue to grow indefinitely
    // if the disk can't keep up.
    std::unique_lock lock(m_mtx);
    while (m_num_copys > m_flushers.size()) {
        std::printf("waiting for %*d copy jobs to complete ...  \r", 3, (int)m_num_copys);
        std::flush(std::cout);
        m_wait.wait(lock);
    }

    // move the file into flush job.
    m_flushes.emplace_back(std::bind(
        [fileEnd](auto &&file) -> void {
            file->truncate(fileEnd);
            file->close();
            file.reset();
        },
        std::move(file_)));
    ++m_num_flushes;
    m_run.notify_all();

    auto time_end = std::chrono::steady_clock::now();
    auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_start).count();

    std::printf("saved %s, took %.2f s\n\r", path.c_str(), dt_ms / 1000.f);
}

void CacheWriter::flush() {
    std::unique_lock lock(m_mtx);
    while (m_num_flushes) {
        std::printf("%*d copy jobs total remaining on %*d files  ...  \r", 3, (int)m_num_copys, 2, (int)m_num_flushes);
        std::flush(std::cout);
        m_wait.wait(lock);
    }
}
