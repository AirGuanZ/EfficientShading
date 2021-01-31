#include <agz-utils/file.h>

#include "./hierarchy.h"

HierarchyZGenerator::HierarchyZGenerator(D3D12Context &d3d)
    : d3d_(d3d), depthViewport_(), depthScissor_(),
      depthBuffer_(nullptr), hierarchyZBuffer_(nullptr),
      nextHierarchyPassIndex_(0)
{
    initRootSignature();
}

rg::Vertex *HierarchyZGenerator::addToRenderGraph(
    rg::Graph *graph, rg::Resource *framebuffer)
{
    depthViewport_ = framebuffer->getDefaultViewport();
    depthScissor_ = framebuffer->getDefaultScissor();

    initHierarchyZBuffer(graph, framebuffer);
    initConstantBuffer();
    nextHierarchyPassIndex_ = 0;

    initPipeline();

    // depth pass

    auto depthPass = graph->addPass("depth");
    depthPass->addDSV(
        depthBuffer_, D3D12_DEPTH_STENCIL_VIEW_DESC{
            .Format        = DXGI_FORMAT_D32_FLOAT,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags         = D3D12_DSV_FLAG_NONE,
            .Texture2D     = D3D12_TEX2D_DSV{ .MipSlice = 0, }
        });
    depthPass->setCallback(this, &HierarchyZGenerator::doDepthPass);

    // copy depth pass

    auto copyDepthPass = graph->addPass("copy depth to hierarchy");
    copyDepthPass->addResourceState(
        depthBuffer_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    copyDepthPass->addResourceState(
        hierarchyZBuffer_, D3D12_RESOURCE_STATE_COPY_DEST, 0);
    copyDepthPass->setCallback(this, &HierarchyZGenerator::doCopyDepthPass);

    // downsample passes

    hierarchyDescTables_.clear();

    std::vector<rg::Pass*> hierarchyPasses;
    for(size_t i = 0; i < mipmapSizes_.size() - 1; ++i)
    {
        auto pass = graph->addPass("downsample_" + std::to_string(i));

        auto table = pass->addDescriptorTable(false, true);
        table->addSRV(
            hierarchyZBuffer_, rg::ShaderResourceType::NonPixelOnly,
            D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format                  = DXGI_FORMAT_R32_FLOAT,
                .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2D               = D3D12_TEX2D_SRV{
                    .MostDetailedMip     = static_cast<UINT>(i),
                    .MipLevels           = 1,
                    .PlaneSlice          = 0,
                    .ResourceMinLODClamp = 0
                }
            });
        table->addUAV(
            hierarchyZBuffer_,
            D3D12_UNORDERED_ACCESS_VIEW_DESC{
                .Format        = DXGI_FORMAT_R32_FLOAT,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D     = D3D12_TEX2D_UAV{
                    .MipSlice   = static_cast<UINT>(i + 1),
                    .PlaneSlice = 0
                }
            });
        hierarchyDescTables_.push_back(table);

        pass->setCallback(this, &HierarchyZGenerator::doHierarchyPass);

        hierarchyPasses.push_back(pass);
    }

    graph->addDependency(depthPass, copyDepthPass);

    if(hierarchyPasses.empty())
        return graph->addAggregate("hierarchy", depthPass, copyDepthPass);

    graph->addDependency(copyDepthPass, hierarchyPasses.front());
    for(size_t i = 1; i < hierarchyPasses.size(); ++i)
        graph->addDependency(hierarchyPasses[i - 1], hierarchyPasses[i]);

    return graph->addAggregate("hierarchy", depthPass, hierarchyPasses.back());
}

void HierarchyZGenerator::addMesh(const Mesh *mesh)
{
    meshes_.push_back(mesh);
}

void HierarchyZGenerator::initRootSignature()
{
    {
        CD3DX12_ROOT_PARAMETER params[1] = {};
        params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

        RootSignatureBuilder builder;
        for(auto &p : params)
            builder.addParameter(p);

        builder.addFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        depthRootSignature_ = builder.build(d3d_.getDevice());
    }

    {
        CD3DX12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_ROOT_PARAMETER params[2] = {};
        params[0].InitAsConstantBufferView(0, 0);
        params[1].InitAsDescriptorTable(2, ranges);

        RootSignatureBuilder builder;
        for(auto &p : params)
            builder.addParameter(p);

        hierarchyRootSignature_ = builder.build(d3d_.getDevice());
    }
}

void HierarchyZGenerator::initHierarchyZBuffer(
    rg::Graph *graph, rg::Resource *framebuffer)
{
    int w = static_cast<int>(framebuffer->getDescription().Width);
    int h = static_cast<int>(framebuffer->getDescription().Height);

    depthBuffer_ = graph->addInternalResource("depth buffer");
    depthBuffer_->setInitialState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
    depthBuffer_->setClearValue(D3D12_CLEAR_VALUE{
        .Format       = DXGI_FORMAT_D32_FLOAT,
        .DepthStencil = { 1, 0 }
    });
    depthBuffer_->setDescription(CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_TYPELESS, w, h, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));

    hierarchyZBuffer_ = graph->addInternalResource("hierarchy-z buffer");
    hierarchyZBuffer_->setInitialState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    hierarchyZBuffer_->setDescription(CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_FLOAT, w, h, 1, 0, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));

    mipmapSizes_.clear();

    for(;;)
    {
        mipmapSizes_.push_back({ w, h });
        if(w == 1 && h == 1)
            break;
        if(w > 1) w >>= 1;
        if(h > 1) h >>= 1;
    }
}

