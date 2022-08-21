#include "PoolAllocator.hpp"

namespace RIN {
	PoolAllocator::Allocation::Allocation(uint64_t start) : start(start) {}

	PoolAllocator::PoolAllocator(uint32_t elementCount, uint64_t elementSize) :
		elementCount(elementCount),
		elementSize(elementSize),
		top(nullptr)
	{
		uint32_t r = elementCount % BLOCK_SIZE;
		// Ceiling division
		uint32_t blockCount = elementCount / BLOCK_SIZE + !!r;
		offset = r ? r - 1 : BLOCK_SIZE - 1;
		
		uint64_t start = elementCount * elementSize - elementSize;
		for(uint32_t i = 0; i < blockCount; ++i) {
			StackBlock* block = new StackBlock;
			block->prev = top;
			// This will end up writing extra garbage values,
			// but it seems cheaper than checking the bounds of e
			// for every entry, especially for large pools
			for(uint32_t j = 0; j < BLOCK_SIZE; ++j, start -= elementSize)
				block->starts[j] = start;
			top = block;
		}
	}
	
	PoolAllocator::~PoolAllocator() {
		while(top) {
			StackBlock* block = top->prev;
			delete top;
			top = block;
		}
	}

	PoolAllocator::allocation_type PoolAllocator::allocate() {
		// Lock allocator
		std::lock_guard<std::mutex> lock(mutex);

		if(!top) return std::nullopt;

		uint64_t start = top->starts[offset];

		if(offset) --offset;
		else {
			// Shrink stack
			StackBlock* block = top->prev;
			delete top;
			top = block;
			offset = BLOCK_SIZE - 1;
		}

		// Mutex unlocked
		return start;
	}

	void PoolAllocator::free(allocation_type allocation) {
		if(!allocation) return;

		free(allocation.value());

		allocation.reset();
	}

	void PoolAllocator::free(Allocation allocation) {
		std::lock_guard<std::mutex> lock(mutex);

		if(offset == BLOCK_SIZE - 1) {
			// Grow stack
			StackBlock* block = new StackBlock;
			block->prev = top;
			top = block;
			offset = 0;
		} else ++offset;

		top->starts[offset] = allocation.start;

		// Mutex is unlocked
	}

	uint64_t PoolAllocator::getSize() const {
		return elementCount * elementSize;
	}

	uint64_t PoolAllocator::getElementSize() const {
		return elementSize;
	}
	
#ifdef RIN_DEBUG
	std::ostream& operator<<(std::ostream& os, const PoolAllocator& allocator) {
		if(allocator.top) {
			for(uint32_t i = allocator.offset; i > 0; --i)
				os << "|" << allocator.top->starts[i];
			os << "|" << allocator.top->starts[0];

			PoolAllocator::StackBlock* block = allocator.top->prev;
			uint32_t blockCount = 1;
			while(block) {
				for(uint32_t i = PoolAllocator::BLOCK_SIZE - 1; i > 0; --i)
					os << "|" << block->starts[i];
				os << "|" << block->starts[0];

				block = block->prev;
				++blockCount;
			}
			os << "| (" << blockCount << " block(s))";
		} else os << "| | (0 block(s))";

		return os;
	}
#endif
}