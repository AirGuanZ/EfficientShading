#pragma once

#include "../common/mesh.h"

class MeshRenderer : public agz::misc::uncopyable_t
{
public:

    struct VSTransform
    {
        Mat4 world;
        Mat4 worldViewProj;
    };

    struct Mesh : ::Mesh
    {
        void load(
            D3D12Context      &d3d12,
            ResourceUploader  &uploader,
            const std::string &model,
            const std::string &albedo,
            const std::string &metallic,
            const std::string &roughness);

        ConstantBuffer<VSTransform> vsTransform;
    };

    struct Light
    {
        Float3 lightPosition;  float maxLightDistance = 0;
        Float3 lightIntensity; float pad0 = 0;
        Float3 lightAmbient;   float pad1 = 0;
    };

    struct RenderGraphNodes
    {
        rg::Pass *entry = nullptr;
        rg::Pass *exit  = nullptr;
    };

    explicit MeshRenderer(D3D12Context &d3d12);

    RenderGraphNodes addToRenderGraph(
        rg::Graph    &graph,
        rg::Resource *renderTarget);

    void addMesh(const Mesh *mesh);

    void setCamera(const Mat4 &viewProj, const Float3 &eye);

    void setLights(
        const Light      *lights,
        size_t            count,
        ResourceManager  &manager,
        ResourceUploader &uploader);

private:

    void initRootSignature();

    void initPipeline(DXGI_FORMAT RTFmt);

    void initConstantBuffer();

    void doGBufferPass(rg::PassContext &ctx);

    void doLightingPass(rg::PassContext &ctx);

    struct PSParams
    {
        Mat4    invViewProj;
        Float3  eye;
        int32_t lightCount = 0;
    };

    D3D12Context &d3d12_;

    // 0: vsTransform   (b0)
    // 1: psTable
    //      0: albedo   (t0)
    //      1: metallic (t1)
    //      2: roughness(t2)
    // linear sampler   (s0)
    ComPtr<ID3D12RootSignature> gbufferRootSignature_;
    ComPtr<ID3D12PipelineState> gbufferPipeline_;

    // 0: psParams        (b0)
    // 1: lightBuffer     (t0)
    // 2: psTable
    //      gbuffer a     (t1)
    //      gbuffer b     (t2)
    //      gbuffer depth (t3)
    // point sampler            (s0)
    ComPtr<ID3D12RootSignature> lightingRootSignature_;
    ComPtr<ID3D12PipelineState> lightingPipeline_;

    D3D12_VIEWPORT viewport_;
    D3D12_RECT     scissor_;

    // A.rgb: normal; A.a: metallic
    // B.rgb: albedo; B.a: roughness
    rg::InternalResource *gbufferARsc_;
    rg::InternalResource *gbufferBRsc_;
    rg::InternalResource *gbufferDepthRsc_;
    rg::Resource         *renderTargetRsc_;

    rg::DescriptorTable *psTable_;

    std::vector<const Mesh *> meshes_;

    PSParams                 psParamsData_;
    ConstantBuffer<PSParams> psParams_;

    Buffer lightBuffer_;
};