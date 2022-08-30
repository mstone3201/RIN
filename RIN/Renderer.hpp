#pragma once

#define NOMINMAX

#include <Windows.h>

#include "Config.hpp"
#include "Settings.hpp"
#include "Camera.hpp"
#include "VertexData.hpp"
#include "StaticMesh.hpp"
#include "StaticObject.hpp"
#include "DynamicMesh.hpp"
#include "DynamicObject.hpp"
#include "Texture.hpp"
#include "Material.hpp"
#include "Light.hpp"

/*
Using DirectXMath

Calling conventions explanation:
https://docs.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-internals

XMVECTOR and XMMATRIX types must be 16-byte aligned
They are automatically 16-byte aligned on the stack,
but when placed on the heap, this must be done manually
XMVECTOR and XMMATRIX class/struct members should be
prefixed with alignas(16) to ensure that they are
aligned properly (for instance, struct { float; XMVECTOR; }
does not have the XMVECTOR aligned properly)

Matrices are row-major row vector matrices (x * A)
Transforms use pre-multiplication (left to right transform order)

In the shaders, matrices are column-major column vector matrices (A * x)

When using DirectXMath, no special treatment needs to be given
to the matrices; the change from row-major to column-major acts
as a transpose, and x * A = A^T * x

As long as matrices on the CPU are treated as either row-major row
vector matrices (DirectXMath) or column-major column vector matrices
(some alternative linear algebra libraries), and matrices on the GPU
are treated as column-major column vector matrices, then there will
be no issues
*/

// See: https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-syskeydown
// lParam bit 29 is 1 if ALT key is currently down
#define IS_ALT_ENTER(wParam, lParam) ((wParam) == VK_RETURN && (lParam) & ((LPARAM)1 << 29))

#define ALIGN_TO(x, alignment) (((x) + (alignment) - 1) / (alignment) * (alignment))

namespace RIN {
	typedef uint32_t index_type;

	/*
	update() must only be called after construction or after render()
	render() must only be called after update()
	
	Thread Safety:
	Renderer::create is not thread-safe
	Renderer::destroy is not thread-safe
	Renderer::getCamera is not thread-safe
	Renderer::addStaticMesh is thread-safe
	Renderer::removeStaticMesh is thread-safe
	Renderer::addStaticObject is thread-safe
	Renderer::removeStaticObject is thread-safe
	Renderer::updateStaticObject is thread-safe
	Renderer::addDynamicMesh is thread-safe
	Renderer::removeDynamicMesh is thread-safe
	Renderer::addDynamicObject is thread-safe
	Renderer::removeDynamicObject is thread-safe
	Renderer::addTexture is thread-safe
	Renderer::removeTexture is thread-safe
	Renderer::addMaterial is thread-safe
	Renderer::removeMaterial is thread-safe
	Renderer::addLight is thread-safe
	Renderer::removeLight is thread-safe
	Renderer::setSkybox is not thread-safe
	Renderer::setBRDFLUT is not thread-safe
	Renderer::update is not thread-safe
	Renderer::render is not thread-safe
	Renderer::resizeSwapChain is not thread-safe
	Renderer::toggleFullScreen is not thread-safe
	Renderer::showWindow is not thread-safe
	Renderer::getSettings is not thread-safe
	Renderer::applySettings is not thread-safe
	*/
	class Renderer {
	protected:
		Settings settings;

		// Initialization
		Renderer(const Config& config, const Settings& settings);
		Renderer(const Renderer&) = delete;
		virtual ~Renderer() = default;
	public:
		const Config config;

		// Initialization
		static Renderer* create(HWND hwnd, const Config& config, const Settings& settings);
		static void destroy(Renderer* renderer) noexcept;

		// Resource uploading
		// Scene
		virtual Camera& getCamera() = 0;
		virtual const Camera& getCamera() const = 0;
		virtual StaticMesh* addStaticMesh(
			const BoundingSphere& boundingSphere,
			const StaticVertex* vertices,
			const uint32_t* vertexCounts,
			const index_type* indices,
			const uint32_t* indexCounts,
			uint32_t lodCount
		) = 0;
		virtual void removeStaticMesh(StaticMesh* mesh) = 0;
		virtual StaticObject* addStaticObject(StaticMesh* mesh, Material* material) = 0;
		virtual void removeStaticObject(StaticObject* object) = 0;
		virtual void updateStaticObject(StaticObject* object) = 0;
		virtual DynamicMesh* addDynamicMesh(
			const BoundingSphere& boundingSphere,
			const DynamicVertex* vertices,
			const uint32_t* vertexCounts,
			const index_type* indices,
			const uint32_t* indexCounts,
			uint32_t lodCount
		) = 0;
		virtual void removeDynamicMesh(DynamicMesh* mesh) = 0;
		virtual DynamicObject* addDynamicObject(DynamicMesh* mesh, Material* material) = 0;
		virtual void removeDynamicObject(DynamicObject* object) = 0;
		// Setting mipCount to -1 will use the full mip chain
		virtual Texture* addTexture(
			TEXTURE_TYPE type,
			TEXTURE_FORMAT format,
			uint32_t width,
			uint32_t height,
			uint32_t mipCount,
			const char* textureData
		) = 0;
		virtual void removeTexture(Texture* texture) = 0;
		virtual Material* addMaterial(
			MATERIAL_TYPE type,
			Texture* baseColor,
			Texture* normal,
			Texture* roughnessAO,
			Texture* metallic,
			Texture* height,
			Texture* special = nullptr
		) = 0;
		virtual void removeMaterial(Material* material) = 0;
		virtual Light* addLight() = 0;
		virtual void removeLight(Light*) = 0;
		virtual void setSkybox(Texture* skybox, Texture* diffuseIBL, Texture* specularIBL) = 0;
		virtual void setBRDFLUT(Texture* texture) = 0;
		// GUI
		// Update and commit upload
		virtual void update() = 0;

		// Rendering
		virtual void render() = 0;
		virtual void resizeSwapChain() = 0;
		virtual void toggleFullScreen() = 0;
		virtual void showWindow() = 0;

		// Settings
		const Settings& getSettings() const;
		virtual void applySettings(const Settings& settings) = 0;
	};
}