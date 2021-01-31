#include <d3dcompiler.h>

#include <agz-utils/file.h>

#include "./forward.h"

ForwardRenderer::ForwardRenderer(D3D12Context &d3d12)
    : d3d12_(d3d12),
      viewport_(),
      scissor_(),
      renderTarget_(nullptr),
      depthBuffer_(nullptr)
{
    initRootSignature();
    initConstantBuffer();
}

rg::Pass *ForwardRenderer::addToRenderGraph(
    rg::Graph                           &graph,
    rg::Resource                        *renderTarget,
    rg::Resource                        *depthBuffer)
{
    initPipeline(
        renderTarget->getDescription().Format,
        depthBuffer->getDescription().Format);

    renderTarget_ = renderTarget;
    depthBuffer_ = depthBuffer;

    viewport_ = renderTarget->getDefaultViewport();
    scissor_ = renderTarget_->getDefaultScissor();

    auto pass = graph.addPass("forward");
    pass->addRTV(renderTarget, D3D12_RENDER_TARGET_VIEW_DESC{
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        .Texture2D     = { 0, 0 }
        });
    pass->addDSV(
        depthBuffer,
        rg::DepthStencilType::ReadOnly,
        D3D12_DEPTH_STENCIL_VIEW_DESC{
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags         = D3D12_DSV_FLAG_NONE,
            .Texture2D     = { 0 }
        });
    pass->setCallback([this](rg::PassContext &ctx)
    {
        doForwardPass(ctx);
    });

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

void ForwardRenderer::setLights(
    const Light      *lights,
    size_t            count,
    ResourceManager  &manager,
    ResourceUploader &uploader)
{
    psParamsData_.lightCount = static_cast<int32_t>(count);
    lights_ = manager.createDefaultBuffer(
        sizeof(Light) * count, D3D12_RESOURCE_STATE_COPY_DEST);
    uploader.upload(lights_, lights, sizeof(Light) * count);
    uploader.submitAndSync();
}

void ForwardRenderer::initRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE psTableRange;
    psTableRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

    CD3DX12_ROOT_PARAMETER params[4];
    params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    params[1].InitAsDescriptorTable(1, &psTableRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[3].InitAsShaderResourceView(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    RootSignatureBuilder builder;
    for(auto &p : params)
        builder.addParameter(p);

    builder.addStaticSampler(
        s0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    builder.addFlags(
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    rootSignature_ = builder.build(d3d12_.getDevice());
}

void ForwardRenderer::initPipeline(
    DXGI_FORMAT renderTargetFormat,
    DXGI_FORMAT depthBufferFormat)
{
    const char       *shaderFilename = "./asset/predepth/forward.hlsl";
    const std::string shaderSource   = agz::file::read_txt_file(shaderFilename);

    FXC shaderCompiler;
    shaderCompiler.setWarnings(true);
    
    auto vs = shaderCompiler.compile(
        shaderSource, "vs_5_0", FXC::Options{
            .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
            .sourceName = shaderFilename,
            .entry      = "VSMain"
        });

    auto ps = shaderCompiler.compile(
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
    builder.setRenderTargetFormat(0, renderTargetFormat);
    builder.setDepthStencilFormat(depthBufferFormat);

    builder.setDepthFunc(D3D12_COMPARISON_FUNC_LESS_EQUAL);
    builder.setDepthTest(true, false);
    builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    builder.setCullMode(D3D12_CULL_MODE_BACK);
    builder.setRootSignature(rootSignature_);

    builder.setVertexShader(vs);
    builder.setPixelShader(ps);

    pipeline_ = builder.build(d3d12_.getDevice());
}

void ForwardRenderer::initConstantBuffer()
{
    psParams_.initializeUpload(
        d3d12_.getResourceManager(), d3d12_.getFramebufferCount());
}

void ForwardRenderer::doForwardPass(rg::PassContext &ctx)
{
    auto rawRTV = ctx.getDescriptor(renderTarget_).getCPUHandle();
    auto rawDSV = ctx.getDescriptor(depthBuffer_).getCPUHandle();
    ctx->OMSetRenderTargets(1, &rawRTV, false, &rawDSV);

    ctx->RSSetViewports(1, &viewport_);
    ctx->RSSetScissorRects(1, &scissor_);

    ctx->SetGraphicsRootSignature(rootSignature_.Get());
    ctx->SetPipelineState(pipeline_.Get());

    ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->SetGraphicsRootShaderResourceView(3, lights_.getGPUVirtualAddress());

    psParams_.updateData(ctx.getFrameIndex(), psParamsData_);
    ctx->SetGraphicsRootConstantBufferView(
        2, psParams_.getGPUVirtualAddress(ctx.getFrameIndex()));

    for(auto mesh : meshes_)
    {
        ctx->SetGraphicsRootConstantBufferView(
            0, mesh->vsTransform.getGPUVirtualAddress(ctx.getFrameIndex()));
        ctx->SetGraphicsRootDescriptorTable(1, mesh->descTable[0]);

        auto vtxBufView = mesh->vertexBuffer.getView();
        ctx->IASetVertexBuffers(0, 1, &vtxBufView);

        ctx->DrawInstanced(mesh->vertexBuffer.getVertexCount(), 1, 0, 0);
    }
}
