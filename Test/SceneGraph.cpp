#include "SceneGraph.hpp"

#include <algorithm>

SceneGraph::Node::Node(Node* parent, RIN::DynamicObject* object) :
	localMatrix(DirectX::XMMatrixIdentity()),
	parent(parent),
	object(object),
	matrixDirty(false)
{}

void XM_CALLCONV SceneGraph::Node::setTansform(DirectX::FXMMATRIX M) {
	localMatrix = M;
	matrixDirty = true;
}

DirectX::XMMATRIX SceneGraph::Node::getTransform() const {
	return localMatrix;
}

SceneGraph::SceneGraph(uint32_t objectCount) :
	nodePool(objectCount)
{}

SceneGraph::Node* SceneGraph::addNode(Node* parent, RIN::DynamicObject* object) {
	if(!object) return nullptr;

	Node* node = nodePool.insert(parent, object);

	if(parent) parent->children.emplace_back(node);
	else rootChildren.emplace(node);

	return node;
}

void SceneGraph::removeNode(Node* node) {
	if(!node) return;

	Node* parent = node->parent;
	if(parent) {
		auto& children = parent->children;
		auto it = std::find(children.begin(), children.end(), node);
		if(it != children.end())
			children.erase(it);
		else throw std::runtime_error("Scene Graph node anomaly");
	} else rootChildren.erase(node);

	nodePool.remove(node);
}

/*
Helper recursive function for update
The parent is checked to be resident before being passed in
*/
void updateChildren(SceneGraph::Node* parent) {
	DirectX::XMMATRIX parentMatrix = parent->object->getWorldMatrix();

	bool parentDirty = parent->matrixDirty;
	parent->matrixDirty = false;

	for(auto child : parent->children) {
		auto object = child->object;
		if(parentDirty)
			child->matrixDirty = true;

		if(child->matrixDirty) object->setWorldMatrix(DirectX::XMMatrixMultiply(child->localMatrix, parentMatrix));

		updateChildren(child);
	}
}

void SceneGraph::update() {
	for(auto node : rootChildren) {
		auto object = node->object;
		if(node->matrixDirty) object->setWorldMatrix(node->localMatrix);
		updateChildren(node);
	}
}