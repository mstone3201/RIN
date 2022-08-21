#pragma once

#include <iostream>
#include <cstdint>
#include <optional>
#include <atomic>

#include "Debug.hpp"

namespace RIN {
	/*
	Used for buffers which are allocated linearly and freed all at once

	Thread Safety:
	BumpAllocator::allocate is thread-safe
	BumpAllocator::free is thread-safe
	BumpAllocator::getSize is thread-safe
	std::ostream& operator<< is not thread-safe
	*/
	class BumpAllocator {
		const uint64_t size;
		std::atomic_uint64_t offset;
	public:
		// Make this mutable so that it can be reassigned
		struct Allocation {
			uint64_t start;
			Allocation(uint64_t start);
		};

		typedef std::optional<Allocation> allocation_type;

		BumpAllocator(uint64_t size);
		BumpAllocator(const BumpAllocator&) = delete;
		~BumpAllocator() = default;
		allocation_type allocate(uint64_t size);
		void free();
		uint64_t getSize() const;

	#ifdef RIN_DEBUG
		friend std::ostream& operator<<(std::ostream&, const BumpAllocator&);
	#endif
	};

#ifdef RIN_DEBUG
	std::ostream& operator<<(std::ostream&, const BumpAllocator&);
#endif
}