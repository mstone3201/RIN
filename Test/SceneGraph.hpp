#pragma once

#include <unordered_set>
#include <vector>
#include <Renderer.hpp>
#include <Pool.hpp>

class SceneGraph {
public:
	class Node {
		friend SceneGraph;
		friend void updateChildren(Node*);

		alignas(16) DirectX::XMMATRIX localMatrix;
		std::vector<Node*> children;
		Node* parent;
		RIN::DynamicObject* object;
		bool matrixDirty;
	public:
		Node(Node* parent, RIN::DynamicObject* object);
		void XM_CALLCONV setTansform(DirectX::FXMMATRIX localMatrix);
		DirectX::XMMATRIX getTransform() const;
	};

	static constexpr Node* ROOT_NODE = nullptr;
private:
	RIN::DynamicPool<Node> nodePool;
	std::unordered_set<Node*> rootChildren;
public:
	SceneGraph(uint32_t objectCount);
	SceneGraph(const SceneGraph&) = delete;
	Node* addNode(Node* parent, RIN::DynamicObject* object);
	void removeNode(Node* node);
	void update();
};