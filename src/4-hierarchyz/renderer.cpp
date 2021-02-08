#include <agz-utils/file.h>

#include "./renderer.h"

Renderer::Renderer(D3D12Context &d3d)
    : d3d_(d3d)
{

}

rg::Vertex *Renderer::addToRenderGraph(
    rg::Graph    &graph,
    rg::Resource *renderTarget,
    rg::Resource *hierarchyZ,
    int           cullThread,
    int           cullQueue,
    int           renderThread,
    int           renderQueue)
{
    initCullPipeline(graph, hierarchyZ);
    initRenderPipeline(graph, renderTarget);

    rg::Pass *copyPerMeshConstsPass,
             *clearCommandBufferCounterPass,
             *cullPass,
             *renderPass;
    
    {
        copyPerMeshConstsPass = graph.addPass(
            "copy per-mesh constants", cullThread, cullQueue);

        copyPerMeshConstsPass->addResourceState(
            perMeshConsts_, D3D12_RESOURCE_STATE_COPY_DEST);

        copyPerMeshConstsPass->setCallback(
            this, &Renderer::doCopyPerMeshConstsPass);
    }

    {
        clearCommandBufferCounterPass = graph.addPass(
            "clear command buffer counter", cullThread, cullQueue);

        clearCommandBufferCounterPass->addResourceState(
            commandBuffer_, D3D12_RESOURCE_STATE_COPY_DEST);

        clearCommandBufferCounterPass->addResourceState(
            culledCommandBuffer_, D3D12_RESOURCE_STATE_COPY_DEST);

        clearCommandBufferCounterPass->setCallback(
            this, &Renderer::doClearCommandBufferCounterPass);
    }

    {
        cullPass = graph.addPass("cull", cullThread, cullQueue);

        cullTable_ = cullPass->addDescriptorTable(false, true);

        cullTable_->addSRV(
            perMeshConsts_,
            rg::ShaderResourceType::NonPixelOnly,
            D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format                  = DXGI_FORMAT_UNKNOWN,
                .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer                  = D3D12_BUFFER_SRV{
                    .FirstElement        = 0,
                    .NumElements         = MAX_MESH_COUNT,
                    .StructureByteStride = sizeof(PerMeshConst),
                    .Flags               = D3D12_BUFFER_SRV_FLAG_NONE
                }
            });

        cullTable_->addSRV(
            hierarchyZ_,
            rg::ShaderResourceType::NonPixelOnly,
            D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format                  = DXGI_FORMAT_R32_FLOAT,
                .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2D               = D3D12_TEX2D_SRV{
                .MostDetailedMip         = 0,
                    .MipLevels           = UINT(-1),
                    .PlaneSlice          = 0,
                    .ResourceMinLODClamp = 0
                }
            });

        cullTable_->addUAV(
            commandBuffer_,
            commandBuffer_,
            D3D12_UNORDERED_ACCESS_VIEW_DESC{
                .Format        = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                .Buffer        = D3D12_BUFFER_UAV{
                    .FirstElement         = 0,
                    .NumElements          = MAX_MESH_COUNT,
                    .StructureByteStride  = sizeof(IndirectCommand),
                    .CounterOffsetInBytes = commandBufferCounterOffset_,
                    .Flags                = D3D12_BUFFER_UAV_FLAG_NONE
                }
            });

        cullTable_->addUAV(
            culledCommandBuffer_,
            culledCommandBuffer_,
            D3D12_UNORDERED_ACCESS_VIEW_DESC{
                .Format        = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                .Buffer        = D3D12_BUFFER_UAV{
                    .FirstElement         = 0,
                    .NumElements          = MAX_MESH_COUNT,
                    .StructureByteStride  = sizeof(IndirectCommand),
                    .CounterOffsetInBytes = commandBufferCounterOffset_,
                    .Flags                = D3D12_BUFFER_UAV_FLAG_NONE
                }
            });

        cullPass->setCallback(this, &Renderer::doCullPass);
    }

    {
        renderPass = graph.addPass("forward", renderThread, renderQueue);

        renderPass->addResourceState(
            commandBuffer_, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        renderPass->addResourceState(
            culledCommandBuffer_, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        renderPass->addRTV(
            renderTarget_,
            D3D12_RENDER_TARGET_VIEW_DESC{
                .Format        = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                .Texture2D     = { 0, 0 }
            });

        renderPass->addDSV(
            renderDepthBuffer_,
            rg::DepthStencilType::ReadAndWrite,
            D3D12_DEPTH_STENCIL_VIEW_DESC{
                .Format        = DXGI_FORMAT_D32_FLOAT,
                .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
                .Flags         = D3D12_DSV_FLAG_NONE,
                .Texture2D     = { 0 }
            });

        renderPass->setCallback(this, &Renderer::doRenderPass);
    }

    graph.addDependency(copyPerMeshConstsPass, cullPass);
    graph.addDependency(clearCommandBufferCounterPass, cullPass);
    graph.addDependency(cullPass, renderPass);

    return graph.addAggregate("cull and render", cullPass, renderPass);
}

void Renderer::addMesh(const Mesh *mesh)
{
    if(meshes_.size() >= MAX_MESH_COUNT)
        throw std::runtime_error("too many meshes!");
    meshes_.push_back(mesh);
}

void Renderer::setCamera(const Mat4 &view, const Mat4 &proj)
{
    view_ = view;
    proj_ = proj;
}

void Renderer::setCulledMeshRenderingEnabled(bool enabled)
{
    renderCulledMeshes_ = enabled;
}

void Renderer::initCullPipeline(rg::Graph &graph, rg::Resource *hierarchyZ)
{
    if(!cullRootSignature_)
    {
        CD3DX12_DESCRIPTOR_RANGE csTableRanges[2] = {};
        csTableRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
        csTableRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);

        CD3DX12_ROOT_PARAMETER params[2] = {};
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsDescriptorTable(2, csTableRanges);

        RootSignatureBuilder builder;
        builder.addParameters(params);

        builder.addStaticSampler(
            s0, D3D12_SHADER_VISIBILITY_ALL, D3D12_FILTER_MIN_MAG_MIP_POINT);

        cullRootSignature_ = builder.build(d3d_.getDevice());
    }

    if(!cullPipeline_)
    {
        FXC compiler;
        compiler.setWarnings(true);

        const char *shaderFilename = "./asset/hierarchyz/cull.hlsl";
        const std::string shaderSource = agz::file::read_txt_file(shaderFilename);

        auto cs = compiler.compile(
            shaderSource, "cs_5_0",
            FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = shaderFilename,
                .entry      = "CSMain"
            });

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
        desc.pRootSignature     = cullRootSignature_.Get();
        desc.CS.pShaderBytecode = cs->GetBufferPointer();
        desc.CS.BytecodeLength  = cs->GetBufferSize();
        desc.NodeMask           = 0;
        desc.CachedPSO          = {};
        desc.Flags              = D3D12_PIPELINE_STATE_FLAG_NONE;

        AGZ_D3D12_CHECK_HR(
            d3d_.getDevice()->CreateComputePipelineState(
                &desc, IID_PPV_ARGS(cullPipeline_.GetAddressOf())));
    }

    if(!cullParams_.isAvailable())
    {
        cullParams_.initializeUpload(
            d3d_.getResourceManager(), d3d_.getFramebufferCount());
    }

    const size_t perMeshConstsSize = sizeof(PerMeshConst) * MAX_MESH_COUNT;

    if(perMeshConstsUpload_.empty())
    {
        perMeshConstsUpload_.resize(d3d_.getFramebufferCount());
        for(auto &b : perMeshConstsUpload_)
        {
            b.initializeUpload(
                d3d_.getResourceManager(), perMeshConstsSize);
        }
    }

    perMeshConsts_ = graph.addInternalResource("per mesh constants");
    perMeshConsts_->setInitialState(D3D12_RESOURCE_STATE_COPY_DEST);
    perMeshConsts_->setDescription(
        CD3DX12_RESOURCE_DESC::Buffer(perMeshConstsSize));

    if(!commandBufferZeroCounter_.isAvailable())
    {
        commandBufferZeroCounter_.initializeUpload(
            d3d_.getResourceManager(), 4);

        const uint32_t counterValue = 0;
        commandBufferZeroCounter_.updateData(0, 4, &counterValue);
    }

    const size_t commandsSize = sizeof(IndirectCommand) * MAX_MESH_COUNT;
    commandBufferCounterOffset_ = agz::upalign_to<size_t>(
        commandsSize, D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);

    const size_t commandBufferSize = commandBufferCounterOffset_ + 4;

    commandBuffer_ = graph.addInternalResource("command buffer");
    commandBuffer_->setInitialState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandBuffer_->setDescription(
        CD3DX12_RESOURCE_DESC::Buffer(
            commandBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
    commandBuffer_->setPerFrame();

    culledCommandBuffer_ = graph.addInternalResource("culled command buffer");
    culledCommandBuffer_->setInitialState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    culledCommandBuffer_->setDescription(commandBuffer_->getDescription());
    culledCommandBuffer_->setPerFrame();

    hierarchyZ_ = hierarchyZ;
}

void Renderer::initRenderPipeline(rg::Graph &graph, rg::Resource *renderTarget)
{
    FXC compiler;
    compiler.setWarnings(true);

    if(!renderRootSignature_)
    {
        CD3DX12_ROOT_PARAMETER params[3] = {};
        params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        params[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        RootSignatureBuilder builder;
        builder.addParameters(params);

        builder.addFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        renderRootSignature_ = builder.build(d3d_.getDevice());
    }

    if(!culledRootSignature_)
    {
        CD3DX12_ROOT_PARAMETER params[2] = {};
        params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

        RootSignatureBuilder builder;
        builder.addParameters(params);

        builder.addFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        culledRootSignature_ = builder.build(d3d_.getDevice());
    }

    if(!renderPipeline_)
    {
        const char *shaderFilename = "./asset/hierarchyz/forward.hlsl";
        const std::string shaderSource = agz::file::read_txt_file(shaderFilename);

        auto vs = compiler.compile(
            shaderSource, "vs_5_0",
            FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = shaderFilename,
                .entry      = "VSMain"
            });

        auto ps = compiler.compile(
            shaderSource, "ps_5_0",
            FXC::Options{
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
        builder.addInputElement({
            "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
            0, offsetof(Mesh::Vertex, normal),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        });
        builder.setRootSignature(renderRootSignature_.Get());
        builder.setVertexShader(vs);
        builder.setPixelShader(ps);
        builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        builder.setRenderTargetCount(1);
        builder.setRenderTargetFormat(0, renderTarget->getDescription().Format);
        builder.setDepthStencilFormat(DXGI_FORMAT_D32_FLOAT);
        builder.setMultisample(1, 0);
        builder.setCullMode(D3D12_CULL_MODE_BACK);
        builder.setDepthTest(true, true);
        
        renderPipeline_ = builder.build(d3d_.getDevice());
    }
    
    if(!culledPipeline_)
    {
        const char *shaderFilename = "./asset/hierarchyz/forward_culled.hlsl";
        const std::string shaderSource = agz::file::read_txt_file(shaderFilename);

        auto vs = compiler.compile(
            shaderSource, "vs_5_0",
            FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = shaderFilename,
                .entry      = "VSMain"
            });

        auto ps = compiler.compile(
            shaderSource, "ps_5_0",
            FXC::Options{
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
        builder.setRootSignature(culledRootSignature_.Get());
        builder.setVertexShader(vs);
        builder.setPixelShader(ps);
        builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        builder.setRenderTargetCount(1);
        builder.setRenderTargetFormat(0, renderTarget->getDescription().Format);
        builder.setMultisample(1, 0);
        builder.setCullMode(D3D12_CULL_MODE_BACK);
        builder.setDepthTest(false, false);
        
        culledPipeline_ = builder.build(d3d_.getDevice());
    }

    if(!renderCommandSignature_)
    {
        D3D12_INDIRECT_ARGUMENT_DESC args[3] = {};
        args[0].Type                                  = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
        args[0].ConstantBufferView.RootParameterIndex = 0;
        args[1].Type                                  = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
        args[1].VertexBuffer.Slot                     = 0;
        args[2].Type                                  = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC desc;
        desc.ByteStride       = sizeof(IndirectCommand);
        desc.NumArgumentDescs = static_cast<UINT>(agz::array_size(args));
        desc.pArgumentDescs   = args;
        desc.NodeMask         = 0;

        AGZ_D3D12_CHECK_HR(
            d3d_.getDevice()->CreateCommandSignature(
                &desc, renderRootSignature_.Get(),
                IID_PPV_ARGS(renderCommandSignature_.GetAddressOf())));
    }

    if(!culledCommandSignature_)
    {
        D3D12_INDIRECT_ARGUMENT_DESC args[3] = {};
        args[0].Type                                  = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
        args[0].ConstantBufferView.RootParameterIndex = 0;
        args[1].Type                                  = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
        args[1].VertexBuffer.Slot                     = 0;
        args[2].Type                                  = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC desc;
        desc.ByteStride       = sizeof(IndirectCommand);
        desc.NumArgumentDescs = static_cast<UINT>(agz::array_size(args));
        desc.pArgumentDescs   = args;
        desc.NodeMask         = 0;

        AGZ_D3D12_CHECK_HR(
            d3d_.getDevice()->CreateCommandSignature(
                &desc, culledRootSignature_.Get(),
                IID_PPV_ARGS(culledCommandSignature_.GetAddressOf())));
    }

    if(!vsCamera_.isAvailable())
    {
        vsCamera_.initializeUpload(
            d3d_.getResourceManager(), d3d_.getFramebufferCount());
    }

    if(!psParams_.isAvailable())
    {
        psParams_.initializeUpload(
            d3d_.getResourceManager(), d3d_.getFramebufferCount());
    }

    viewport_ = renderTarget->getDefaultViewport();
    scissor_  = renderTarget->getDefaultScissor();

    renderTarget_ = renderTarget;

    const UINT w = static_cast<UINT>(renderTarget->getDescription().Width);
    const UINT h = static_cast<UINT>(renderTarget->getDescription().Height);

    auto renderDepthBuffer = graph.addInternalResource("forward depth buffer");
    renderDepthBuffer->setInitialState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
    renderDepthBuffer->setClearValue(
        D3D12_CLEAR_VALUE{
            .Format       = DXGI_FORMAT_D32_FLOAT,
            .DepthStencil = { 1, 0 }
        });
    renderDepthBuffer->setDescription(
        CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT, w, h, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
    renderDepthBuffer_ = renderDepthBuffer;
}

void Renderer::doCopyPerMeshConstsPass(rg::PassContext &ctx)
{
    if(meshes_.empty())
        return;

    std::vector<PerMeshConst> perMeshConstsData(meshes_.size());
    for(size_t i = 0; i < meshes_.size(); ++i)
    {
        auto  mesh      = meshes_[i];
        auto &meshConst = perMeshConstsData[i];

        meshConst.World       = mesh->vsTransformData.World;
        meshConst.vertexCount = mesh->vertexBuffer.getVertexCount();
        meshConst.lower       = mesh->lower;
        meshConst.upper       = mesh->upper;

        meshConst.vertexBuffer =
            mesh->vertexBuffer.getView().BufferLocation;
            
        meshConst.constantBuffer =
            mesh->vsTransform.getGPUVirtualAddress(ctx.getFrameIndex());
    }

    auto &uploadBuffer = perMeshConstsUpload_[ctx.getFrameIndex()];
    uploadBuffer.updateData(
        0,
        sizeof(PerMeshConst) * perMeshConstsData.size(),
        perMeshConstsData.data());

    ctx->CopyBufferRegion(
        ctx.getRawResource(perMeshConsts_), 0,
        uploadBuffer.getResource(), 0,
        uploadBuffer.getByteSize());
}

void Renderer::doClearCommandBufferCounterPass(rg::PassContext &ctx)
{
    ctx->CopyBufferRegion(
        ctx.getRawResource(commandBuffer_),
        commandBufferCounterOffset_,
        commandBufferZeroCounter_.getResource(),
        0, 4);

    ctx->CopyBufferRegion(
        ctx.getRawResource(culledCommandBuffer_),
        commandBufferCounterOffset_,
        commandBufferZeroCounter_.getResource(),
        0, 4);
}

void Renderer::doCullPass(rg::PassContext &ctx)
{
    if(meshes_.empty())
        return;

    ctx->SetComputeRootSignature(cullRootSignature_.Get());
    ctx->SetPipelineState(cullPipeline_.Get());

    cullParams_.updateData(
        ctx.getFrameIndex(),
        {
            view_,
            proj_,
            {
                static_cast<float>(renderTarget_->getDescription().Width),
                static_cast<float>(renderTarget_->getDescription().Height)
            },
            static_cast<int>(meshes_.size())
        });
    ctx->SetComputeRootConstantBufferView(
        0, cullParams_.getGPUVirtualAddress(ctx.getFrameIndex()));

    auto cullTable = ctx.getDescriptorRange(cullTable_);
    ctx->SetComputeRootDescriptorTable(1, cullTable[0]);

    const int threadGroupCount =
        agz::upalign_to<int>(
            static_cast<int>(meshes_.size()), CULL_THREAD_GROUP_SIZE) /
        CULL_THREAD_GROUP_SIZE;
    ctx->Dispatch(threadGroupCount, 1, 1);
}

void Renderer::doRenderPass(rg::PassContext &ctx)
{
    auto rawRTV = ctx.getDescriptor(renderTarget_).getCPUHandle();
    auto rawDSV = ctx.getDescriptor(renderDepthBuffer_).getCPUHandle();
    ctx->ClearDepthStencilView(
        rawDSV, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 1, &scissor_);
    ctx->OMSetRenderTargets(1, &rawRTV, false, &rawDSV);

    ctx->RSSetViewports(1, &viewport_);
    ctx->RSSetScissorRects(1, &scissor_);

    ctx->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // normal meshes

    ctx->SetGraphicsRootSignature(renderRootSignature_.Get());
    ctx->SetPipelineState(renderPipeline_.Get());

    vsCamera_.updateData(ctx.getFrameIndex(), { view_ * proj_ });
    ctx->SetGraphicsRootConstantBufferView(
        1, vsCamera_.getGPUVirtualAddress(ctx.getFrameIndex()));

    psParams_.updateData(ctx.getFrameIndex(), { Float3(0.3f, -1, 0.5f).normalize() });
    ctx->SetGraphicsRootConstantBufferView(
        2, psParams_.getGPUVirtualAddress(ctx.getFrameIndex()));

    auto rawCommandBuffer = ctx.getRawResource(commandBuffer_);
    ctx->ExecuteIndirect(
        renderCommandSignature_.Get(), MAX_MESH_COUNT,
        rawCommandBuffer, 0,
        rawCommandBuffer, commandBufferCounterOffset_);

    // culled meshes

    if(renderCulledMeshes_)
    {
        ctx->SetGraphicsRootSignature(culledRootSignature_.Get());
        ctx->SetPipelineState(culledPipeline_.Get());

        ctx->SetGraphicsRootConstantBufferView(
            1, vsCamera_.getGPUVirtualAddress(ctx.getFrameIndex()));

        auto rawCulledCommandBuffer = ctx.getRawResource(culledCommandBuffer_);
        ctx->ExecuteIndirect(
            culledCommandSignature_.Get(), MAX_MESH_COUNT,
            rawCulledCommandBuffer, 0,
            rawCulledCommandBuffer, commandBufferCounterOffset_);
    }
}
