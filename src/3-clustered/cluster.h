#pragma once

#include "./common.h"

class LightCluster : public agz::misc::uncopyable_t
{
public:

    explicit LightCluster(D3D12Context &d3d);

    void setClusterCount(const Int3 &count);

    rg::Vertex *addToRenderGraph(
        rg::Graph &graph, int thread = 0, int queue = 0);

    rg::Resource *getClusterRangeBuffer() const;

    rg::Resource *getLightIndexBuffer() const;

    D3D12_SHADER_RESOURCE_VIEW_DESC getClusterRangeSRV() const;

    D3D12_SHADER_RESOURCE_VIEW_DESC getLightIndexSRV() const;

    void setProj(float nearZ, float farZ, const Mat4 &proj);

    void setView(const Float3 &eye, const Mat4 &view);

    void updateClusterAABBs(ResourceUploader &uploader);

    void setLights(const Buffer &lightBuffer, size_t lightCount);

private:

    static constexpr int AVG_LIGHTS_PER_CLUSTER = 8;

    struct CSParams
    {
        Mat4 view;

        int32_t clusterXCount      = 0;
        int32_t clusterYCount      = 0;
        int32_t clusterZCount      = 0;
        int32_t lightCount         = 0;

        int32_t lightIndexCount = 0;
        float pad0[3] = {};
    };

    struct ClusterRange
    {
        int32_t rangeBeg;
        int32_t rangeEnd;
    };

    struct ClusterAABB
    {
        Float3 lower;
        Float3 upper;
    };

    void initRootSignature();

    void initPipeline();

    void initConstantBuffer();

    void initZeroLightIndexCounter();

    void initClusterAABBBuffer(ResourceUploader &uploader);

    void updateCSParams();

    void doClearLightIndexCounterPass(rg::PassContext &ctx);

    void doClusterPass(rg::PassContext &ctx);

    D3D12Context &d3d_;

    // pipeline

    // 0. csParams         (b0)
    // 1. lightBuffer      (t0)
    // 2. clusterAABBBuffer(t1)
    // 3. uavTable:
    //      0: clusterRange     (u0)
    //      1: lightIndex       (u1)
    //      2: lightIndexCounter(u2)
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipeline_;

    // cluster

    Int3 clusterCount_;

    rg::InternalResource *clusterRange_;
    rg::InternalResource *lightIndex_;

    rg::DescriptorTable *uavTable_;

    // proj

    float  nearZ_;
    float  farZ_;
    Mat4   proj_;

    // view

    Float3 eye_;
    Mat4   view_;

    // lights

    const Buffer *lightBuffer_;
    size_t        lightCount_;

    // upload buffer used to clear light index counter

    Buffer zeroLightIndexCounter_;
    rg::InternalResource *lightIndexCounter_;

    // cluster aabb

    Buffer clusterAABBBuffer_;

    // constant buffer

    ConstantBuffer<CSParams> csParams_;
};
