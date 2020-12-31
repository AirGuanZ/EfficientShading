#pragma once

#include "./mesh.h"

class PreDepthRenderer : public agz::misc::uncopyable_t
{
public:

    explicit PreDepthRenderer(D3D12Context &d3d12);

    rg::Pass *addToRenderGraph(
        rg::Graph    &graph,
        rg::Resource *depthBuffer);

    void addMesh(const Mesh *mesh);

private:

    void initRootSignature();

    void initPipeline(DXGI_FORMAT depthBufferFormat);

    void doPreDepthPass(rg::PassContext &ctx);

    D3D12Context &d3d12_;

    rg::Resource *depthBuffer_;

    // 0: vsTransform
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipeline_;

    D3D12_VIEWPORT viewport_;
    D3D12_RECT     scissor_;

    std::vector<const Mesh *> meshes_;
};
