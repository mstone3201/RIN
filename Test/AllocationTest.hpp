#pragma once

#include <iostream>
#include <thread>
#include <chrono>

#include <FreeListAllocator.hpp>
#include <PoolAllocator.hpp>
#include <BumpAllocator.hpp>

void testFreeListAllocator() {
	RIN::FreeListAllocator allocator(100);
	std::cout << allocator << std::endl; // Expected |100|
	auto a1 = allocator.allocate(20);
	std::cout << allocator << std::endl; // Expected | |80|
	auto a2 = allocator.allocate(15);
	std::cout << allocator << std::endl; // Expected | |65|
	auto a3 = allocator.allocate(65);
	std::cout << allocator << std::endl; // Expected | |
	auto a4 = allocator.allocate(5);
	if(a4) std::cout << "Allocated 5 even though pool is full" << std::endl;
	else std::cout << "Could not allocate 5 because pool is full" << std::endl; // Expected
	allocator.free(a3);
	std::cout << allocator << std::endl; // Expected | |65|
	a3 = allocator.allocate(40);
	std::cout << allocator << std::endl; // Expected | |25|
	allocator.free(a2);
	std::cout << allocator << std::endl; // Expected | |15| |25|
	// Bad ordering
	a2 = allocator.allocate(1);
	a4 = allocator.allocate(15);
	std::cout << allocator << std::endl; // Expected | |14| |10|
	auto a5 = allocator.allocate(24);
	if(a5) std::cout << "Allocated 24 even though pool is fragemented" << std::endl;
	else std::cout << "Could not allocate 24 because pool is fragmented" << std::endl; // Expected
	allocator.free(a2);
	allocator.free(a4);
	// Good ordering
	a2 = allocator.allocate(24);
	a4 = allocator.allocate(15);
	a5 = allocator.allocate(1);
	std::cout << allocator << std::endl; // Expected | |
	allocator.free(a2);
	std::cout << allocator << std::endl; // Expected | |24| |
	allocator.free(a5);
	allocator.free(a4);
	allocator.free(a3);
	allocator.free(a1);
	std::cout << allocator << std::endl; // Expected |100|
	// Bad ordering
	a1 = allocator.allocate(20);
	a2 = allocator.allocate(20);
	a3 = allocator.allocate(20);
	a4 = allocator.allocate(20);
	a5 = allocator.allocate(20);
	allocator.free(a5);
	a5 = allocator.allocate(19);
	allocator.free(a4);
	a4 = allocator.allocate(19);
	allocator.free(a3);
	a3 = allocator.allocate(19);
	allocator.free(a2);
	a2 = allocator.allocate(19);
	allocator.free(a1);
	a1 = allocator.allocate(19);
	std::cout << allocator << std::endl; // Expected | |1| |1| |1| |1| |1|
	allocator.free(a1);
	allocator.free(a2);
	allocator.free(a3);
	allocator.free(a4);
	allocator.free(a5);
	// Good ordering
	a1 = allocator.allocate(20);
	a2 = allocator.allocate(20);
	a3 = allocator.allocate(20);
	a4 = allocator.allocate(20);
	a5 = allocator.allocate(20);
	allocator.free(a1);
	a1 = allocator.allocate(19);
	allocator.free(a2);
	a2 = allocator.allocate(19);
	allocator.free(a3);
	a3 = allocator.allocate(19);
	allocator.free(a4);
	a4 = allocator.allocate(19);
	allocator.free(a5);
	a5 = allocator.allocate(19);
	std::cout << allocator << std::endl; // Expected | |5|
	allocator.free(a1);
	allocator.free(a2);
	allocator.free(a3);
	allocator.free(a4);
	allocator.free(a5);
	std::cout << allocator << std::endl; // Expected |100|
}

void testThreadedFLA() {
	RIN::FreeListAllocator allocator(100);

	auto work = [&allocator](uint64_t size) {
		using namespace std::chrono_literals;
		for(uint64_t i = 0; i < size; ++i) {
			std::this_thread::sleep_for(0.01ms);
			auto a1 = allocator.allocate(1);
			std::this_thread::sleep_for(0.01ms);
			allocator.allocate(1);
			std::this_thread::sleep_for(0.01ms);
			allocator.free(a1);
		}
	};

	std::cout << allocator << std::endl; // Expected |100|

	std::thread t1(work, 10);
	std::thread t2(work, 20);
	std::thread t3(work, 30);
	std::thread t4(work, 20);
	t1.join();
	t2.join();
	t3.join();
	t4.join();

	std::cout << allocator << std::endl; // Expected free space of 20
}

