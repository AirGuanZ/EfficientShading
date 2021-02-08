#pragma once

#include "./common.h"

class HierarchyZGenerator : public agz::misc::uncopyable_t
{
public:

    explicit HierarchyZGenerator(D3D12Context &d3d);

    rg::Vertex *addToRenderGraph(
        rg::Graph    &graph,
        rg::Resource *framebuffer,
        int           depthThread,
        int           depthQueue,
        int           hierarchyThread,
        int           hierarchyQueue);

    void addMesh(const Mesh *mesh);

    void setCamera(const Mat4 &viewProj);

    rg::Resource *getHierarchyZBuffer() const;

private:

    void initRootSignature();

    void initHierarchyZBuffer(rg::Graph &graph, rg::Resource *framebuffer);

    void initConstantBuffer();

    void initPipeline();

    void doDepthPass(rg::PassContext &ctx);

    void doCopyDepthPass(rg::PassContext &ctx);

    void doHierarchyPass(rg::PassContext &ctx);

    struct HierarchyCSParams
    {
        int32_t lastWidth;
        int32_t lastHeight;
        int32_t thisWidth;
        int32_t thisHeight;
    };

    struct VSCamera
    {
        Mat4 viewProj;
    };

    D3D12Context &d3d_;

    Mat4 viewProj_;

    // 0: vsTransform
    // 1: vsCamera
    ComPtr<ID3D12RootSignature> depthRootSignature_;
    ComPtr<ID3D12PipelineState> depthPipeline_;

    ConstantBuffer<VSCamera> vsCamera_;

    // 0: csParams          (b0)
    // 1: csTable
    //    0: lastLevelDepth (t0)
    //    1: thisLevelDepth (u0)
    ComPtr<ID3D12RootSignature> hierarchyRootSignature_;
    ComPtr<ID3D12PipelineState> hierarchyPipeline_;

    D3D12_VIEWPORT depthViewport_;
    D3D12_RECT     depthScissor_;

    rg::InternalResource *depthBuffer_;
    rg::InternalResource *hierarchyZBuffer_;

    std::vector<rg::DescriptorTable *> hierarchyDescTables_;
    std::vector<Int2> mipmapSizes_;

    size_t nextHierarchyPassIndex_;
    std::vector<ConstantBuffer<HierarchyCSParams>> csParams_;

    std::vector<const Mesh *> meshes_;
};
