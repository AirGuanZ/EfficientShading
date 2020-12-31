#pragma once

#include "common.h"

namespace common
{

    class SkyRenderer : public agz::misc::uncopyable_t
    {
    public:
    
        explicit SkyRenderer(
            D3D12Context     &d3d12,
            ResourceUploader &uploader);
    
        void loadSkyBox(
            const std::string &posX,
            const std::string &negX,
            const std::string &posY,
            const std::string &negY,
            const std::string &posZ,
            const std::string &negZ);
    
        rg::Pass *addToRenderGraph(
            rg::Graph    &graph,
            rg::Resource *renderTarget);
    
        void setCamera(const Float3 &eye, const Mat4 &viewProj) noexcept;
    
    private:
    
        void initVertexBuffer();
    
        void initRootSignature();
    
        void initPipeline(DXGI_FORMAT RTFmt);
    
        void initConstantBuffer();
    
        void doSkyPass(rg::PassContext &ctx);
    
        struct Vertex
        {
            Float3 position;
        };
    
        struct VSTransform
        {
            Float3 eye;
            float  pad = 0;
            Mat4   viewProj;
        };
    
        D3D12Context &d3d12_;
    
        ResourceUploader &uploader_;
    
        VertexBuffer<Vertex> vertexBuffer_;
    
        ComPtr<ID3D12RootSignature> rootSignature_;
        ComPtr<ID3D12PipelineState> pipeline_;
    
        UniqueResource cubeTex_;
        Descriptor     cubeTexSRV_;
    
        rg::Resource *renderTarget_;
    
        D3D12_VIEWPORT viewport_;
        D3D12_RECT     scissor_;
    
        Float3 eye_;
        Mat4   viewProj_;
    
        ConstantBuffer<VSTransform> vsTransform_;
    };

} // namespace common
