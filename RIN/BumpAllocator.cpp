#include "BumpAllocator.hpp"

namespace RIN {
	BumpAllocator::Allocation::Allocation(uint64_t start) : start(start) {}

	BumpAllocator::BumpAllocator(uint64_t size) : size(size), offset(0) {}

	BumpAllocator::allocation_type BumpAllocator::allocate(uint64_t size) {
		if(!size) return std::nullopt;

		uint64_t max = BumpAllocator::size - size;

		// Explanation of memory models: https://gcc.gnu.org/wiki/Atomic/GCCMM/AtomicSync
		// Here we don't have any state to synchronize between threads, just
		// a single atomic variable, so it is safe to use relaxed everywhere
		uint64_t prev = offset.load(std::memory_order_relaxed);
		bool allocated;
		// Use compare_exchange to attempt to update the value,
		// if it fails, that means offset has been changed by another thread,
		// so check that space for the allocation still exists and try again
		while((allocated = prev <= max) && !offset.compare_exchange_weak(prev, prev + size, std::memory_order_relaxed));

		return allocated ? allocation_type{std::in_place, prev} : std::nullopt;
	}

	void BumpAllocator::free() {
		offset.store(0, std::memory_order_relaxed);
	}

	uint64_t BumpAllocator::getSize() const {
		return size;
	}

#ifdef RIN_DEBUG
	std::ostream& operator<<(std::ostream& os, const BumpAllocator& allocator) {
		std::cout << allocator.offset << "/" << allocator.size;
		return os;
	}
#endif
}