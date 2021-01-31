#pragma once

#include "../common/light.h"
#include "./mesh.h"

class ForwardRenderer : public agz::misc::uncopyable_t
{
public:

    using Light = common::PBSLight;

    explicit ForwardRenderer(D3D12Context &d3d12);

    rg::Pass *addToRenderGraph(
        rg::Graph    &graph,
        rg::Resource *renderTarget,
        rg::Resource *depthBuffer);

    void addMesh(const Mesh *mesh);

    void setCamera(const Float3 &eye);

    void setLights(
        const Light      *lights,
        size_t            count,
        ResourceManager  &manager,
        ResourceUploader &uploader);

private:

    void initRootSignature();

    void initPipeline(
        DXGI_FORMAT renderTargetFormat,
        DXGI_FORMAT depthBufferFormat);

    void initConstantBuffer();

    void doForwardPass(rg::PassContext &ctx);

    struct PSParams
    {
        Float3  eye;
        int32_t lightCount = 0;
    };

    D3D12Context &d3d12_;

    // 0: vsTransform   (b0)
    // 1: psTable
    //      0: albedo   (t0)
    //      1: metallic (t1)
    //      2: roughness(t2)
    // 2: psParams      (b1)
    // 3: lightBuffer   (t3)
    // linearSampler    (s0)
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipeline_;

    D3D12_VIEWPORT viewport_;
    D3D12_RECT     scissor_;

    rg::Resource *renderTarget_;
    rg::Resource *depthBuffer_;

    std::vector<const Mesh *> meshes_;

    PSParams                 psParamsData_;
    ConstantBuffer<PSParams> psParams_;

    Buffer lights_;
};
