#pragma once

#include "../common/light.h"
#include "../common/mesh.h"

class MeshRenderer : public agz::misc::uncopyable_t
{
public:

    using Mesh  = common::MeshWithTransform;
    using Light = common::PBSLight;

    explicit MeshRenderer(D3D12Context &d3d12);

    rg::Pass *addToRenderGraph(
        rg::Graph    &graph,
        rg::Resource *renderTarget,
        rg::Resource *depthStencil);

    void addToRenderQueue(const Mesh *mesh);

    void setCamera(const Float3 &eye);

    void setLights(
        const Light      *lights,
        size_t            count,
        ResourceManager  &manager,
        ResourceUploader &uploader);

private:

    void initRootSignature();

    void initPipeline(DXGI_FORMAT RTFmt, DXGI_FORMAT DSFmt);

    void initConstantBuffer();

    struct PSParams
    {
        Float3  eye;
        int32_t lightCount = 0;
    };

    ID3D12Device    *device_;
    ResourceManager &rscMgr_;
    int              frameCount_;

    // 0: vsTransform
    // 1: psTable
    //      0: albedo
    //      1: metallic
    //      2: roughness
    // 2: psParams
    // 3: lightBuffer
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipeline_;

    D3D12_VIEWPORT viewport_;
    D3D12_RECT     scissor_;

    std::vector<const Mesh *> meshes_;

    PSParams                 psParamsData_;
    ConstantBuffer<PSParams> psParams_;

    Buffer lightBuffer_;
};
