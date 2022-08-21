# mesh_export.py
# This script is used to exporting individual meshes
# Select the type of mesh to export
MESH_TYPE_STATIC = 0
MESH_TYPE_DYNAMIC = 1
# Set mode to the desired mesh type
mesh_type = MESH_TYPE_STATIC
# When using MESH_STATIC you may select as many
# objects as you like and they will be combined into
# one mesh
# When using MESH_DYNAMIC only the active object
# will be exported
# Make sure to triangulate the objects before exporting
# otherwise calc_tangents() will fail
# The name of the file will be the name of the active object
# Set the path relative to the .blend file for this scene
path = "../meshes/"
# You must indicate the LODs for each object in the scene
# Objects should be placed into collections marking which LOD
# they belong to
# Every object must have at least the most detailed LOD
# An object can have any combination of lower detailed LODs
# If an object is missing an LOD and the lowest LOD of any
# object being exported is lower than the missing LOD, then the
# lowest LOD higher than the missing LOD is exported in its place
# The names of the LOD collections to look for can be configured
# here, the length of the list represents the number of LODs to use
# The order of the collection names matter as the first name is the
# most detailed LOD and the last is the least detailed LOD
# Currently only 3 LODs are supported in RIN
# It is perfectly acceptible to export more or less than 3 LODs
# If less than 3 LODs are exproted, then RIN will just use the lowest
# LOD available for the missing LODs
# If more than 3 LODs are exported, then RIN will just ignore the extras
lods = [ "LOD 0", "LOD 1", "LOD 2" ]

# Static object input layout
# POSITION (float3)
# NORMAL (float3)
# TANGENT (float3)
# BINORMAL (float3)
# TEXCOORD (float2)
# MATERIAL_ID (uint)

# Dynamic object input layout
# POSITION (float3)
# NORMAL (float3)
# TANGENT (float3)
# BINORMAL (float3)
# TEXCOORD (float2)

# File format
# Object type (uint8)
# LOD count (uint8)
# Bounding sphere center (float32[3])
# Bounding sphere radius (float32)
# Vertex counts (uint32 array of LOD count elements)
# Index counts (uint32 array of LOD count elements)
# Vertices (struct buffer array of LOD count buffers)
# Indices (uint32 buffer array of LOD count buffers)

import bpy
import struct
import mathutils
import math

C = bpy.context
D = bpy.data

if mesh_type == MESH_TYPE_STATIC:
    print("Exporting static mesh")
    objects = C.selected_objects
    if not objects:
        objects = [C.active_object]
elif mesh_type == MESH_TYPE_DYNAMIC:
    print("Exporting dynamic mesh")
    objects = [C.active_object]

# Validate objects
objects_valid = bool(objects)
object_lods = [[None for lod in lods] for o in objects]
max_lods = 0
for object_index in range(len(objects)):
    object = objects[object_index]
    
    if len(object.users_collection) != 1:
        print("\"" + object.name + "\" must be in one collection")
        objects_valid = False
        break
    
    if object.users_collection[0].name != lods[0]:
        print("\"" + object.name + "\" must be in collection \"" + lods[0] + "\"")
        objects_valid = False
        break
    
    # Validate lods
    lods_valid = True
    for lod_index in range(len(lods)):
        lod = lods[lod_index]
        
        found = 0
        for o in D.collections[lod].objects:
            if o.name.startswith(object.name):
                lod_object = o
                found += 1
                
        if found == 0:
            object_lods[object_index][lod_index] = object_lods[object_index][lod_index - 1]
            continue
                
        if found > 1:
            print("Found naming conflict for \"" + object.name + "\" in \"" + lod + "\"")
            lods_valid = False
            break
        
        if lod_object.type != "MESH":
            print("\"" + lod_object.name + "\" is not a mesh")
            lods_valid = False
            break
        
        # Check to see if mesh is triangulated
        triangulated = True
        for face in lod_object.data.polygons:
            if len(face.vertices) != 3:
                print("\"" + lod_object.name + "\" is not triangulated")
                triangulated = False
                break
        if not triangulated:
            lods_valid = False
            break
        
        object_lods[object_index][lod_index] = lod_object
        max_lods = max(max_lods, lod_index + 1)
        
    if not lods_valid:
        objects_valid = False
        break

