#include "SceneGraph.hpp"

#include <algorithm>

SceneGraph::Node::Node(Node* parent) :
	parent(parent)
{}

void XM_CALLCONV SceneGraph::Node::setTansform(DirectX::FXMMATRIX M) {
	localMatrix = M;
	matrixDirty = true;
}

DirectX::XMMATRIX SceneGraph::Node::getTransform() const {
	return localMatrix;
}

SceneGraph::DynamicObjectNode::DynamicObjectNode(Node* parent, RIN::DynamicObject* object) :
	Node(parent),
	object(object)
{}

void SceneGraph::DynamicObjectNode::updateWorldMatrix() {
	object->setWorldMatrix(localMatrix);
}

void XM_CALLCONV SceneGraph::DynamicObjectNode::updateWorldMatrix(DirectX::FXMMATRIX parentMatrix) {
	object->setWorldMatrix(DirectX::XMMatrixMultiply(localMatrix, parentMatrix));
}

void SceneGraph::DynamicObjectNode::remove(SceneGraph& sceneGraph) {
	sceneGraph.dynamicObjectNodePool.remove(this);
}

DirectX::XMMATRIX SceneGraph::DynamicObjectNode::getWorldMatrix() const {
	return object->getWorldMatrix();
}

SceneGraph::LightNode::LightNode(Node* parent, RIN::Light* light) :
	Node(parent),
	light(light)
{}

void SceneGraph::LightNode::updateWorldMatrix() {
	worldMatrix = localMatrix;
	DirectX::XMStoreFloat3(&light->position, DirectX::XMVector4Transform({ 0.0f, 0.0f, 0.0f, 1.0f }, worldMatrix));
}

void XM_CALLCONV SceneGraph::LightNode::updateWorldMatrix(DirectX::FXMMATRIX parentMatrix) {
	worldMatrix = DirectX::XMMatrixMultiply(localMatrix, parentMatrix);
	DirectX::XMStoreFloat3(&light->position, DirectX::XMVector4Transform({ 0.0f, 0.0f, 0.0f, 1.0f }, worldMatrix));
}

void SceneGraph::LightNode::remove(SceneGraph& sceneGraph) {
	sceneGraph.lightNodePool.remove(this);
}

DirectX::XMMATRIX SceneGraph::LightNode::getWorldMatrix() const {
	return worldMatrix;
}

SceneGraph::BoneNode::BoneNode(Node* parent, RIN::Bone* bone, DirectX::CXMMATRIX restMatrix) :
	Node(parent),
	restMatrix(restMatrix),
	invRestMatrix(DirectX::XMMatrixInverse(nullptr, restMatrix)),
	bone(bone)
{}

void SceneGraph::BoneNode::updateWorldMatrix() {
	if(boneSpace) bone->setWorldMatrix(DirectX::XMMatrixMultiply(DirectX::XMMatrixMultiply(invRestMatrix, localMatrix), restMatrix));
	else bone->setWorldMatrix(localMatrix);
}

void XM_CALLCONV SceneGraph::BoneNode::updateWorldMatrix(DirectX::FXMMATRIX parentMatrix) {
	if(boneSpace) bone->setWorldMatrix(DirectX::XMMatrixMultiply(DirectX::XMMatrixMultiply(DirectX::XMMatrixMultiply(invRestMatrix, localMatrix), restMatrix), parentMatrix));
	else bone->setWorldMatrix(DirectX::XMMatrixMultiply(localMatrix, parentMatrix));
}

void SceneGraph::BoneNode::remove(SceneGraph& sceneGraph) {
	sceneGraph.boneNodePool.remove(this);
}

void XM_CALLCONV SceneGraph::BoneNode::setTansform(DirectX::FXMMATRIX M) {
	Node::setTansform(M);
	boneSpace = false;
}

void XM_CALLCONV SceneGraph::BoneNode::setBoneSpaceTansform(DirectX::FXMMATRIX M) {
	Node::setTansform(M);
	boneSpace = true;
}

DirectX::XMMATRIX SceneGraph::BoneNode::getWorldMatrix() const {
	return bone->getWorldMatrix();
}

SceneGraph::SceneGraph(uint32_t dynamicObjectCount, uint32_t lightCount, uint32_t boneCount) :
	dynamicObjectNodePool(dynamicObjectCount),
	lightNodePool(lightCount),
	boneNodePool(boneCount)
{}

SceneGraph::DynamicObjectNode* SceneGraph::addNode(Node* parent, RIN::DynamicObject* object) {
	if(!object) return nullptr;

	DynamicObjectNode* node = dynamicObjectNodePool.insert(parent, object);

	if(parent) parent->children.push_back(node);
	else rootChildren.insert(node);

	return node;
}

SceneGraph::LightNode* SceneGraph::addNode(Node* parent, RIN::Light* light) {
	if(!light) return nullptr;

	LightNode* node = lightNodePool.insert(parent, light);

	if(parent) parent->children.push_back(node);
	else rootChildren.insert(node);

	return node;
}

SceneGraph::BoneNode* XM_CALLCONV SceneGraph::addNode(Node* parent, RIN::Bone* bone, DirectX::FXMMATRIX restMatrix) {
	if(!bone) return nullptr;

	BoneNode* node = boneNodePool.insert(parent, bone, restMatrix);
	
	if(parent) parent->children.push_back(node);
	else rootChildren.insert(node);

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

	node->remove(*this);
}

/*
Helper recursive function for update
*/
void updateChildren(SceneGraph::Node* parent) {
	DirectX::XMMATRIX parentMatrix = parent->getWorldMatrix();

	bool parentDirty = parent->matrixDirty;
	parent->matrixDirty = false;

	for(auto child : parent->children) {
		if(parentDirty)
			child->matrixDirty = true;

		if(child->matrixDirty) child->updateWorldMatrix(parentMatrix);

		updateChildren(child);
	}
}

void SceneGraph::update() {
	uint32_t childrenCount = (uint32_t)rootChildren.size();
	uint32_t childrenPerThread = childrenCount / threadPool.numThreads;

	auto begin = rootChildren.begin();
	auto end = begin;
	for(uint32_t i = 0; i < threadPool.numThreads - 1; ++i) {
		for(uint32_t j = 0; j < childrenPerThread; ++j, ++end);

		threadPool.enqueueJob([this, begin, end]() {
			for(auto x = begin; x != end; ++x) {
				if((*x)->matrixDirty) (*x)->updateWorldMatrix();
				updateChildren(*x);
			}
		});

		begin = end;
	}

	for(auto x = begin; x != rootChildren.end(); ++x) {
		if((*x)->matrixDirty) (*x)->updateWorldMatrix();
		updateChildren(*x);
	}

	threadPool.wait();
}