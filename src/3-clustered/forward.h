#pragma once

#include "./common.h"

class ForwardRenderer : public agz::misc::uncopyable_t
{
public:

    struct RenderGraphInput
    {
        rg::Graph *graph = nullptr;

        rg::Resource *renderTarget = nullptr;
        rg::Resource *depthBuffer  = nullptr;

        rg::Resource                   *clusterRangeBuffer = nullptr;
        D3D12_SHADER_RESOURCE_VIEW_DESC clusterRangeSRV    = {};

        rg::Resource                   *lightIndexBuffer = nullptr;
        D3D12_SHADER_RESOURCE_VIEW_DESC lightIndexSRV = {};
    };

    explicit ForwardRenderer(D3D12Context &d3d);

    rg::Pass *addToRenderGraph(const RenderGraphInput &graphInput);

    void addMesh(const Mesh *mesh);

    void setCamera(const Float3 &eye);

    void setCluster(float nearZ, float farZ, const Int3 &clusterCount);

    void setLights(const Buffer *lightBuffer, size_t lightCount);

private:

    void initRootSignature();

    void initConstantBuffer();

    void initViewportAndScissor();

    void initPipeline();

    void doForwardPass(rg::PassContext &ctx);

    struct PSParams
    {
        Float3  eye;
        int32_t lightCount = 0;

        int32_t clusterCountX = 0;
        int32_t clusterCountY = 0;
        int32_t clusterCountZ = 0;
        float   A             = 0;

        float B       = 0;
        float pad0[3] = {};
    };

    D3D12Context &d3d_;

    // 0: vsTransform      (b0)
    // 1: psParams         (b1)
    // 2: lightBuffer      (t0)
    // 3: psMeshTable
    //      0: albedo      (t1)
    //      1: metallic    (t2)
    //      2: roughness   (t3)
    // 4: psClusterTable
    //      1: clusterRange(t4)
    //      2: lightIndex  (t5)
    // linearSampler       (s0)
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipeline_;

    D3D12_VIEWPORT viewport_;
    D3D12_RECT     scissor_;

    RenderGraphInput graphInput_;

    std::vector<const Mesh *> meshes_;

    PSParams                 psParamsData_;
    ConstantBuffer<PSParams> psParams_;

    rg::DescriptorTable *psClusterTable_;

    const Buffer *lightBuffer_;
};
