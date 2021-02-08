#pragma once

#include "../common/light.h"
#include "../common/mesh.h"

using Light = common::PBSLight;

class Mesh : public agz::misc::uncopyable_t
{
public:

    struct Vertex
    {
        Float3 position;
        Float3 normal;
    };

    struct VSTransform
    {
        Mat4 World;
    };

    Mesh() = default;

    Mesh(Mesh &&) noexcept = default;

    Mesh &operator=(Mesh &&) noexcept = default;
    
    void load(
        D3D12Context      &d3d12,
        ResourceUploader  &uploader,
        const std::string &model);

    void updateVSTransform(int frameIndex, const VSTransform &transform);

    VertexBuffer<Vertex> vertexBuffer;

    VSTransform                 vsTransformData;
    ConstantBuffer<VSTransform> vsTransform;

    Float3 lower;
    Float3 upper;
};
