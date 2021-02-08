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
        rg::Graph    &graph,
        rg::Resource *renderTarget,
        rg::Resource *hierarchyZ,
        int           cullThread,
        int           cullQueue,
        int           renderThread,
        int           renderQueue);

    void addMesh(const Mesh *mesh);

    void setCamera(const Mat4 &view, const Mat4 &proj);

    void setCulledMeshRenderingEnabled(bool enabled);

private:

    static constexpr int MAX_MESH_COUNT = 20000;

    static constexpr int CULL_THREAD_GROUP_SIZE = 64;

    struct CullParams
    {
        Mat4 View;
        Mat4 Proj;
        Float2 viewport;
        int meshCount = 0;
        float pad[1]  = {};
    };

    struct PerMeshConst
    {
        Mat4 World;

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
        D3D12_VERTEX_BUFFER_VIEW  vertexBuffer   = {};
        D3D12_DRAW_ARGUMENTS      drawArguments  = {};
    };

    struct VSCamera
    {
        Mat4 ViewProj;
    };

    struct PSParams
    {
        Float3 lightDirection;
        float pad0 = 0;
    };

    static_assert(sizeof(PerMeshConst) % 16 == 0);

    void initCullPipeline(rg::Graph &graph, rg::Resource *hierarchyZ);

    void initRenderPipeline(rg::Graph &graph, rg::Resource *renderTarget);

    void doCopyPerMeshConstsPass(rg::PassContext &ctx);

    void doClearCommandBufferCounterPass(rg::PassContext &ctx);

    void doCullPass(rg::PassContext &ctx);

    void doRenderPass(rg::PassContext &ctx);

    D3D12Context &d3d_;

    std::vector<const Mesh *> meshes_;

    Mat4 view_;
    Mat4 proj_;

    bool renderCulledMeshes_ = false;

    // for mesh culling

    // 0: csParams              (b0)
    // 1: csTable
    //   0: perMeshConsts       (t0)
    //   1: hierarchyZ          (t1)
    //   2: commandBuffer       (u0)
    //   3: culledCommandBuffer (u1)
    ComPtr<ID3D12RootSignature> cullRootSignature_;
    ComPtr<ID3D12PipelineState> cullPipeline_;

    ConstantBuffer<CullParams> cullParams_;
    rg::DescriptorTable       *cullTable_ = nullptr;

    std::vector<Buffer>   perMeshConstsUpload_;
    rg::InternalResource *perMeshConsts_ = nullptr;

    Buffer                commandBufferZeroCounter_;
    size_t                commandBufferCounterOffset_ = 0;

    rg::InternalResource *commandBuffer_       = nullptr;
    rg::InternalResource *culledCommandBuffer_ = nullptr;

    rg::Resource *hierarchyZ_ = nullptr;

    // render (maybe-visible) meshes

    // 0: vsTransform (b0)
    // 1: camera      (b1)
    // 1: psParams    (b2)
    ComPtr<ID3D12RootSignature>    renderRootSignature_;
    ComPtr<ID3D12PipelineState>    renderPipeline_;
    ComPtr<ID3D12CommandSignature> renderCommandSignature_;

    ConstantBuffer<VSCamera> vsCamera_;
    ConstantBuffer<PSParams> psParams_;

    D3D12_VIEWPORT viewport_ = {};
    D3D12_RECT     scissor_  = {};

    rg::Resource *renderTarget_      = nullptr;
    rg::Resource *renderDepthBuffer_ = nullptr;

    // render culled meshes

    // 0: vsTransform (b0)
    // 1: camera      (b1)
    ComPtr<ID3D12RootSignature>    culledRootSignature_;
    ComPtr<ID3D12PipelineState>    culledPipeline_;
    ComPtr<ID3D12CommandSignature> culledCommandSignature_;
};
