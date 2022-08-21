#include <iostream>

#include "FreeListAllocator.hpp"
#include "Error.hpp"

namespace RIN {
	FreeListAllocator::FreeBlock::FreeBlock() {
		for(uint32_t i = 0; i < BLOCK_SIZE - 1; ++i)
			chunks[i].nextStart = chunks + i + 1;
		chunks[BLOCK_SIZE - 1].nextStart = nullptr;
	}

	FreeListAllocator::Allocation::Allocation(uint64_t start, uint64_t size) :
		start(start),
		size(size)
	{}

	FreeListAllocator::FreeListAllocator(uint64_t size) :
		size(size),
		freeSpace(size)
	{
		pool = new FreeBlock();
		firstFree = pool->chunks;
		smallestFree = firstFree;
		firstFree->nextStart = nullptr;
		firstFree->nextSize = nullptr;
		firstFree->start = 0;
		firstFree->size = size;
		poolFree = firstFree + 1;
	}

	FreeListAllocator::~FreeListAllocator() {
		while(pool) {
			FreeBlock* block = pool->next;
			delete pool;
			pool = block;
		}
	}

	// NOTE: Due to how blocks are chosen for new allocations, fragmentation can be
	// minimized by making allocations in order from largest to smallest
	FreeListAllocator::allocation_type FreeListAllocator::allocate(uint64_t size) {
		if(!size) return std::nullopt;

		// Lock allocator
		std::lock_guard<std::mutex> lock(mutex);

		// Check that there is at least some space
		if(freeSpace < size || !smallestFree || !firstFree) return std::nullopt;

		// Find the smallest FreeChunk with large enough size
		FreeChunk* prev = nullptr;
		FreeChunk* chunk = smallestFree;
		while(chunk) {
			if(chunk->size >= size) break;
			prev = chunk;
			chunk = chunk->nextSize;
		}

		// Memory is too fragmented
		// Mutex is unlocked
		if(!chunk) return std::nullopt;

		uint64_t start = chunk->start;

		// Remove node from size ordered list
		if(prev) prev->nextSize = chunk->nextSize;
		else smallestFree = chunk->nextSize;

		uint64_t chunkSize = chunk->size - size;

		if(chunkSize) {
			// Can just modify chunk and change links
			chunk->start += size;
			chunk->size = chunkSize;

			// Insert chunk in size order
			FreeChunk* sizePrev = nullptr;
			FreeChunk* sizeChunk = smallestFree;
			while(sizeChunk) {
				if(chunkSize <= sizeChunk->size) break;
				sizePrev = sizeChunk;
				sizeChunk = sizeChunk->nextSize;
			}

			chunk->nextSize = sizeChunk;
			if(sizePrev) sizePrev->nextSize = chunk;
			else smallestFree = chunk;

			// The start ordered list has not been invalidated
			// Chunks do not overlap, so the start cannot be
			// Incremented past the start of the next chunk
		} else {
			// Chunk should be deleted
			// Chunk already removed from size ordered list
			// Find chunk in start ordered list
			FreeChunk* startPrev = nullptr;
			FreeChunk* startChunk = firstFree;
			while(startChunk) {
				if(startChunk == chunk) break;
				startPrev = startChunk;
				startChunk = startChunk->nextStart;
			}

			if(!startChunk) throw Error("Allocator free list anomaly");

			// Remove chunk from start ordered list
			if(startPrev) startPrev->nextStart = chunk->nextStart;
			else firstFree = chunk->nextStart;

			// Free the chunk from the pool
			chunk->nextStart = poolFree;
			poolFree = chunk;
		}

		freeSpace -= size;

		// Mutex is unlocked
		return allocation_type{std::in_place, start, size};
	}

	void FreeListAllocator::free(allocation_type allocation) {
		if(!allocation) return;

		free(allocation.value());

		allocation.reset();
	}

