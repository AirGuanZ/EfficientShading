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
        Mesh() = default;

        Mesh(Mesh &&) noexcept = default;

        Mesh &operator=(Mesh &&) noexcept = default;
        
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