void HierarchyZGenerator::initConstantBuffer()
{
    assert(mipmapSizes_.size() >= 1);

    csParams_.clear();
    csParams_.resize(mipmapSizes_.size() - 1);
    for(auto &c : csParams_)
    {
        c.initializeUpload(
            d3d_.getResourceManager(), d3d_.getFramebufferCount());
    }
}

void HierarchyZGenerator::initPipeline()
{
    FXC compiler;
    compiler.setWarnings(true);

    {
        const char *shaderFilename = "./asset/hierarchyz/depth.hlsl";
        const auto shaderSource = agz::file::read_txt_file(shaderFilename);

        auto vs = compiler.compile(
            shaderSource, "vs_5_0", FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = shaderFilename,
                .entry      = "VSMain"
            });

        auto ps = compiler.compile(
            shaderSource, "ps_5_0", FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = shaderFilename,
                .entry      = "PSMain"
            });

        PipelineBuilder builder;

        builder.addInputElement({
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
            0, offsetof(Mesh::Vertex, position),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            });
        builder.setRootSignature(depthRootSignature_);
        builder.setVertexShader(vs);
        builder.setPixelShader(ps);
        builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        builder.setRenderTargetCount(0);
        builder.setDepthStencilFormat(DXGI_FORMAT_D32_FLOAT);
        builder.setMultisample(1, 0);
        builder.setFillMode(D3D12_FILL_MODE_SOLID);
        builder.setCullMode(D3D12_CULL_MODE_BACK);
        builder.setDepthTest(true, true);

        depthPipeline_ = builder.build(d3d_.getDevice());
    }

    {
        const char *shaderFilename = "./asset/hierarchyz/hierarchy.hlsl";
        const auto shaderSource = agz::file::read_txt_file(shaderFilename);

        auto cs = compiler.compile(
            shaderSource, "cs_5_0", FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = shaderFilename,
                .entry      = "CSMain"
            });

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc;

        desc.pRootSignature = hierarchyRootSignature_.Get();
        desc.CS = D3D12_SHADER_BYTECODE{
            .pShaderBytecode = cs->GetBufferPointer(),
            .BytecodeLength = cs->GetBufferSize()
        };
        desc.NodeMask  = 0;
        desc.CachedPSO = {};
        desc.Flags     = D3D12_PIPELINE_STATE_FLAG_NONE;

        AGZ_D3D12_CHECK_HR(
            d3d_.getDevice()->CreateComputePipelineState(
                &desc, IID_PPV_ARGS(hierarchyPipeline_.GetAddressOf())));
    }
}

void HierarchyZGenerator::doDepthPass(rg::PassContext &ctx)
{
    auto rawDSV = ctx.getDescriptor(depthBuffer_).getCPUHandle();
    ctx->ClearDepthStencilView(
        rawDSV, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 1, &depthScissor_);
    ctx->OMSetRenderTargets(0, nullptr, false, &rawDSV);

    ctx->SetPipelineState(depthPipeline_.Get());
    ctx->SetGraphicsRootSignature(depthRootSignature_.Get());

    ctx->RSSetViewports(1, &depthViewport_);
    ctx->RSSetScissorRects(1, &depthScissor_);

    for(auto mesh : meshes_)
    {
        ctx->SetGraphicsRootConstantBufferView(
            0, mesh->vsTransform.getGPUVirtualAddress(ctx.getFrameIndex()));

        auto rawVtxBufView = mesh->vertexBuffer.getView();
        ctx->IASetVertexBuffers(0, 1, &rawVtxBufView);

        ctx->DrawInstanced(mesh->vertexBuffer.getVertexCount(), 1, 0, 0);
    }
}

void HierarchyZGenerator::doCopyDepthPass(rg::PassContext &ctx)
{
    D3D12_TEXTURE_COPY_LOCATION srcLoc;
    srcLoc.pResource        = ctx.getRawResource(depthBuffer_);
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc;
    dstLoc.pResource        = ctx.getRawResource(hierarchyZBuffer_);
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    ctx->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
}

void HierarchyZGenerator::doHierarchyPass(rg::PassContext &ctx)
{
    AGZ_SCOPE_GUARD({
        ++nextHierarchyPassIndex_;
        nextHierarchyPassIndex_ %= mipmapSizes_.size() - 1;
    });

    const Int2 thisSize = mipmapSizes_[nextHierarchyPassIndex_ + 1];

    ctx->SetPipelineState(hierarchyPipeline_.Get());
    ctx->SetComputeRootSignature(hierarchyRootSignature_.Get());
    
    auto &constBuffer = csParams_[nextHierarchyPassIndex_];
    constBuffer.updateData(
        ctx.getFrameIndex(), HierarchyCSParams{
            .lastWidth  = mipmapSizes_[nextHierarchyPassIndex_].x,
            .lastHeight = mipmapSizes_[nextHierarchyPassIndex_].y,
            .thisWidth  = thisSize.x,
            .thisHeight = thisSize.y
        });
    ctx->SetComputeRootConstantBufferView(
        0, constBuffer.getGPUVirtualAddress(ctx.getFrameIndex()));

    auto csTable = ctx.getDescriptorRange(
        hierarchyDescTables_[nextHierarchyPassIndex_]);
    ctx->SetComputeRootDescriptorTable(1, csTable[0]);

    constexpr int THREAD_GROUP_SIZE_X = 16;
    constexpr int THREAD_GROUP_SIZE_Y = 16;

    const int xGroupCount = agz::upalign_to(
        thisSize.x, THREAD_GROUP_SIZE_X) / THREAD_GROUP_SIZE_X;
    const int yGroupCount = agz::upalign_to(
        thisSize.y, THREAD_GROUP_SIZE_Y) / THREAD_GROUP_SIZE_Y;

    ctx->Dispatch(xGroupCount, yGroupCount, 1);
}
