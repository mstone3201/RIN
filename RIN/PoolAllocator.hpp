#pragma once

#include <iostream>
#include <cstdint>
#include <optional>
#include <mutex>

#include "Debug.hpp"

namespace RIN {
	/*
	Used for buffers with elements of uniform size

	Thread Safety:
	PoolAllocator::allocate is thread-safe
	PoolAllocator::free is thread-safe
	PoolAllocator::getSize is thread-safe
	PoolAllocator::getElementSize is thread-safe
	std::ostream& operator<< is not thread-safe
	*/
	class PoolAllocator {
		static constexpr uint32_t BLOCK_SIZE = 16;

		struct StackBlock {
			StackBlock* prev;
			uint64_t starts[BLOCK_SIZE];
		};

		std::mutex mutex;
		const uint64_t elementSize;
		// Stack to store free chunks
		StackBlock* top;
		// Offset to the top of the top block
		uint32_t offset;
		const uint32_t elementCount;
	public:
		// Make this mutable so that it can be reassigned
		struct Allocation {
			uint64_t start;
			Allocation(uint64_t start);
		};

		typedef std::optional<Allocation> allocation_type;

		PoolAllocator(uint32_t elementCount, uint64_t elementSize);
		PoolAllocator(const PoolAllocator&) = delete;
		~PoolAllocator();
		allocation_type allocate();
		void free(allocation_type allocation);
		void free(Allocation allocation);
		uint64_t getSize() const;
		uint64_t getElementSize() const;

	#ifdef RIN_DEBUG
		friend std::ostream& operator<<(std::ostream&, const PoolAllocator&);
	#endif
	};

#ifdef RIN_DEBUG
	std::ostream& operator<<(std::ostream&, const PoolAllocator&);
#endif
}