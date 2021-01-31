#include <d3dcompiler.h>

#include <agz-utils/file.h>

#include "./depth.h"

PreDepthRenderer::PreDepthRenderer(D3D12Context &d3d12)
    : d3d12_(d3d12), depthBuffer_(nullptr), viewport_(), scissor_()
{
    initRootSignature();
}

rg::Pass *PreDepthRenderer::addToRenderGraph(
    rg::Graph    &graph,
    rg::Resource *depthBuffer)
{
    initPipeline(depthBuffer->getDescription().Format);

    depthBuffer_ = depthBuffer;

    viewport_ = depthBuffer_->getDefaultViewport();
    scissor_ = depthBuffer_->getDefaultScissor();

    auto pass = graph.addPass("predepth");
    pass->addDSV(depthBuffer, D3D12_DEPTH_STENCIL_VIEW_DESC{
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
        .Flags         = D3D12_DSV_FLAG_NONE,
        .Texture2D     = { 0 }
    });
    pass->setCallback([this](auto &ctx) { this->doPreDepthPass(ctx); });

    return pass;
}

void PreDepthRenderer::addMesh(const Mesh *mesh)
{
    meshes_.push_back(mesh);
}

void PreDepthRenderer::initRootSignature()
{
    RootSignatureBuilder builder;
    builder.addParameterCBV(b0, D3D12_SHADER_VISIBILITY_VERTEX);
    builder.addFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    rootSignature_ = builder.build(d3d12_.getDevice());
}

void PreDepthRenderer::initPipeline(DXGI_FORMAT depthBufferFormat)
{
    FXC compiler;
    compiler.setWarnings(true);

    const char *shaderFilename = "./asset/predepth/depth.hlsl";

    const std::string shaderSrc = agz::file::read_txt_file(shaderFilename);

    auto vs = compiler.compile(
        shaderSrc, "vs_5_0", FXC::Options{
            .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
            .sourceName = shaderFilename,
            .entry      = "VSMain"
        });

    auto ps = compiler.compile(
        shaderSrc, "ps_5_0", FXC::Options{
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

    builder.setCullMode(D3D12_CULL_MODE_BACK, false);
    builder.setDepthTest(true, true);
    builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    builder.setRootSignature(rootSignature_);

    builder.setVertexShader(vs);
    builder.setPixelShader(ps);

    builder.setDepthStencilFormat(depthBufferFormat);
    builder.setRenderTargetCount(0);

    pipeline_ = builder.build(d3d12_.getDevice());
}

void PreDepthRenderer::doPreDepthPass(rg::PassContext &ctx)
{
    auto rawDSV = ctx.getDescriptor(depthBuffer_).getCPUHandle();
    ctx->ClearDepthStencilView(
        rawDSV, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 1, &scissor_);
    ctx->OMSetRenderTargets(0, nullptr, false, &rawDSV);

    ctx->SetPipelineState(pipeline_.Get());
    ctx->SetGraphicsRootSignature(rootSignature_.Get());

    ctx->RSSetViewports(1, &viewport_);
    ctx->RSSetScissorRects(1, &scissor_);

    ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for(auto mesh : meshes_)
    {
        ctx->SetGraphicsRootConstantBufferView(
            0, mesh->vsTransform.getGPUVirtualAddress(ctx.getFrameIndex()));

        auto vtxBufView = mesh->vertexBuffer.getView();
        ctx->IASetVertexBuffers(0, 1, &vtxBufView);

        ctx->DrawInstanced(mesh->vertexBuffer.getVertexCount(), 1, 0, 0);
    }
}
