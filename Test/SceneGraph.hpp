#pragma once

#include <unordered_set>
#include <vector>
#include <Renderer.hpp>
#include <Pool.hpp>
#include <ThreadPool.hpp>

class SceneGraph {
public:
	class Node {
		friend SceneGraph;
		friend void updateChildren(Node*);
	protected:
		alignas(16) DirectX::XMMATRIX localMatrix = DirectX::XMMatrixIdentity();
		std::vector<Node*> children;
		Node* parent;
		bool matrixDirty = false;

		Node(Node* parent);
		Node(const Node&) = delete;
		virtual ~Node() = default;

		virtual void updateWorldMatrix() = 0;
		virtual void XM_CALLCONV updateWorldMatrix(DirectX::FXMMATRIX parentMatrix) = 0;
		virtual void remove(SceneGraph& sceneGraph) = 0;
	public:
		virtual void XM_CALLCONV setTansform(DirectX::FXMMATRIX localMatrix);
		DirectX::XMMATRIX getTransform() const;
		virtual DirectX::XMMATRIX getWorldMatrix() const = 0;
	};

	class DynamicObjectNode : public Node {
		friend class RIN::DynamicPool<DynamicObjectNode>;

		RIN::DynamicObject* object;

		DynamicObjectNode(Node* parent, RIN::DynamicObject* object);
		DynamicObjectNode(const DynamicObjectNode&) = delete;
		~DynamicObjectNode() = default;

		void updateWorldMatrix() override;
		void XM_CALLCONV updateWorldMatrix(DirectX::FXMMATRIX parentMatrix) override;
		void remove(SceneGraph& sceneGraph) override;
	public:
		DirectX::XMMATRIX getWorldMatrix() const override;
	};

	class LightNode : public Node {
		friend class RIN::DynamicPool<LightNode>;

		alignas(16) DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();
		RIN::Light* light;

		LightNode(Node* parent, RIN::Light* light);
		LightNode(const LightNode&) = delete;
		~LightNode() = default;

		void updateWorldMatrix() override;
		void XM_CALLCONV updateWorldMatrix(DirectX::FXMMATRIX parentMatrix) override;
		void remove(SceneGraph& sceneGraph) override;
	public:
		DirectX::XMMATRIX getWorldMatrix() const override;
	};

	/*
	BoneNode::setTransform transforms the bone in pose space, starting with it at its
	rest position rotations may not yield the desired results, since the bone will be
	rotating around the origin, which is not centered on the bone
	BoneNode::setBoneSpaceTransform transforms the bone as if it was at the origin
	*/
	class BoneNode : public Node {
		friend class RIN::DynamicPool<BoneNode>;

		alignas(16) DirectX::XMMATRIX restMatrix;
		alignas(16) DirectX::XMMATRIX invRestMatrix;
		RIN::Bone* bone;
		bool boneSpace = false;

		BoneNode(Node* parent, RIN::Bone* bone, DirectX::CXMMATRIX restMatrix);
		BoneNode(const BoneNode&) = delete;
		~BoneNode() = default;

		void updateWorldMatrix() override;
		void XM_CALLCONV updateWorldMatrix(DirectX::FXMMATRIX parentMatrix) override;
		void remove(SceneGraph& sceneGraph) override;
	public:
		// Transforms the bone in pose space
		void XM_CALLCONV setTansform(DirectX::FXMMATRIX localMatrix);
		// Transforms the bone as if its tail was at the origin
		void XM_CALLCONV setBoneSpaceTansform(DirectX::FXMMATRIX localMatrix);
		DirectX::XMMATRIX getWorldMatrix() const override;
	};

	static constexpr Node* ROOT_NODE = nullptr;
private:
	RIN::ThreadPool threadPool;
	RIN::DynamicPool<DynamicObjectNode> dynamicObjectNodePool;
	RIN::DynamicPool<LightNode> lightNodePool;
	RIN::DynamicPool<BoneNode> boneNodePool;
	std::unordered_set<Node*> rootChildren;
public:
	SceneGraph(uint32_t dynamicObjectCount, uint32_t lightCount, uint32_t boneCount);
	SceneGraph(const SceneGraph&) = delete;
	DynamicObjectNode* addNode(Node* parent, RIN::DynamicObject* object);
	LightNode* addNode(Node* parent, RIN::Light* light);
	BoneNode* XM_CALLCONV addNode(Node* parent, RIN::Bone* bone, DirectX::FXMMATRIX restMatrix = DirectX::XMMatrixIdentity());
	void removeNode(Node* node);
	void update();
};