#pragma once

#include <cstdint>

namespace RIN {
	enum class TEXTURE_TYPE : uint32_t {
		TEXTURE_2D,
		TEXTURE_CUBE
	};

	enum class TEXTURE_FORMAT : uint32_t {
		R8_UNORM,
		R16_FLOAT,
		R32_FLOAT,
		R8G8_UNORM,
		R16G16_FLOAT,
		R32G32_FLOAT,
		R8G8B8A8_UNORM,
		R8G8B8A8_UNORM_SRGB,
		B8G8R8A8_UNORM,
		B8G8R8A8_UNORM_SRGB,
		R16B16G16A16_FLOAT,
		R32G32B32A32_FLOAT,
		BC3_UNORM, // Block compressed RGBA
		BC3_UNORM_SRGB, // Block compressed RGBA
		BC4_UNORM, // Block compressed R
		BC5_UNORM, // Block compressed RG
		BC6H_FLOAT, // Block compressed RGB
		BC7_UNORM, // Block compressed RGBA
		BC7_UNORM_SRGB // Block compressed RGBA
	};

	class Texture {
	protected:
		bool _resident = false;

		Texture(TEXTURE_TYPE type, TEXTURE_FORMAT format) :
			type(type),
			format(format)
		{}

		Texture(const Texture&) = delete;
		virtual ~Texture() = 0;
	public:
		const TEXTURE_TYPE type;
		const TEXTURE_FORMAT format;

		// Check this to determine if it is safe to free the
		// buffers used to create the texture
		bool resident() const {
			return _resident;
		}

		static uint64_t getRowPitch(uint32_t width, TEXTURE_FORMAT format) {
			// Pitch calculations
			// https://docs.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide

			switch(format) {
			case TEXTURE_FORMAT::R8_UNORM:
				return width;
			case TEXTURE_FORMAT::R16_FLOAT:
			case TEXTURE_FORMAT::R8G8_UNORM:
				return (uint64_t)width * 2;
			case TEXTURE_FORMAT::R32_FLOAT:
			case TEXTURE_FORMAT::R16G16_FLOAT:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM_SRGB:
				return (uint64_t)width * 4;
			case TEXTURE_FORMAT::R32G32_FLOAT:
			case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
				return (uint64_t)width * 8;
			case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
				return (uint64_t)width * 16;
			case TEXTURE_FORMAT::BC3_UNORM:
			case TEXTURE_FORMAT::BC3_UNORM_SRGB:
			case TEXTURE_FORMAT::BC5_UNORM:
			case TEXTURE_FORMAT::BC6H_FLOAT:
			case TEXTURE_FORMAT::BC7_UNORM:
			case TEXTURE_FORMAT::BC7_UNORM_SRGB:
				return std::max((uint64_t)1, ((uint64_t)width + 3) / 4) * 16;
			case TEXTURE_FORMAT::BC4_UNORM:
				return std::max((uint64_t)1, ((uint64_t)width + 3) / 4) * 8;
			}
			return 0;
		}

		static uint32_t getRowCount(uint32_t height, TEXTURE_FORMAT format) {
			switch(format) {
			case TEXTURE_FORMAT::R8_UNORM:
			case TEXTURE_FORMAT::R16_FLOAT:
			case TEXTURE_FORMAT::R32_FLOAT:
			case TEXTURE_FORMAT::R8G8_UNORM:
			case TEXTURE_FORMAT::R16G16_FLOAT:
			case TEXTURE_FORMAT::R32G32_FLOAT:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
			case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
				return height;
			case TEXTURE_FORMAT::BC3_UNORM:
			case TEXTURE_FORMAT::BC3_UNORM_SRGB:
			case TEXTURE_FORMAT::BC4_UNORM:
			case TEXTURE_FORMAT::BC5_UNORM:
			case TEXTURE_FORMAT::BC6H_FLOAT:
			case TEXTURE_FORMAT::BC7_UNORM:
			case TEXTURE_FORMAT::BC7_UNORM_SRGB:
				return (height + 3) / 4;
			}
			return 0;
		}

		// To allow for future ASTC support
		static uint32_t getBlockWidth(TEXTURE_FORMAT format) {
			switch(format) {
			case TEXTURE_FORMAT::R8_UNORM:
			case TEXTURE_FORMAT::R16_FLOAT:
			case TEXTURE_FORMAT::R32_FLOAT:
			case TEXTURE_FORMAT::R8G8_UNORM:
			case TEXTURE_FORMAT::R16G16_FLOAT:
			case TEXTURE_FORMAT::R32G32_FLOAT:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
			case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
				return 1;
			case TEXTURE_FORMAT::BC3_UNORM:
			case TEXTURE_FORMAT::BC3_UNORM_SRGB:
			case TEXTURE_FORMAT::BC4_UNORM:
			case TEXTURE_FORMAT::BC5_UNORM:
			case TEXTURE_FORMAT::BC6H_FLOAT:
			case TEXTURE_FORMAT::BC7_UNORM:
			case TEXTURE_FORMAT::BC7_UNORM_SRGB:
				return 4;
			}
			return 0;
		}

		// To allow for future ASTC support
		static uint32_t getBlockHeight(TEXTURE_FORMAT format) {
			switch(format) {
			case TEXTURE_FORMAT::R8_UNORM:
			case TEXTURE_FORMAT::R16_FLOAT:
			case TEXTURE_FORMAT::R32_FLOAT:
			case TEXTURE_FORMAT::R8G8_UNORM:
			case TEXTURE_FORMAT::R16G16_FLOAT:
			case TEXTURE_FORMAT::R32G32_FLOAT:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
			case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
				return 1;
			case TEXTURE_FORMAT::BC3_UNORM:
			case TEXTURE_FORMAT::BC3_UNORM_SRGB:
			case TEXTURE_FORMAT::BC4_UNORM:
			case TEXTURE_FORMAT::BC5_UNORM:
			case TEXTURE_FORMAT::BC6H_FLOAT:
			case TEXTURE_FORMAT::BC7_UNORM:
			case TEXTURE_FORMAT::BC7_UNORM_SRGB:
				return 4;
			}
			return 0;
		}

		uint64_t getRowPitch(uint32_t width) {
			return getRowPitch(width, format);
		}

		uint32_t getRowCount(uint32_t height) {
			return getRowCount(height, format);
		}

		uint32_t getBlockWidth() {
			return getBlockWidth(format);
		}

		uint32_t getBlockHeight() {
			return getBlockHeight(format);
		}
	};

	// inline allows the definition to reside here even if it included multiple times
	inline Texture::~Texture() {}
}