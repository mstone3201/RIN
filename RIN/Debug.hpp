#pragma once

#include <iostream>

#include "Error.hpp"

//#define RIN_DEBUG

#ifdef RIN_DEBUG
#define RIN_DEBUG_ERROR(message) std::cout << "RIN ERROR:\t" << (message) << std::endl
#define RIN_DEBUG_INFO(message) std::cout << "RIN INFO:\t" << (message) << std::endl
#define RIN_ERROR(message) RIN_DEBUG_ERROR(message), throw RIN::Error(message)
#define RIN_DEBUG_NAME(object, name) object->SetName(L#name)
#else
#define RIN_DEBUG_ERROR(message)
#define RIN_DEBUG_INFO(message)
#define RIN_ERROR(message) throw RIN::Error(message)
#define RIN_DEBUG_NAME(object, name)
#endif