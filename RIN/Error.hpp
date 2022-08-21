#pragma once

#include <stdexcept>

namespace RIN {
	class Error : public std::runtime_error {
	public:
		Error(std::string message) : std::runtime_error(message) {}
	};
}