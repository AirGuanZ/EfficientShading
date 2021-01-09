#include <agz-utils/file.h>

#include "./forward.h"

ForwardRenderer::ForwardRenderer(D3D12Context &d3d)
    : d3d_(d3d), viewport_(), scissor_(), psClusterTable_(nullptr)
{
    initRootSignature();
    initConstantBuffer();
}

rg::Pass *ForwardRenderer::addToRenderGraph(const RenderGraphInput &graphInput)
{
    graphInput_ = graphInput;

    initViewportAndScissor();
    initPipeline();

    auto pass = graphInput_.graph->addPass(
        "forward rendering");

    pass->addRTV(graphInput_.renderTarget, D3D12_RENDER_TARGET_VIEW_DESC{
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        .Texture2D     = { 0, 0 }
    });

    pass->addDSV(graphInput_.depthBuffer, D3D12_DEPTH_STENCIL_VIEW_DESC{
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
        .Flags         = D3D12_DSV_FLAG_NONE,
        .Texture2D     = { 0 }
        });

    psClusterTable_ = pass->addDescriptorTable(rg::Pass::GPUOnly);
    psClusterTable_->addSRV(
        graphInput_.clusterRangeBuffer,
        rg::ShaderResourceType::PixelOnly,
        graphInput_.clusterRangeSRV);
    psClusterTable_->addSRV(
        graphInput_.lightIndexBuffer,
        rg::ShaderResourceType::PixelOnly,
        graphInput_.lightIndexSRV);

    pass->setCallback(this, &ForwardRenderer::doForwardPass);

    return pass;
}

void ForwardRenderer::addMesh(const Mesh *mesh)
{
    meshes_.push_back(mesh);
}

void ForwardRenderer::setCamera(const Float3 &eye)
{
    psParamsData_.eye = eye;
}

void ForwardRenderer::setCluster(
    float nearZ, float farZ, const Int3 &clusterCount)
{
    psParamsData_.clusterCountX = clusterCount.x;
    psParamsData_.clusterCountY = clusterCount.y;
    psParamsData_.clusterCountZ = clusterCount.z;

    psParamsData_.A = clusterCount.z / std::log(farZ / nearZ);
    psParamsData_.B = clusterCount.z * std::log(nearZ) / std::log(farZ / nearZ);
}

void ForwardRenderer::setLights(const Buffer *lightBuffer, size_t lightCount)
{
    psParamsData_.lightCount = static_cast<int32_t>(lightCount);
    lightBuffer_             = lightBuffer;
}

void ForwardRenderer::setCulling(bool enabled)
{
    psParamsData_.enableCulling = enabled;
}

void ForwardRenderer::initRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE psMeshTable;
    psMeshTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1);

    CD3DX12_DESCRIPTOR_RANGE psClusterTable;
    psClusterTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 4);

    CD3DX12_ROOT_PARAMETER params[5];
    params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsShaderResourceView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[3].InitAsDescriptorTable(1, &psMeshTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[4].InitAsDescriptorTable(1, &psClusterTable, D3D12_SHADER_VISIBILITY_PIXEL);

    RootSignatureBuilder builder(d3d_.getDevice());
    builder.addParameters(params);

    builder.addStaticSampler(
        s0,
        D3D12_SHADER_VISIBILITY_PIXEL,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    builder.addFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    rootSignature_ = builder.build();
}

void ForwardRenderer::initConstantBuffer()
{
    psParams_.initializeUpload(
        d3d_.getResourceManager(), d3d_.getFramebufferCount());
}

void ForwardRenderer::initViewportAndScissor()
{
    const auto &RTDesc = graphInput_.renderTarget->getDescription();

    viewport_ = CD3DX12_VIEWPORT(
        0.0f, 0.0f, float(RTDesc.Width), float(RTDesc.Height));
    scissor_ = CD3DX12_RECT(
        0, 0, LONG(RTDesc.Width), LONG(RTDesc.Height));
}

void ForwardRenderer::initPipeline()
{
    const char       *shaderFilename = "./asset/clustered/forward.hlsl";
    const std::string shaderSource   = agz::file::read_txt_file(shaderFilename);

    FXC compiler;
    compiler.setWarnings(true);

    auto vs = compiler.compile(
        shaderSource, "vs_5_1", FXC::Options{
            .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
            .sourceName = shaderFilename,
            .entry      = "VSMain"
        });

    auto ps = compiler.compile(
        shaderSource, "ps_5_1", FXC::Options{
            .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
            .sourceName = shaderFilename,
            .entry      = "PSMain"
        });

    PipelineBuilder builder(d3d_.getDevice());

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
    builder.addInputElement({
        "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
        0, offsetof(Mesh::Vertex, texCoord),
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        });

    builder.setRenderTargetCount(1);
    builder.setRenderTargetFormat(
        0, graphInput_.renderTarget->getDescription().Format);
    builder.setDepthStencilFormat(
        graphInput_.depthBuffer->getDescription().Format);

    builder.setDepthTest(true, true);
    builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    builder.setCullMode(D3D12_CULL_MODE_BACK);
    builder.setRootSignature(rootSignature_);

    builder.setVertexShader(vs);
    builder.setPixelShader(ps);

    pipeline_ = builder.build();
}

void ForwardRenderer::doForwardPass(rg::PassContext &ctx)
{
    auto rawRTV = ctx.getDescriptor(graphInput_.renderTarget).getCPUHandle();
    auto rawDSV = ctx.getDescriptor(graphInput_.depthBuffer).getCPUHandle();
    ctx->ClearDepthStencilView(rawDSV, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 1, &scissor_);
    ctx->OMSetRenderTargets(1, &rawRTV, false, &rawDSV);

    ctx->RSSetViewports(1, &viewport_);
    ctx->RSSetScissorRects(1, &scissor_);

    ctx->SetGraphicsRootSignature(rootSignature_.Get());
    ctx->SetPipelineState(pipeline_.Get());

    ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    psParams_.updateData(ctx.getFrameIndex(), psParamsData_);
    ctx->SetGraphicsRootConstantBufferView(
        1, psParams_.getGPUVirtualAddress(ctx.getFrameIndex()));

    ctx->SetGraphicsRootShaderResourceView(
        2, lightBuffer_->getGPUVirtualAddress());

    ctx->SetGraphicsRootDescriptorTable(
        4, ctx.getDescriptorRange(psClusterTable_)[0]);

    for(auto mesh : meshes_)
    {
        ctx->SetGraphicsRootConstantBufferView(
            0, mesh->vsTransform.getGPUVirtualAddress(ctx.getFrameIndex()));
        ctx->SetGraphicsRootDescriptorTable(3, mesh->descTable[0]);

        auto vtxBufView = mesh->vertexBuffer.getView();
        ctx->IASetVertexBuffers(0, 1, &vtxBufView);

        ctx->DrawInstanced(mesh->vertexBuffer.getVertexCount(), 1, 0, 0);
    }
}
