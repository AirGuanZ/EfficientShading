#include <agz/utility/mesh.h>

#include "./mesh.h"

namespace common
{

    void Mesh::load(
        D3D12Context      &d3d12,
        ResourceUploader  &uploader,
        const std::string &model,
        const std::string &albedo,
        const std::string &metallic,
        const std::string &roughness)
    {
        // vertices
    
        const auto triangles = agz::mesh::load_from_file(model);
    
        std::vector<Vertex> vertexData;
        vertexData.reserve(triangles.size() * 3);
    
        for(auto &tri : triangles)
        {
            for(int i = 0; i < 3; ++i)
            {
                vertexData.push_back({
                    tri.vertices[i].position,
                    tri.vertices[i].normal,
                    { tri.vertices[i].tex_coord.x, 1 - tri.vertices[i].tex_coord.y }
                });
            }
        }
    
        vertexBuffer.initializeDefault(
            d3d12.getResourceManager(),
            vertexData.size(),
            D3D12_RESOURCE_STATE_COMMON);
    
        uploader.upload(
            vertexBuffer, vertexData.data(), vertexData.size());
    
        uploader.submitAndSync();
    
        // textures
    
        TextureLoader texLoader(d3d12.getResourceManager(), uploader);
    
        this->albedo = texLoader.loadFromFile(
            DXGI_FORMAT_R8G8B8A8_UNORM, 1, albedo, true);
        this->metallic = texLoader.loadFromFile(
            DXGI_FORMAT_R8_UNORM, 1, metallic, true);
        this->roughness = texLoader.loadFromFile(
            DXGI_FORMAT_R8_UNORM, 1, roughness, true);
    
        texLoader.submit();
        AGZ_SCOPE_GUARD({ texLoader.sync(); });
    
        // descriptors
    
        descTable = d3d12.allocStaticRange(3);
    
        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc;
        SRVDesc.Format                  = DXGI_FORMAT_UNKNOWN;
        SRVDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Texture2D               = { 0, UINT(-1), 0, 0 };
    
        d3d12.getDevice()->CreateShaderResourceView(
            this->albedo->resource.Get(), &SRVDesc, descTable[0]);
        d3d12.getDevice()->CreateShaderResourceView(
            this->metallic->resource.Get(), &SRVDesc, descTable[1]);
        d3d12.getDevice()->CreateShaderResourceView(
            this->roughness->resource.Get(), &SRVDesc, descTable[2]);
    }

    void MeshWithTransform::load(
        D3D12Context      &d3d12,
        ResourceUploader  &uploader,
        const std::string &model,
        const std::string &albedo,
        const std::string &metallic,
        const std::string &roughness)
    {
        Mesh::load(d3d12, uploader, model, albedo, metallic, roughness);
        vsTransform.initializeUpload(
            d3d12.getResourceManager(), d3d12.getFramebufferCount());
    }


} // namespace common
