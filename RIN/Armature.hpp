#pragma once

#include "Bone.hpp"

namespace RIN {
	class Armature {
	public:
		Bone* const bones;
	protected:
		bool _resident = false;

		Armature(Bone* bones) :
			bones(bones)
		{}

		Armature(const Armature&) = delete;
		virtual ~Armature() = 0;
	public:
		bool resident() const {
			return _resident;
		}
	};

	// inline allows the definition to reside here even if it is included multiple times
	inline Armature::~Armature() {}
}