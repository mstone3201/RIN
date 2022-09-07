#pragma once

#include <cstdint>

#include "Texture.hpp"
#include "Debug.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	enum class MATERIAL_TYPE : uint32_t {
		PBR_STANDARD,
		PBR_EMISSIVE,
		PBR_CLEAR_COAT,
		PBR_SHEEN
	};

	class Material {
		friend class D3D12Renderer;
		friend class DynamicPool<Material>;
	protected:
		MATERIAL_TYPE type;
		Texture* baseColor;
		Texture* normal;
		Texture* roughnessAO;
		Texture* metallic;
		Texture* height;
		Texture* special;

		Material(
			MATERIAL_TYPE type,
			Texture* baseColor,
			Texture* normal,
			Texture* roughnessAO,
			Texture* metallic,
			Texture* height,
			Texture* special = nullptr
		) {
			setMaterial(type, baseColor, normal, roughnessAO, metallic, height, special);
		}

		Material(const Material&) = delete;
		virtual ~Material() = default;
	public:
		virtual void setMaterial(
			MATERIAL_TYPE type,
			Texture* baseColor,
			Texture* normal,
			Texture* roughnessAO,
			Texture* metallic,
			Texture* height,
			Texture* special = nullptr
		) {
			// Validation
			if(!baseColor || !normal || !roughnessAO || !metallic || !height) RIN_ERROR("Materials require base color, normal, roughness AO, metallic, and height textures");
			
			if(baseColor->type != TEXTURE_TYPE::TEXTURE_2D) RIN_ERROR("Material base color texture must be type texture 2D");
			if(normal->type != TEXTURE_TYPE::TEXTURE_2D) RIN_ERROR("Material normal texture must be type texture 2D");
			if(roughnessAO->type != TEXTURE_TYPE::TEXTURE_2D) RIN_ERROR("Material roughness AO texture must be type texture 2D");
			if(metallic->type != TEXTURE_TYPE::TEXTURE_2D) RIN_ERROR("Material metallic texture must be type texture 2D");
			if(height->type != TEXTURE_TYPE::TEXTURE_2D) RIN_ERROR("Material height texture must be type texture 2D");
			if(special && special->type != TEXTURE_TYPE::TEXTURE_2D) RIN_ERROR("Material special texture must be type texture 2D");

			switch(baseColor->format) {
			case TEXTURE_FORMAT::R8G8B8A8_UNORM:
			case TEXTURE_FORMAT::R8G8B8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM:
			case TEXTURE_FORMAT::B8G8R8A8_UNORM_SRGB:
			case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
			case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
			case TEXTURE_FORMAT::BC3_UNORM:
			case TEXTURE_FORMAT::BC3_UNORM_SRGB:
			case TEXTURE_FORMAT::BC7_UNORM:
			case TEXTURE_FORMAT::BC7_UNORM_SRGB:
				break;
			default:
				RIN_ERROR("Invalid material base color texture format");
			}

			switch(normal->format) {
			case TEXTURE_FORMAT::R8G8_UNORM:
			case TEXTURE_FORMAT::R16G16_FLOAT:
			case TEXTURE_FORMAT::R32G32_FLOAT:
			case TEXTURE_FORMAT::BC5_UNORM:
				break;
			default:
				RIN_ERROR("Invalid material normal texture format");
			}

			switch(roughnessAO->format) {
			case TEXTURE_FORMAT::R8G8_UNORM:
			case TEXTURE_FORMAT::R16G16_FLOAT:
			case TEXTURE_FORMAT::R32G32_FLOAT:
			case TEXTURE_FORMAT::BC5_UNORM:
				break;
			default:
				RIN_ERROR("Invalid material roughness AO texture format");
			}

			switch(metallic->format) {
			case TEXTURE_FORMAT::R8_UNORM:
			case TEXTURE_FORMAT::R16_FLOAT:
			case TEXTURE_FORMAT::R32_FLOAT:
			case TEXTURE_FORMAT::BC4_UNORM:
				break;
			default:
				RIN_ERROR("Invalid material metallic texture format");
			}

			switch(height->format) {
			case TEXTURE_FORMAT::R8_UNORM:
			case TEXTURE_FORMAT::R16_FLOAT:
			case TEXTURE_FORMAT::R32_FLOAT:
			case TEXTURE_FORMAT::BC4_UNORM:
				break;
			default:
				RIN_ERROR("Invalid material height texture format");
			}

			switch(type) {
			case MATERIAL_TYPE::PBR_STANDARD:
				if(special) RIN_ERROR("Standard PBR material does not support a special texture");
				break;
			case MATERIAL_TYPE::PBR_EMISSIVE:
				if(!special) RIN_ERROR("emissive PBR material requires a special texture");
				switch(special->format) {
				case TEXTURE_FORMAT::R8G8B8A8_UNORM:
				case TEXTURE_FORMAT::R8G8B8A8_UNORM_SRGB:
				case TEXTURE_FORMAT::B8G8R8A8_UNORM:
				case TEXTURE_FORMAT::B8G8R8A8_UNORM_SRGB:
				case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
				case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
				case TEXTURE_FORMAT::BC3_UNORM:
				case TEXTURE_FORMAT::BC3_UNORM_SRGB:
				case TEXTURE_FORMAT::BC7_UNORM:
				case TEXTURE_FORMAT::BC7_UNORM_SRGB:
					break;
				default:
					RIN_ERROR("Invalid emissive material special texture format");
				}
				break;
			case MATERIAL_TYPE::PBR_CLEAR_COAT:
			case MATERIAL_TYPE::PBR_SHEEN:
				if(!special) RIN_ERROR("Clear coat and sheen PBR materials require a special texture");
				switch(special->format) {
				case TEXTURE_FORMAT::R8G8_UNORM:
				case TEXTURE_FORMAT::R16G16_FLOAT:
				case TEXTURE_FORMAT::R32G32_FLOAT:
				case TEXTURE_FORMAT::BC5_UNORM:
					break;
				default:
					RIN_ERROR("Invalid clear coat or sheen material special texture format");
				}
				break;
			}

			Material::type = type;
			Material::baseColor = baseColor;
			Material::normal = normal;
			Material::roughnessAO = roughnessAO;
			Material::metallic = metallic;
			Material::height = height;
			Material::special = special;
		}

		bool resident() const {
			return
				baseColor->resident() &&
				normal->resident() &&
				roughnessAO->resident() &&
				metallic->resident() &&
				height->resident() &&
				(special ? special->resident() : true);
		}
	};
}