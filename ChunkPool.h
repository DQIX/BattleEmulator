//
// Created by owner on 2026/01/15.
//

#ifndef NEWDIRECTORY_CHUNKPOOL_H
#define NEWDIRECTORY_CHUNKPOOL_H

#include <vector>
#include <cstdint>

template <class T, uint32_t CHUNK_SHIFT = 19>
class ChunkPool {
public:
    static constexpr uint32_t CHUNK_SIZE = 1u << CHUNK_SHIFT; // 0x80000
    static constexpr uint32_t CHUNK_MASK = CHUNK_SIZE - 1u;   // 0x7FFFF

    explicit ChunkPool(size_t estimated_chunks = 0) {
        if (estimated_chunks > 0) {
            chunks_.reserve(estimated_chunks);
        }
    }


    uint32_t alloc(const T& value) {
        const uint32_t id = nextId_++;
        ensureCapacity(id);
        get(id) = value;
        return id;
    }

    uint32_t alloc(T&& value) {
        const uint32_t id = nextId_++;
        ensureCapacity(id);
        get(id) = std::move(value);
        return id;
    }

    T& get(uint32_t id) {
        return chunks_[id >> CHUNK_SHIFT][id & CHUNK_MASK];
    }
    const T& get(uint32_t id) const {
        return chunks_[id >> CHUNK_SHIFT][id & CHUNK_MASK];
    }

    [[nodiscard]] constexpr uint32_t size() const { return nextId_; }

private:
    void ensureCapacity(uint32_t id) {
        const uint32_t needChunk = id >> CHUNK_SHIFT; // ← constexprにしない
        while (chunks_.size() <= needChunk) {
            chunks_.emplace_back();
            chunks_.back().resize(CHUNK_SIZE);
        }
    }

    std::vector<std::vector<T>> chunks_;
    uint32_t nextId_ = 0;
};

#endif //NEWDIRECTORY_CHUNKPOOL_H