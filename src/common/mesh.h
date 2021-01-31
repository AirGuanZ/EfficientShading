#pragma once

#include "./common.h"

namespace common
{

    class SimpleMesh : public agz::misc::uncopyable_t
    {
    public:

        struct Vertex
        {
            Float3 position;
            Float3 normal;
        };

        SimpleMesh() = default;

        SimpleMesh(SimpleMesh && other) noexcept = default;

        SimpleMesh &operator=(SimpleMesh && other) noexcept = default;

        void load(
            D3D12Context      &d3d12,
            ResourceUploader  &uploader,
            const std::string &model);

        VertexBuffer<Vertex> vertexBuffer;
    };

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
    
    class MeshWithTransform : public Mesh
    {
    public:
    
        struct VSTransform
        {
            Mat4 world;
            Mat4 worldViewProj;
        };
    
        MeshWithTransform() = default;
    
        MeshWithTransform(MeshWithTransform &&) noexcept = default;
    
        MeshWithTransform &operator=(MeshWithTransform &&) noexcept = default;
        
        void load(
            D3D12Context      &d3d12,
            ResourceUploader  &uploader,
            const std::string &model,
            const std::string &albedo,
            const std::string &metallic,
            const std::string &roughness);
    
        ConstantBuffer<VSTransform> vsTransform;
    };

    class MeshWithViewTransform : public Mesh
    {
    public:

        struct VSTransform
        {
            Mat4 world;
            Mat4 worldView;
            Mat4 worldViewProj;
        };

        MeshWithViewTransform() = default;

        MeshWithViewTransform(MeshWithViewTransform &&) noexcept = default;

        MeshWithViewTransform &operator=(MeshWithViewTransform &&) noexcept = default;

        void load(
            D3D12Context &d3d12,
            ResourceUploader &uploader,
            const std::string &model,
            const std::string &albedo,
            const std::string &metallic,
            const std::string &roughness);

        ConstantBuffer<VSTransform> vsTransform;
    };

}
