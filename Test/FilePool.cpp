#include "FilePool.hpp"

#include <fstream>
#include <iostream>

FilePool::File::File() :
	_size(0),
	_data(nullptr)
{}

FilePool::File::File(const File& other) {
	_size = other._size;
	_data = new char[_size];

	memcpy((void*)_data, other._data, _size);
}

FilePool::File::File(File&& other) noexcept {
	_size = other._size;
	_data = other._data;

	other._size = 0;
	other._data = nullptr;
}

FilePool::File::~File() {
	if(_data) delete[] _data;
}

FilePool::File& FilePool::File::operator=(const File& other) {
	this->~File();

	_size = other._size;
	_data = new char[_size];

	memcpy((void*)_data, other._data, _size);

	return *this;
}

FilePool::File& FilePool::File::operator=(File&& other) noexcept {
	this->~File();

	_size = other._size;
	_data = other._data;

	other._size = 0;
	other._data = nullptr;

	return *this;
}

uint64_t FilePool::File::size() const {
	return _size;
}

const char* FilePool::File::data() const {
	return _data;
}

bool FilePool::File::ready() const {
	// Check if _data is valid since it will be updated last, which avoids the need for a mutex
	return _data != nullptr;
}

FilePool::File::operator bool() const {
	return ready();
}

// Blocks until this file's data is ready
void FilePool::File::wait() const {
	while(true) {
		if(ready()) return;

		std::this_thread::yield();
	}
}

void FilePool::File::close() {
	this->~File();

	_size = 0;
	_data = nullptr;
}

void FilePool::readFile(const char* fileName, File& fileRef) {
	// Close the file ref first to free up memory
	// and leave it in an empty state in case the lambda fails
	fileRef.close();

	threadPool.enqueueJob(
		[fileName, &fileRef]() {
			std::ifstream file(fileName, std::ios::binary | std::ios::ate);

			if(file.is_open()) {
				std::ifstream::pos_type size = file.tellg();
				if(size != std::ifstream::pos_type(-1)) {
					file.seekg(0);

					char* data = new char[size];
					file.read(data, size);

					fileRef._size = size;
					// Update this one last to avoid needing a mutex on ready()
					fileRef._data = data;
				}
			}

			// Destructor is called and file is closed
		}
	);
}

void FilePool::wait() {
	threadPool.wait();
}