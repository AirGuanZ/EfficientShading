#pragma once

#include "./common.h"

/*
    upload per mesh consts
    clear commandBuffer.counter
    cull shader -> append to commandBuffer
    drawIndirect with commandBuffer
*/

class Renderer : public agz::misc::uncopyable_t
{
public:

    explicit Renderer(D3D12Context &d3d);

    rg::Vertex *addToRenderGraph(
        const rg::Graph &graph,
        rg::Resource    *renderTarget);

    void addMesh(const Mesh *mesh);

private:

    static constexpr int MAX_MESH_COUNT = 1000;

    struct CullParams
    {
        int totalMeshCount = 0;
        float pad[3]       = {};
    };

    struct PerMeshConst
    {
        Mat4 WVP;

        Float3 lower;
        uint32_t vertexCount = 0;

        Float3 upper;
        float pad0 = 0;

        D3D12_GPU_VIRTUAL_ADDRESS vertexBuffer   = 0;
        D3D12_GPU_VIRTUAL_ADDRESS constantBuffer = 0;
    };

    struct IndirectCommand
    {
        D3D12_GPU_VIRTUAL_ADDRESS constantBuffer = {};
        D3D12_DRAW_ARGUMENTS      drawArguments  = {};
        D3D12_VERTEX_BUFFER_VIEW  vertexBuffer   = {};
    };

    struct PSParams
    {
        Float3 lightDirection;
        float pad0 = 0;
    };

    static_assert(sizeof(PerMeshConst) % 16 == 0);

    void initCullPipeline();

    void initRenderPipeline();

    D3D12Context &d3d_;

    // for mesh culling

    ComPtr<ID3D12RootSignature> cullRootSignature_;
    ComPtr<ID3D12PipelineState> cullPipeline_;

    ConstantBuffer<CullParams> cullParams_;

    ConstantBuffer<PerMeshConst> perMeshConstsUpload_;
    rg::InternalResource        *perMeshConsts_;

    Buffer                commandBufferZeroCounter_;
    rg::InternalResource *commandBuffer_;

    rg::Resource *hierarchyZ_;

    // for indirect drawing

    ComPtr<ID3D12RootSignature> renderRootSignature_;
    ComPtr<ID3D12PipelineState> renderPipeline_;

    D3D12_VIEWPORT viewport_;
    D3D12_RECT     scissor_;

    rg::Resource *renderTarget_;
    rg::Resource *renderDepthBuffer_;
};
