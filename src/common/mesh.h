#pragma once

#include "./common.h"

class Mesh : public agz::misc::uncopyable_t
{
public:

    struct Vertex
    {
        Float3 position;
        Float3 normal;
        Float2 texCoord;
    };

    Mesh() = default;

    Mesh(Mesh &&other) noexcept = default;

    Mesh &operator=(Mesh &&other) noexcept = default;

    void load(
        D3D12Context      &d3d12,
        ResourceUploader  &uploader,
        const std::string &model,
        const std::string &albedo,
        const std::string &metallic,
        const std::string &roughness);

    VertexBuffer<Vertex> vertexBuffer;
    UniqueResource       albedo;
    UniqueResource       metallic;
    UniqueResource       roughness;
    DescriptorRange      descTable;
};