void testPoolAllocator() {
	RIN::PoolAllocator allocator(5, 32);
	std::cout << allocator << std::endl; // Expected |0|32|64|96|128|
	auto a1 = allocator.allocate();
	auto a2 = allocator.allocate();
	auto a3 = allocator.allocate();
	std::cout << allocator << std::endl; // Expected |96|128|
	std::cout << "A1: " << a1->start << std::endl; // Expected 0
	std::cout << "A2: " << a2->start << std::endl; // Expected 32
	std::cout << "A3: " << a3->start << std::endl; // Expected 64
	allocator.free(a2);
	std::cout << allocator << std::endl; // Expected |32|96|128|
	auto a4 = allocator.allocate();
	a2 = allocator.allocate();
	std::cout << allocator << std::endl; // Expected |128|
	std::cout << "A1: " << a1->start << std::endl; // Expected 0
	std::cout << "A2: " << a2->start << std::endl; // Expected 96
	std::cout << "A3: " << a3->start << std::endl; // Expected 64
	std::cout << "A4: " << a4->start << std::endl; // Expected 32
	auto a5 = allocator.allocate();
	std::cout << allocator << std::endl; // Expected | |
	auto a6 = allocator.allocate();
	if(a6) std::cout << "Allocated even though pool is full" << std::endl;
	else std::cout << "Could not allocate because pool was full" << std::endl; // Expected
	allocator.free(a5);
	allocator.free(a1);
	allocator.free(a3);
	allocator.free(a4);
	std::cout << allocator << std::endl; // Expected |32|64|0|128|
	allocator.free(a2);
	std::cout << allocator << std::endl; // Expected |96|32|64|0|128|
}

void testThreadedPA() {
	RIN::PoolAllocator allocator(100, 1);

	auto work = [&allocator](uint64_t size) {
		using namespace std::chrono_literals;
		for(uint64_t i = 0; i < size; ++i) {
			std::this_thread::sleep_for(0.01ms);
			auto a1 = allocator.allocate();
			std::this_thread::sleep_for(0.01ms);
			allocator.allocate();
			std::this_thread::sleep_for(0.01ms);
			allocator.free(a1);
		}
	};

	std::cout << allocator << std::endl; // Expected |0|1|2|3|...|99|

	std::thread t1(work, 10);
	std::thread t2(work, 20);
	std::thread t3(work, 30);
	std::thread t4(work, 20);
	t1.join();
	t2.join();
	t3.join();
	t4.join();

	std::cout << allocator << std::endl; // Expected 20 elemements
}

void testBumpAllocator() {
	RIN::BumpAllocator allocator(1000);
	std::cout << allocator << std::endl; // Expected 0/1000
	auto a1 = allocator.allocate(1000);
	std::cout << allocator << std::endl; // Expected 1000/1000
	a1 = allocator.allocate(10);
	if(a1) std::cout << "Allocated 10 even though allocator is full" << std::endl;
	else std::cout << "Failed to allocate 10 because allocator us full" << std::endl; // Expected
	allocator.free();
	std::cout << allocator << std::endl; // Expected 0/1000
	allocator.allocate(13);
	std::cout << allocator << std::endl; // Expected 13/1000
	allocator.allocate(57);
	allocator.allocate(30);
	std::cout << allocator << std::endl; // Expected 100/1000
	allocator.free();
	std::cout << allocator << std::endl; // Expected 0/1000
}

void testThreadedBA() {
	RIN::BumpAllocator allocator(1000);

	auto work = [&allocator]() {
		using namespace std::chrono_literals;
		for(uint32_t i = 0; i < 20; ++i)
			allocator.allocate(5);
		allocator.free();
		std::this_thread::sleep_for(1ms);
		for(uint32_t i = 0; i < 100; ++i) // Let it run out of space
			allocator.allocate(3);
	};

	std::cout << allocator << std::endl; // Expected 0/1000

	std::thread t1(work);
	std::thread t2(work);
	std::thread t3(work);
	std::thread t4(work);
	std::thread t5(work);
	t1.join();
	t2.join();
	t3.join();
	t4.join();
	t5.join();

	std::cout << allocator << std::endl; // Expected 999/1000
}