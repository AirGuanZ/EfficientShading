#include <d3dcompiler.h>

#include <agz/utility/file.h>
#include <agz/utility/mesh.h>

#include "mesh.h"

MeshRenderer::MeshRenderer(D3D12Context &d3d12)
    : device_(d3d12.getDevice()),
      rscMgr_(d3d12.getResourceManager()),
      frameCount_(d3d12.getFramebufferCount()),
      viewport_(),
      scissor_()
{
    initRootSignature();
    initConstantBuffer();
}

rg::Pass *MeshRenderer::addToRenderGraph(
    rg::Graph    &graph,
    rg::Resource *renderTarget,
    rg::Resource *depthStencil)
{
    initPipeline(
        renderTarget->getDescription().Format,
        depthStencil->getDescription().Format);
    
    const auto &RTDesc = renderTarget->getDescription();

    viewport_ = CD3DX12_VIEWPORT(
        0.0f, 0.0f, float(RTDesc.Width), float(RTDesc.Height));
    scissor_  = CD3DX12_RECT(
        0, 0, LONG(RTDesc.Width), LONG(RTDesc.Height));

    const D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        .Texture2D     = { 0, 0 }
    };

    const D3D12_DEPTH_STENCIL_VIEW_DESC DSVDesc = {
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
        .Flags         = D3D12_DSV_FLAG_NONE,
        .Texture2D     = { 0 }
    };

    auto pass = graph.addPass("mesh renderer");

    pass->addRTV(renderTarget, RTVDesc);
    pass->addDSV(depthStencil, DSVDesc);

    pass->setCallback(
        [renderTarget, depthStencil, this](rg::PassContext &ctx)
    {
        const int frame = ctx.getFrameIndex();

        auto rawRTV = ctx.getDescriptor(renderTarget).getCPUHandle();
        auto rawDSV = ctx.getDescriptor(depthStencil).getCPUHandle();
        ctx->OMSetRenderTargets(1, &rawRTV, false, &rawDSV);

        ctx->SetGraphicsRootSignature(rootSignature_.Get());
        ctx->SetPipelineState(pipeline_.Get());

        ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ctx->RSSetViewports(1, &viewport_);
        ctx->RSSetScissorRects(1, &scissor_);

        psParams_.updateData(ctx.getFrameIndex(), psParamsData_);
        ctx->SetGraphicsRootConstantBufferView(
            2, psParams_.getGPUVirtualAddress(frame));

        ctx->SetGraphicsRootShaderResourceView(
            3, lightBuffer_.getGPUVirtualAddress());

        for(auto mesh : meshes_)
        {
            ctx->SetGraphicsRootConstantBufferView(
                0, mesh->vsTransform.getGPUVirtualAddress(frame));
            ctx->SetGraphicsRootDescriptorTable(
                1, mesh->descTable.getDescriptor(0).getGPUHandle());

            auto vtxBufView = mesh->vertexBuffer.getView();
            ctx->IASetVertexBuffers(0, 1, &vtxBufView);

            ctx->DrawInstanced(mesh->vertexBuffer.getVertexCount(), 1, 0, 0);
        }
    });

    return pass;
}

void MeshRenderer::addToRenderQueue(const Mesh *mesh)
{
    meshes_.push_back(mesh);
}

void MeshRenderer::setCamera(const Float3 &eye)
{
    psParamsData_.eye = eye;
}

void MeshRenderer::setLights(
    const Light      *lights,
    size_t            count,
    ResourceManager  &manager,
    ResourceUploader &uploader)
{
    psParamsData_.lightCount = static_cast<int32_t>(count);

    lightBuffer_ = manager.createDefaultBuffer(
        sizeof(Light) * count, D3D12_RESOURCE_STATE_COPY_DEST);
    uploader.upload(lightBuffer_, lights, lightBuffer_.getByteSize());
    uploader.submitAndSync();
}

void MeshRenderer::initRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE psTableRange;
    psTableRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

    CD3DX12_ROOT_DESCRIPTOR_TABLE psTable;
    psTable.Init(1, &psTableRange);

    RootSignatureBuilder builder(device_);
    builder.addParameterCBV(b0,   D3D12_SHADER_VISIBILITY_VERTEX);
    builder.addParameter(psTable, D3D12_SHADER_VISIBILITY_PIXEL);
    builder.addParameterCBV(b1,   D3D12_SHADER_VISIBILITY_PIXEL);
    builder.addParameterSRV(t3,   D3D12_SHADER_VISIBILITY_PIXEL);
    builder.addStaticSampler(
        s0,
        D3D12_SHADER_VISIBILITY_PIXEL,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    builder.addFlags(
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    rootSignature_ = builder.build();
}

void MeshRenderer::initPipeline(DXGI_FORMAT RTFmt, DXGI_FORMAT DSFmt)
{
    FXC compiler;
    compiler.setWarnings(true);

    const std::string src = agz::file::read_txt_file("./asset/basic/mesh.hlsl");

    auto vs = compiler.compile(
        src, "vs_5_0", FXC::Options{
        .macros     = nullptr,
        .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
        .sourceName = "./asset/basic/mesh.hlsl",
        .entry      = "VSMain"
    });

    auto ps = compiler.compile(
        src, "ps_5_0", FXC::Options{
        .macros     = nullptr,
        .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
        .sourceName = "./asset/basic/mesh.hlsl",
        .entry      = "PSMain"
    });

    PipelineBuilder builder(device_);
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
    builder.setRenderTargetFormat(0, RTFmt);
    builder.setDepthStencilFormat(DSFmt);
    builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    builder.setVertexShader(vs);
    builder.setPixelShader(ps);
    builder.setRootSignature(rootSignature_);
    builder.setCullMode(D3D12_CULL_MODE_BACK, false);
    builder.setDepthTest(true, true);

    pipeline_ = builder.build();
}

void MeshRenderer::initConstantBuffer()
{
    psParams_.initializeUpload(rscMgr_, frameCount_);
}
