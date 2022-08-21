#pragma once

#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

namespace RIN {
	/*
	Thread Safety:
	ThreadPool::enqueueJob is thread-safe
	ThreadPool::wait is thread-safe
	*/
	class ThreadPool {
	public:
		typedef std::function<void()> job_type;
		const uint32_t numThreads;
	private:
		bool terminate = false;
		std::thread* threads;
		bool* threadIdle; // True if idle, false if busy
		std::queue<job_type> jobs;
		std::mutex mutex;
		std::condition_variable condition;

		void work(uint32_t tid) {
			while(true) {
				job_type job;
				{
					// Critical section
					std::unique_lock<std::mutex> lock(mutex);
					condition.wait(lock,
						[this]() {
							return !jobs.empty() || terminate;
						}
					);

					// Exit condition
					if(terminate) return;

					// Dequeue job
					job = jobs.front();
					jobs.pop();

					// Mark this thread as busy
					// Do this under the mutex, so wait() doesn't see
					// an empty queue before the status is updated
					threadIdle[tid] = false;
				}

				job();

				// Mark this thread as idle
				threadIdle[tid] = true;
			}
		}
	public:
		ThreadPool() :
			numThreads(std::thread::hardware_concurrency()),
			threads(new std::thread[numThreads]),
			threadIdle(new bool[numThreads])
		{
			for(uint32_t i = 0; i < numThreads; ++i) {
				threads[i] = std::thread(&ThreadPool::work, this, i);
				threadIdle[i] = true;
			}
		}

		ThreadPool(const ThreadPool&) = delete;

		~ThreadPool() {
			{
				// Even though this is atomic, the condition
				// must be modified under the mutex
				std::lock_guard<std::mutex> lock(mutex);
				terminate = true;
			}
			// Condition variable notification should not be under the mutex
			condition.notify_all();

			for(uint32_t i = 0; i < numThreads; ++i)
				if(threads[i].joinable()) threads[i].join();
			delete[] threads;

			delete[] threadIdle;
		}

		void enqueueJob(const job_type& job) {
			{
				// Critical section
				std::lock_guard<std::mutex> lock(mutex);
				jobs.push(job);
			}
			// Condition variable notification should not be under the mutex
			condition.notify_one();
		}

		// Blocks until the thread pool has finished all of its jobs
		void wait() {
			while(true) {
				bool idle;
				{
					// Critical section
					std::lock_guard<std::mutex> lock(mutex);
					// We can't guarantee that queue::empty is atomic
					idle = jobs.empty();
				}

				for(uint32_t i = 0; i < numThreads; ++i)
					idle = idle && threadIdle[i];

				if(idle) return;

				std::this_thread::yield();
			}
		}
	};
}