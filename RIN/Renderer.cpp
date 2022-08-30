#include "Renderer.hpp"
#include "Error.hpp"
#include "Debug.hpp"
#include "D3D12Renderer.hpp"

namespace RIN {

	Renderer::Renderer(const Config& config, const Settings& settings) :
		config(config),
		settings(settings)
	{
		// Validation
		if(!config.uploadStreamSize) RIN_ERROR("Upload stream size must not be 0");
		if(!config.staticVertexCount) RIN_ERROR("Static vertex count must not be 0");
		if(!config.staticIndexCount) RIN_ERROR("Static index count must not be 0");
		if(!config.staticMeshCount) RIN_ERROR("Static mesh count must not be 0");
		if(!config.staticObjectCount) RIN_ERROR("Static object count must not be 0");
		if(!config.dynamicVertexCount) RIN_ERROR("Dynamic vertex count must not be 0");
		if(!config.dynamicIndexCount) RIN_ERROR("Dynamic index count must not be 0");
		if(!config.dynamicMeshCount) RIN_ERROR("Dynamic mesh count must not be 0");
		if(!config.dynamicObjectCount) RIN_ERROR("Dynamic object count must not be 0");
		if(!config.texturesSize) RIN_ERROR("Textures size must not be 0");
		if(!config.textureCount) RIN_ERROR("Texture count must not be 0");
		if(!config.materialCount) RIN_ERROR("Material count must not be 0");
		if(!config.lightCount) RIN_ERROR("Light count must not be 0");

		if(!settings.backBufferWidth) RIN_ERROR("Back buffer width must not be 0");
		if(!settings.backBufferHeight) RIN_ERROR("Back buffer height must not be 0");
		if(!settings.backBufferCount) RIN_ERROR("Back buffer count must not be 0");
	}

	Renderer* Renderer::create(HWND hwnd, const Config& config, const Settings& settings) {
		Renderer* renderer = nullptr;

		switch(config.engine) {
		case RENDER_ENGINE::D3D12:
			renderer = new D3D12Renderer(hwnd, config, settings);

			RIN_DEBUG_INFO("Created D3D12 renderer");

			break;
		}

		return renderer;
	}

	void Renderer::destroy(Renderer* renderer) noexcept {
		if(renderer) delete renderer;
	}

	const Settings& Renderer::getSettings() const {
		return settings;
	}
}