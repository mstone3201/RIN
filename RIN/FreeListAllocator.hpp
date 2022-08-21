#pragma once

#include <iostream>
#include <cstdint>
#include <optional>
#include <mutex>

#include "Debug.hpp"

namespace RIN {
	/*
	Used for buffers with elements of variable length (ex. storing mesh data in one large buffer)

	Thread Safety:
	FreeListAllocator::allocate is thread-safe
	FreeListAllocator::free is thread-safe
	FreeListAllocator::getSize is thread-safe
	std::ostream& operator<< is not thread-safe
	*/
	class FreeListAllocator {
		// CAUTION: Minimum allowable blockSize is 2
		static constexpr uint32_t BLOCK_SIZE = 16; // 512 bytes

		/*
		This acts like two linked lists with shared data
		nextStart list used for merging adjacent free chunks
		nextSize list used for picking best fit chunks (greedy)
		Might want to profile defragmenting every once in a while instead of using nextStart ptr
		*/
		struct FreeChunk {
			FreeChunk* nextStart; // next FreeChunk in ascending start order
			FreeChunk* nextSize; // next FreeChunk in ascending size order
			uint64_t start;
			uint64_t size;
		};

		struct FreeBlock {
			FreeBlock* next{};
			FreeChunk chunks[BLOCK_SIZE];
			FreeBlock();
		};

		// The locking is kind of crude, since we need to keep the two sorted linked lists
		// valid for every allocate and free, so it's hard to make the locking fine-grained
		std::mutex mutex;
		const uint64_t size;
		// Memory pool to store free list
		// Reuse nextStart for the pool's free list
		FreeBlock* pool;
		FreeChunk* firstFree;
		FreeChunk* smallestFree;
		FreeChunk* poolFree;
		uint64_t freeSpace; // Used to determine if an allocation could possibly be made
	public:
		// Make this mutable so that it can be reassigned
		struct Allocation {
			uint64_t start;
			uint64_t size;
			Allocation(uint64_t start, uint64_t size);
		};

		typedef std::optional<Allocation> allocation_type;

		FreeListAllocator(uint64_t size);
		FreeListAllocator(const FreeListAllocator&) = delete;
		~FreeListAllocator();
		allocation_type allocate(uint64_t size);
		void free(allocation_type allocation);
		void free(Allocation allocation);
		uint64_t getSize() const;

	#ifdef RIN_DEBUG
		friend std::ostream& operator<<(std::ostream&, const FreeListAllocator&);
	#endif
	};

#ifdef RIN_DEBUG
	std::ostream& operator<<(std::ostream&, const FreeListAllocator&);
#endif
}