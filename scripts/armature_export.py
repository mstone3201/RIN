# armature_export.py

# This script is used for exporting individual armatures
# The name of the file will be the name of the active object
# Set the path relative to the .blend file for this scene
path = "../armatures/"
# Note that the root bone must always be at index 0

# File format
# Bone count (uint8)
# Root bone rest matrix (float4x4)
# Child bone index (uint8)
# Parent bone index (uint8)
# Child bone rest matrix (float4x4)
# ... (bone count - 1) ...
# Child bone index (uint8)
# Parent bone index (uint8)
# Child bone rest matrix (float4x4)

import bpy
import struct

C = bpy.context
D = bpy.data

armature = C.active_object

if armature.type == "ARMATURE":
    if not armature.data.bones:
        print("\"" + armature.name + "\" does not have any bones")
    elif len(armature.data.bones) > 256:
        print("\"" + armature.name + "\" has more than 256 bones")
    elif armature.data.bones[0].parent:
        print("\"" + armature.name + "\" does not have the root bone at index 0")
    else:
        print("Exporting armature")
        print("Generating \"" + armature.name + "\", bones: " + str(len(armature.data.bones)))
        
        file = open(D.filepath[:D.filepath.rfind('\\') + 1] + path + C.active_object.name + ".arm", 'wb')
        file.write(struct.pack(
            '<B',
            len(armature.data.bones)
        ))
        
        # Column-major column vector matrix (A * x)
        # Transpose it to make it a row-major row vector matrix (x * A)
        rest_matrix = armature.data.bones[0].matrix_local.transposed()
        
        file.write(struct.pack(
            '<ffffffffffffffff',
            rest_matrix[0][0],
            rest_matrix[0][1],
            rest_matrix[0][2],
            rest_matrix[0][3],
            rest_matrix[1][0],
            rest_matrix[1][1],
            rest_matrix[1][2],
            rest_matrix[1][3],
            rest_matrix[2][0],
            rest_matrix[2][1],
            rest_matrix[2][2],
            rest_matrix[2][3],
            rest_matrix[3][0],
            rest_matrix[3][1],
            rest_matrix[3][2],
            rest_matrix[3][3]
        ))
        
        # children_recursive returns a list of bones in BFS order
        for bone in armature.data.bones[0].children_recursive:
            # Column-major column vector matrix (A * x)
            # Transpose it to make it a row-major row vector matrix (x * A)
            rest_matrix = bone.matrix_local.transposed()
            
            file.write(struct.pack(
                '<BBffffffffffffffff',
                armature.data.bones.keys().index(bone.name),
                armature.data.bones.keys().index(bone.parent.name),
                rest_matrix[0][0],
                rest_matrix[0][1],
                rest_matrix[0][2],
                rest_matrix[0][3],
                rest_matrix[1][0],
                rest_matrix[1][1],
                rest_matrix[1][2],
                rest_matrix[1][3],
                rest_matrix[2][0],
                rest_matrix[2][1],
                rest_matrix[2][2],
                rest_matrix[2][3],
                rest_matrix[3][0],
                rest_matrix[3][1],
                rest_matrix[3][2],
                rest_matrix[3][3]
            ))
        file.close()
        
        print("Finished generating \"" + file.name[file.name.rfind('\\') + 1:] + "\"")
else:
    print("\"" + armature.name + "\" is not an armature")