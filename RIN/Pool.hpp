#pragma once

#include <bitset>
#include <mutex>
#include <iostream>

#include "Debug.hpp"

namespace RIN {
    /*
    Static Pool
    Can be allocated on the stack
    Size must be known at compile time
    Best used when N is small
    Very fast with small N

    Thread Safety:
    StaticPool::insert is thread-safe
    StaticPool::remove is thread-safe
    StaticPool::getSize is thread-safe
    StaticPool::getIndex is thread-safe
    StaticPool::at is thread-safe

    Could possibly keep track of the first
    free bit to speed up inserts, however
    this pool is only really meant for
    small values of N, so the additional
    overhead and memory accesses might
    end up making it slower in practice
    */
    template<class T, uint32_t N> class StaticPool {
        static_assert(N > 0, "Pool cannot be empty");

        std::mutex mutex;
        T data[N]{};
        std::bitset<N> mask;
    public:
        StaticPool() = default;
        StaticPool(const StaticPool<T, N>&) = delete; // A copy would result in leaked chunks
        ~StaticPool() = default;

        // Mimics an std emplace signature
        // Returns nullptr if there is no space
        template<class... Types> T* insert(Types&&... args) {
            uint32_t i = 0;

            {
                // Critical section
                std::lock_guard<std::mutex> lock(mutex);

                if(mask.all()) return nullptr;

                for(; i < N; ++i)
                    if(!mask.test(i)) break;

                mask.set(i);
            }

            // This thread now owns *(data + i)
            // Note the use of placement new to construct the object
            // in the already allocated pool memory
            return new(data + i) T(args...);
        }

        // Does no bounds checking
        // Does nothing if chunk is nullptr
        // Destroys chunk
        void remove(T* chunk) {
            if(!chunk) return;

            // Destruct first before handing the chunk back to the pool
            chunk->~T();

            // Critical section
            std::lock_guard<std::mutex> lock(mutex);

            mask.reset(chunk - data);
        }

        uint32_t getSize() const {
            return N;
        }

        uint32_t getIndex(T* chunk) const {
            return (uint32_t)(chunk - data);
        }

        // Does not bounds checking
        // Return nullptr if the data in the chunk is not resident
        T* at(uint32_t index) {
            if(mask.test(index))
                return &data[index];

            return nullptr;
        }

    #ifdef RIN_DEBUG
        template<class T, uint32_t N> friend std::ostream& operator<<(std::ostream&, const StaticPool<T, N>&);
    #endif
    };

#ifdef RIN_DEBUG
    template<class T, uint32_t N> std::ostream& operator<<(std::ostream& os, const StaticPool<T, N>& pool) {
        for(uint32_t i = 0; i < N; ++i) {
            if(pool.mask.test(i)) os << "|" << pool.data[i];
            else os << "| ";
        }
        os << "|";

        return os;
    }
#endif

    /*
    Dynamic Pool
    Backing buffer allocated on the heap
    Size is determined at runtime
    Best used when N is large

    Thread Safety:
    DynamicPool::insert is thread-safe
    DynamicPool::remove is thread-safe
    DynamicPool::getSize is thread-safe
    DynamicPool::getIndex is thread-safe
    DynamicPool::at is thread-safe
    */
    template<class T> class DynamicPool {
        /*
        This is kind of funky, but we know that alignof(Chunk) will either
        be alignof(Chunk*) or alignof(T) depending on which is larger. Since
        we know this, it is safe to use placement new at the beginning of
        Chunk, since that is where T is stored. Using this, we can construct
        T inside of the chunk safely and have the property that &T == &Chunk,
        allowing us to cast back and forth. Since resident is placed after
        T, it is safe to read and write it without interferering with the T
        created by the placement new.
        */
        struct Chunk {
            union {
                T data;
                Chunk* next{};
            };
            bool resident = false;

            ~Chunk() {
                if(resident) data.~T();
            }
        };

        std::mutex mutex;
        Chunk* block;
        Chunk* freeHead;
        const uint32_t size;
    public:
        DynamicPool(uint32_t N) :
            block(new Chunk[N]{}),
            freeHead(block),
            size(N)
        {
            for(uint32_t i = 0; i < N - 1; ++i)
                block[i].next = block + i + 1;
        }

        DynamicPool(const DynamicPool<T>&) = delete; // A copy would result in leaked chunks

        ~DynamicPool() {
            delete[] block;
        }

        // Mimics an std emplace signature
        // Returns nullptr if there is no space
        template<class... Types> T* insert(Types&&... args) {
            Chunk* chunk;
            {
                // Critical section
                std::lock_guard<std::mutex> lock(mutex);

                if(!freeHead) return nullptr;

                chunk = freeHead;
                freeHead = freeHead->next;
            }

            // This thread now owns *chunk
            chunk->resident = true;

            // Note the use of placement new to construct the object
            // inside of the chunk which is in already allocated pool memory
            return new(chunk) T(args...);
        }

        // Does no bounds checking
        // Does nothing if chunkData is nullptr
        // Destroys chunkData
        void remove(T* chunkData) {
            if(!chunkData) return;

            // Destruct first before handing the chunk back to the pool
            chunkData->~T();
            // The object was constructed at the start of the chunk, so
            // it is safe to cast it back to a Chunk*
            Chunk* chunk = (Chunk*)chunkData;
            chunk->resident = false;

            // Critical section
            std::lock_guard<std::mutex> lock(mutex);

            Chunk* prevHead = freeHead;
            freeHead = chunk;
            freeHead->next = prevHead;
        }

        uint32_t getSize() const {
            return size;
        }

        uint32_t getIndex(T* chunkData) const {
            return (uint32_t)((Chunk*)chunkData - block);
        }

        // Does no bounds checking
        // Returns nullptr if the data in the chunk is not resident
        T* at(uint32_t index) {
            Chunk* chunk = &block[index];

            if(chunk->resident)
                // T is located at the start of the chunk, so
                // it is safe to cast it to a T*
                return (T*)chunk;

            return nullptr;
        }

    #ifdef RIN_DEBUG
        template<class T> friend std::ostream& operator<<(std::ostream&, const DynamicPool<T>&);
    #endif
    };

#ifdef RIN_DEBUG
    template<class T> std::ostream& operator<<(std::ostream& os, const DynamicPool<T>& pool) {
        for(uint32_t i = 0; i < pool.size; ++i) {
            auto chunk = &pool.block[i];
            if(chunk->resident) os << "|" << chunk->data;
            else os << "| ";
        }
        os << "|";

        return os;
    }
#endif
}