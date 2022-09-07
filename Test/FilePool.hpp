#pragma once

#include <ThreadPool.hpp>

class FilePool {
	RIN::ThreadPool threadPool;
public:
	class File {
		friend FilePool;

		uint64_t _size = 0;
		const char* _data = nullptr;
	public:
		File() = default;
		File(const File& other);
		File(File&& other) noexcept;
		~File();
		File& operator=(const File& other);
		File& operator=(File&& other) noexcept;
		uint64_t size() const;
		const char* data() const;
		bool ready() const;
		operator bool() const;
		void wait() const;
		void close();
	};

	FilePool() = default;
	FilePool(const FilePool&) = delete;
	~FilePool() = default;
	void readFile(const char* fileName, File& fileRef);
	void wait();
};