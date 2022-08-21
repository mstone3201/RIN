#pragma once

#include <Pool.hpp>

void testStaticPool() {
	RIN::StaticPool<double, 6> pool;
	std::cout << pool << std::endl; // Expected | | | | | | |
	auto a1 = pool.insert();
	std::cout << pool << std::endl; // Expected |0| | | | | |
	auto a2 = pool.insert(4.67);
	std::cout << pool << std::endl; // Expected |0|4.67| | | | |
	auto a3 = pool.insert();
	pool.remove(a2);
	std::cout << pool << std::endl; // Expected |0| |0| | | |
	auto a4 = pool.insert(1.22);
	std::cout << pool << std::endl; // Expected |0|1.22|0| | | |
	std::cout << *(pool.at(1)) << std::endl; // Expected 1.22
	pool.remove(a4);
	pool.remove(a1);
	pool.remove(a3);
	if(pool.at(0)) std::cout << "ERROR" << std::endl;
	std::cout << pool << std::endl; // Expected | | | | | | |
}

void testDynamicPool() {
	// Test non-trivial types
	RIN::DynamicPool<double> pool(6);
	std::cout << pool << std::endl; // Expected | | | | | | |
	auto a1 = pool.insert();
	std::cout << pool << std::endl; // Expected |0| | | | | |
	auto a2 = pool.insert(4.67);
	std::cout << pool << std::endl; // Expected |0|4.67| | | | |
	auto a3 = pool.insert();
	pool.remove(a2);
	std::cout << pool << std::endl; // Expected |0| |0| | | |
	auto a4 = pool.insert(1.22);
	std::cout << pool << std::endl; // Expected |0|1.22|0| | | |
	std::cout << *(pool.at(1)) << std::endl; // Expected 1.22
	pool.remove(a4);
	pool.remove(a1);
	pool.remove(a3);
	if(pool.at(0)) std::cout << "ERROR" << std::endl;
	std::cout << pool << std::endl; // Expected | | | | | | |

	struct Test { // 40 bits, alignment of 4, size of 8
		uint32_t a;
		uint8_t b;
		Test() : a(0), b(0) {}
		Test(uint32_t a) : a(a), b(0) {}
		Test(uint32_t a, uint32_t b) : a(a), b(b) {}
		~Test() { std::cout << "Destroyed" << std::endl; }
	};

	RIN::DynamicPool<Test> structPool(13);
	auto x = structPool.insert(13, 2);
	auto y = structPool.insert(5);
	std::cout << "X: " << x->a << ", " << (uint32_t)(x->b) << std::endl; // Expected 13, 2
	std::cout << "Y: " << y->a << ", " << (uint32_t)(y->b) << std::endl; // Expected 5, 0
	structPool.remove(x); // Expected Destroyed
	x = structPool.insert();
	std::cout << "X: " << x->a << ", " << (uint32_t)(x->b) << std::endl; // Expected 0, 0
	// Expected Destroyed
	// Expected Destroyed
}

struct Float {
	float x;
	Float() : x(0) {}
	Float(float x) : x(x) {}
	~Float() { std::cout << "Float destroyed" << std::endl; }
};

void testDynamicPoolSpecialization() {
	RIN::UntaggedDynamicPool<float> a(5);
	RIN::DynamicPool<Float> b(5);

	auto x1 = a.insert(5.1f);
	auto y1 = b.insert(5.1f);
	std::cout << *x1 << " | " << y1->x << std::endl; // Expected 5.1 | 5.1
	auto x2 = a.insert();
	auto y2 = b.insert();
	std::cout << *x2 << " | " << y2->x << std::endl; // Expected 0 | 0
	std::cout << a.getIndex(x2) << " | " << b.getIndex(y2) << std::endl; // Expected 1 | 1
	a.remove(x1);
	a.insert(0.1f);
	b.remove(y1);
	b.insert(0.1f);
	// Expected Float destroyed
	std::cout << *a.at(0) << " | " << b.at(0)->x << std::endl; // Expected 0.1 | 0.1
	// Expected Float destroyed
	// Expected Float destroyed
}