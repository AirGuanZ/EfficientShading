#include <agz-utils/mesh.h>

#include "./common.h"

void Mesh::load(
    D3D12Context      &d3d12,
    ResourceUploader  &uploader,
    const std::string &model)
{
    const auto triangles = agz::mesh::load_from_file(model);
    
    std::vector<Vertex> vertexData;
    vertexData.reserve(triangles.size() * 3);

    lower = Float3((std::numeric_limits<float>::max)());
    upper = Float3(std::numeric_limits<float>::lowest());
    
    for(auto &tri : triangles)
    {
        for(int i = 0; i < 3; ++i)
        {
            vertexData.push_back({
                tri.vertices[i].position,
                tri.vertices[i].normal,
            });

            lower = vec_min(lower, tri.vertices[i].position);
            upper = vec_max(upper, tri.vertices[i].position);
        }
    }
    
    vertexBuffer.initializeDefault(
        d3d12.getResourceManager(),
        vertexData.size(),
        D3D12_RESOURCE_STATE_COMMON);
    
    uploader.upload(
        vertexBuffer, vertexData.data(), vertexData.size());
    
    uploader.submitAndSync();

    vsTransform.initializeUpload(
        d3d12.getResourceManager(), d3d12.getFramebufferCount());
}

void Mesh::updateVSTransform(int frameIndex, const VSTransform &transform)
{
    vsTransformData = transform;
    vsTransform.updateData(frameIndex, transform);
}