	void FreeListAllocator::free(Allocation allocation) {
		// Lock allocator
		std::lock_guard<std::mutex> lock(mutex);

		// Find chunk neighbors
		FreeChunk* prev = nullptr;
		FreeChunk* next = firstFree;
		while(next) {
			if(allocation.start < next->start)
				break;
			prev = next;
			next = next->nextStart;
		}

		FreeChunk* merged = nullptr;
		uint64_t mergedSize = allocation.size;
		// See if this chunk can merge with the previous one
		if(prev && prev->start + prev->size == allocation.start) {
			mergedSize += prev->size;

			// See if we can also merge the next chunk
			if(next && allocation.start + allocation.size == next->start) {
				mergedSize += next->size;
				
				// Remove next from start ordered list
				prev->nextStart = next->nextStart;

				// Find chunk in size ordered list
				FreeChunk* sizePrev = nullptr;
				FreeChunk* sizeChunk = smallestFree;
				while(sizeChunk) {
					if(sizeChunk == next) break;
					sizePrev = sizeChunk;
					sizeChunk = sizeChunk->nextSize;
				}

				if(!sizeChunk) throw Error("Allocator free list anomaly");

				// Remove chunk from size ordered list
				if(sizePrev) sizePrev->nextSize = sizeChunk->nextSize;
				else smallestFree = sizeChunk->nextSize;

				// Free the chunk from the pool
				next->nextStart = poolFree;
				poolFree = next;
			}

			prev->size = mergedSize;

			merged = prev;
		} else if(next && allocation.start + allocation.size == next->start) {
			// Merge with the next chunk
			next->start = allocation.start;
			mergedSize += next->size;
			next->size = mergedSize;

			merged = next;
		}

		if(merged) {
			// See if the chunk is out of order in size ordered list
			FreeChunk* mergedNext = merged->nextSize;
			if(mergedNext && mergedSize > mergedNext->size) {
				// Update order of chunk in size ordered list
				FreeChunk* sizePrev = nullptr;
				FreeChunk* sizeChunk = smallestFree;
				while(sizeChunk) {
					// Remove the chunk from size ordered list
					if(sizeChunk == merged) {
						sizeChunk = mergedNext;
						if(sizePrev) sizePrev->nextSize = sizeChunk;
						else smallestFree = sizeChunk;
					}
					// Find where chunk should be
					if(mergedSize <= sizeChunk->size) break;
					sizePrev = sizeChunk;
					sizeChunk = sizeChunk->nextSize;
				}

				// Insert chunk
				merged->nextSize = sizeChunk;
				if(sizePrev) sizePrev->nextSize = merged;
				else smallestFree = merged;
			}
		} else if(!merged) {
			// Obtain a FreeChunk
			FreeChunk* chunk = poolFree;
			if(poolFree)
				poolFree = poolFree->nextStart;
			else {
				// Allocate a new FreeBlock
				FreeBlock* block = new FreeBlock();
				block->next = pool;
				pool = block;
				chunk = block->chunks;
				poolFree = chunk + 1;
			}
			
			chunk->start = allocation.start;
			chunk->size = allocation.size;

			// Insert the chunk into start ordered list
			chunk->nextStart = next;
			if(prev) prev->nextStart = chunk;
			else firstFree = chunk;

			// Insert the chunk into size ordered list
			FreeChunk* sizePrev = nullptr;
			FreeChunk* sizeChunk = smallestFree;
			while(sizeChunk) {
				if(allocation.size <= sizeChunk->size) break;
				sizePrev = sizeChunk;
				sizeChunk = sizeChunk->nextSize;
			}

			chunk->nextSize = sizeChunk;
			if(sizePrev) sizePrev->nextSize = chunk;
			else smallestFree = chunk;
		}

		freeSpace += allocation.size;

		// Mutex is unlocked
	}

	uint64_t FreeListAllocator::getSize() const {
		return size;
	}

#ifdef RIN_DEBUG
	std::ostream& operator<<(std::ostream& os, const FreeListAllocator& allocator) {
		if(!allocator.firstFree) {
			os << "| |";
		} else {
			FreeListAllocator::FreeChunk* chunk = allocator.firstFree;
			uint64_t start = 0;
			while(chunk) {
				if(chunk->start == start)
					os << "|" << chunk->size;
				else
					os << "| |" << chunk->size;
				start = chunk->start + chunk->size;
				chunk = chunk->nextStart;
			}
			if(start == allocator.size) os << "|";
			else os << "| |";
		}

		uint32_t blockCount = 0;
		FreeListAllocator::FreeBlock* block = allocator.pool;
		while(block) {
			++blockCount;
			block = block->next;
		}
		os << " (" << blockCount << " block(s))";

		return os;
	}
#endif
}