# Process objects
if objects_valid:
    print(str(len(objects)) + " object(s) and " + str(max_lods) + " LOD(s)")
    
    vertices = [{} for i in range(max_lods)]
    indices = [[] for i in range(max_lods)]
    index = [0 for i in range(max_lods)]
    
    # Axis aligned bounding box
    bbox_min = mathutils.Vector.Fill(3, math.inf)
    bbox_max = mathutils.Vector.Fill(3, -math.inf)
    
    # First pass
    for object_index in range(len(object_lods)):
        o_lods = object_lods[object_index]
        for lod_index in range(max_lods):
            lod = o_lods[lod_index]
            
            print("Generating \"" + lods[lod_index] + "/" + lod.name + "\"")
            
            mesh = lod.data
            
            if mesh_type == MESH_TYPE_STATIC:
                # Apply the object's transform to get its data in world space
                mesh.transform(lod.matrix_world)
            
            # Can fail if the mesh is not triangulated
            mesh.calc_tangents();
            
            for face in mesh.polygons:
                for loop_index in reversed(face.loop_indices): # Use cw winding order
                    loop = mesh.loops[loop_index]
                    vertex = mesh.vertices[loop.vertex_index]
                    uv = mesh.uv_layers.active.data[loop_index].uv
                    
                    # Update bounding box
                    for i in range(3):
                        if vertex.co[i] < bbox_min[i]:
                            bbox_min[i] = vertex.co[i]
                        if vertex.co[i] > bbox_max[i]:
                            bbox_max[i] = vertex.co[i]
                    
                    # Pack data for single vertex
                    if mesh_type == MESH_TYPE_STATIC:
                        packed_data = struct.pack(
                            '<ffffffffffffff',
                            vertex.co.x,
                            vertex.co.y,
                            vertex.co.z,
                            loop.normal.x,
                            loop.normal.y,
                            loop.normal.z,
                            loop.tangent.x,
                            loop.tangent.y,
                            loop.tangent.z,
                            loop.bitangent.x,
                            loop.bitangent.y,
                            loop.bitangent.z,
                            uv.x,
                            uv.y
                        )
                    elif mesh_type == MESH_TYPE_DYNAMIC:
                        packed_data = struct.pack(
                            '<ffffffffffffff',
                            vertex.co.x,
                            vertex.co.y,
                            vertex.co.z,
                            loop.normal.x,
                            loop.normal.y,
                            loop.normal.z,
                            loop.tangent.x,
                            loop.tangent.y,
                            loop.tangent.z,
                            loop.bitangent.x,
                            loop.bitangent.y,
                            loop.bitangent.z,
                            uv.x,
                            uv.y
                        )
                    
                    # Insert vertex into map if it is unique
                    if packed_data not in vertices[lod_index]:
                        vertices[lod_index][packed_data] = index[lod_index]
                        index[lod_index] += 1
                    
                    # Build index list
                    indices[lod_index].append(vertices[lod_index][packed_data])
                    
            if mesh_type == MESH_TYPE_STATIC:
                # Transform the object back into model space
                mesh.transform(lod.matrix_world.inverted())

    # Second pass
    
    # Calculate the bounding sphere
    # This is a poor approximation of the sphere center,
    # but sufficient enough for culling
    bsphere_center = (bbox_min + bbox_max) * 0.5
    bsphere_radius = 0.0
    
    for object_index in range(len(object_lods)):
        o_lods = object_lods[object_index]
        for lod_index in range(max_lods):
            lod = o_lods[lod_index]
            
            mesh = lod.data
            
            if mesh_type == MESH_TYPE_STATIC:
                # Apply the object's transform to get its data in world space
                mesh.transform(lod.matrix_world)
            
            for vertex in mesh.vertices:
                bsphere_radius = max(bsphere_radius, (vertex.co - bsphere_center).length)
            
            if mesh_type == MESH_TYPE_STATIC:
                # Transform the object back into model space
                mesh.transform(lod.matrix_world.inverted())

    # Write file
    if mesh_type == MESH_TYPE_STATIC:
        extension = ".smesh"
    elif mesh_type == MESH_TYPE_DYNAMIC:
        extension = ".dmesh"
    
    file = open(D.filepath[:D.filepath.rfind('\\') + 1] + path + C.active_object.name + extension, 'wb')
    file.write(struct.pack(
        '<BBffff',
        mesh_type,
        max_lods,
        bsphere_center.x,
        bsphere_center.y,
        bsphere_center.z,
        bsphere_radius
    ))
    for lod_index in range(max_lods):
        file.write(struct.pack('<I', len(vertices[lod_index])))
    for lod_index in range(max_lods):
        file.write(struct.pack('<I', len(indices[lod_index])))
    for lod_index in range(max_lods):
        for packed_data in vertices[lod_index]:
            file.write(packed_data)
    for lod_index in range(max_lods):
        for index in indices[lod_index]:
            file.write(struct.pack('<I', index))
    file.close()
    
    print("Finished generating \"" + file.name[file.name.rfind('\\') + 1:] + "\"")
    print("Bounding sphere: <" + str(round(bsphere_center.x, 3)) + ", " +\
        str(round(bsphere_center.y, 3)) + ", " + str(round(bsphere_center.z, 3)) +\
        ">, r: " + str(round(bsphere_radius, 3)))
    for lod_index in range(max_lods):
        print("\"" + lods[lod_index] + "\": " + str(len(vertices[lod_index])) + " vertices and " +\
            str(len(indices[lod_index])) + " indices")
    print("")
else:
    print("Error exporting mesh")