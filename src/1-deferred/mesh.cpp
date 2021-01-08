#include <d3dcompiler.h>

#include <agz-utils/file.h>

#include "./mesh.h"

MeshRenderer::MeshRenderer(D3D12Context &d3d12)
    : d3d12_(d3d12),
      viewport_(),
      scissor_(),
      gbufferARsc_(nullptr),
      gbufferBRsc_(nullptr),
      gbufferDepthRsc_(nullptr),
      renderTargetRsc_(nullptr),
      psTable_(nullptr)
{
    initRootSignature();
    initConstantBuffer();
}

rg::Vertex *MeshRenderer::addToRenderGraph(
    rg::Graph    &graph,
    rg::Resource *renderTarget)
{
    initPipeline(renderTarget->getDescription().Format);

    const UINT64 w = renderTarget->getDescription().Width;
    const UINT   h = renderTarget->getDescription().Height;
    
    viewport_ = CD3DX12_VIEWPORT(0.0f, 0.0f, float(w), float(h));
    scissor_  = CD3DX12_RECT(0, 0, LONG(w), LONG(h));

    // internal resources

    gbufferARsc_     = graph.addInternalResource("gbuffer a");
    gbufferBRsc_     = graph.addInternalResource("gbuffer b");
    gbufferDepthRsc_ = graph.addInternalResource("gbuffer depth");

    gbufferARsc_->setDescription(CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32G32B32A32_FLOAT, w, h, 1, 1,
        1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET));
    gbufferARsc_->setClearValue(D3D12_CLEAR_VALUE{
        .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
        .Color  = { 0, 0, 0, 0 }
    });

    gbufferBRsc_->setDescription(CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, 1,
        1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET));
    gbufferBRsc_->setClearValue(D3D12_CLEAR_VALUE{
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .Color  = { 0, 0, 0, 0 }
    });

    gbufferDepthRsc_->setDescription(CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_TYPELESS, w, h, 1, 1,
        1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
    gbufferDepthRsc_->setClearValue(D3D12_CLEAR_VALUE{
        .Format       = DXGI_FORMAT_D32_FLOAT,
        .DepthStencil = { 1, 0 }
    });

    // imported resource

    renderTargetRsc_ = renderTarget;

    // gbuffer pass

    auto gbufferPass = graph.addPass("gbuffer");

    gbufferPass->addRTV(
        gbufferARsc_,
        D3D12_RENDER_TARGET_VIEW_DESC{
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
            .Texture2D     = { 0, 0 }
        });
    gbufferPass->addRTV(
        gbufferBRsc_,
        D3D12_RENDER_TARGET_VIEW_DESC{
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
            .Texture2D     = { 0, 0 }
        });
    gbufferPass->addDSV(
        gbufferDepthRsc_,
        D3D12_DEPTH_STENCIL_VIEW_DESC{
            .Format        = DXGI_FORMAT_D32_FLOAT,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags         = D3D12_DSV_FLAG_NONE,
            .Texture2D     = { 0 }
        });

    gbufferPass->setCallback(
        [this](rg::PassContext &ctx)
    {
        doGBufferPass(ctx);
    });

    // lighting pass

    auto lightingPass = graph.addPass("lighting");

    psTable_ = lightingPass->addDescriptorTable(rg::Pass::GPUOnly);
    psTable_->addSRV(
        gbufferARsc_,
        rg::ShaderResourceType::PixelOnly,
        D3D12_SHADER_RESOURCE_VIEW_DESC{
            .Format                  = DXGI_FORMAT_R32G32B32A32_FLOAT,
            .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = D3D12_TEX2D_SRV{
                .MostDetailedMip     = 0,
                .MipLevels           = 1,
                .PlaneSlice          = 0,
                .ResourceMinLODClamp = 0
            }
        });
    psTable_->addSRV(
        gbufferBRsc_,
        rg::ShaderResourceType::PixelOnly,
        D3D12_SHADER_RESOURCE_VIEW_DESC{
            .Format                  = DXGI_FORMAT_R8G8B8A8_UNORM,
            .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = D3D12_TEX2D_SRV{
                .MostDetailedMip     = 0,
                .MipLevels           = 1,
                .PlaneSlice          = 0,
                .ResourceMinLODClamp = 0
            }
        });
    psTable_->addSRV(
        gbufferDepthRsc_,
        rg::ShaderResourceType::PixelOnly,
        D3D12_SHADER_RESOURCE_VIEW_DESC{
            .Format                  = DXGI_FORMAT_R32_FLOAT,
            .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = D3D12_TEX2D_SRV{
                .MostDetailedMip     = 0,
                .MipLevels           = 1,
                .PlaneSlice          = 0,
                .ResourceMinLODClamp = 0
            }
        });
    lightingPass->addRTV(
        renderTargetRsc_,
        D3D12_RENDER_TARGET_VIEW_DESC{
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
            .Texture2D     = { 0, 0 }
        });

    lightingPass->setCallback([this](rg::PassContext &ctx)
    {
        doLightingPass(ctx);
    });

    graph.addDependency(gbufferPass, lightingPass);

    return graph.addAggregate("render mesh", gbufferPass, lightingPass);
}

void MeshRenderer::addMesh(const Mesh *mesh)
{
    meshes_.push_back(mesh);
}

void MeshRenderer::setCamera(const Mat4 &viewProj, const Float3 &eye)
{
    psParamsData_.invViewProj = viewProj.inv();
    psParamsData_.eye = eye;
}

void MeshRenderer::setLights(
    const Light      *lights,
    size_t            count,
    ResourceManager  &manager,
    ResourceUploader &uploader)
{
    psParamsData_.lightCount = static_cast<int32_t>(count);
    lightBuffer_.initializeDefault(
        manager, sizeof(Light) * count, D3D12_RESOURCE_STATE_COPY_DEST);
    uploader.upload(lightBuffer_, lights, sizeof(Light) * count);
    uploader.submitAndSync();
}

void MeshRenderer::initRootSignature()
{
    // gbuffer root signature

    {
        CD3DX12_DESCRIPTOR_RANGE psTableRange;
        psTableRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

        CD3DX12_ROOT_PARAMETER params[2];
        params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        params[1].InitAsDescriptorTable(1, &psTableRange, D3D12_SHADER_VISIBILITY_PIXEL);

        RootSignatureBuilder builder(d3d12_.getDevice());
        for(auto &p : params)
            builder.addParameter(p);

        builder.addStaticSampler(
            s0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        builder.addFlags(
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        gbufferRootSignature_ = builder.build();
    }

    // lighting root signature

    {
        CD3DX12_DESCRIPTOR_RANGE psTableRange;
        psTableRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1);
        
        CD3DX12_ROOT_PARAMETER params[3];
        params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsShaderResourceView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        params[2].InitAsDescriptorTable(1, &psTableRange, D3D12_SHADER_VISIBILITY_PIXEL);

        RootSignatureBuilder builder(d3d12_.getDevice());
        for(auto &p : params)
            builder.addParameter(p);

        builder.addStaticSampler(
            s0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_POINT);

        lightingRootSignature_ = builder.build();
    }
}

void MeshRenderer::initPipeline(DXGI_FORMAT RTFmt)
{
    FXC compiler;

    // gbuffer pipeline

    {
        const std::string shader =
            agz::file::read_txt_file("./asset/deferred/gbuffer.hlsl");

        PipelineBuilder builder(d3d12_.getDevice());
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

        builder.setCullMode(D3D12_CULL_MODE_BACK, false);
        builder.setDepthTest(true, true);
        builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        builder.setRootSignature(gbufferRootSignature_);

        builder.setVertexShader(compiler.compile(
            shader, "vs_5_0", FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = "./asset/deferred/gbuffer.hlsl",
                .entry      = "VSMain"
            }));
        builder.setPixelShader(compiler.compile(
            shader, "ps_5_0", FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = "./asset/deferred/gbuffer.hlsl",
                .entry      = "PSMain"
            }));

        builder.setDepthStencilFormat(DXGI_FORMAT_D32_FLOAT);
        builder.setRenderTargetCount(2);
        builder.setRenderTargetFormat(0, DXGI_FORMAT_R32G32B32A32_FLOAT);
        builder.setRenderTargetFormat(1, DXGI_FORMAT_R8G8B8A8_UNORM);

        gbufferPipeline_ = builder.build();
    }

    // lighting pipeline

    {
        const std::string shader =
            agz::file::read_txt_file("./asset/deferred/lighting.hlsl");

        PipelineBuilder builder(d3d12_.getDevice());

        builder.setCullMode(D3D12_CULL_MODE_NONE);
        builder.setDepthTest(false, false);
        builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        builder.setRootSignature(lightingRootSignature_);

        builder.setVertexShader(compiler.compile(
            shader, "vs_5_0", FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = "./asset/deferred/lighting.hlsl",
                .entry      = "VSMain"
            }));
        builder.setPixelShader(compiler.compile(
            shader, "ps_5_0", FXC::Options{
                .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
                .sourceName = "./asset/deferred/lighting.hlsl",
                .entry      = "PSMain"
            }));

        builder.setRenderTargetCount(1);
        builder.setRenderTargetFormat(0, RTFmt);

        lightingPipeline_ = builder.build();
    }
}

void MeshRenderer::initConstantBuffer()
{
    psParams_.initializeUpload(
        d3d12_.getResourceManager(), d3d12_.getFramebufferCount());
}

void MeshRenderer::doGBufferPass(rg::PassContext &ctx)
{
    // clear render targets & depth buffer

    const float clearColor[4] = { 0, 0, 0, 0 };

    D3D12_CPU_DESCRIPTOR_HANDLE rawRTVs[] = {
        ctx.getDescriptor(gbufferARsc_).getCPUHandle(),
        ctx.getDescriptor(gbufferBRsc_).getCPUHandle()
    };
    D3D12_CPU_DESCRIPTOR_HANDLE rawDSV =
        ctx.getDescriptor(gbufferDepthRsc_).getCPUHandle();

    ctx->ClearRenderTargetView(rawRTVs[0], clearColor, 1, &scissor_);
    ctx->ClearRenderTargetView(rawRTVs[1], clearColor, 1, &scissor_);
    ctx->ClearDepthStencilView(
        rawDSV, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 1, &scissor_);

    // set state objs

    ctx->OMSetRenderTargets(2, rawRTVs, false, &rawDSV);
    ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->SetGraphicsRootSignature(gbufferRootSignature_.Get());
    ctx->SetPipelineState(gbufferPipeline_.Get());
    ctx->RSSetViewports(1, &viewport_);
    ctx->RSSetScissorRects(1, &scissor_);

    // emit draw calls

    for(auto mesh : meshes_)
    {
        ctx->SetGraphicsRootConstantBufferView(
            0,
            mesh->vsTransform.getGPUVirtualAddress(
                d3d12_.getFramebufferIndex()));
        ctx->SetGraphicsRootDescriptorTable(
            1, mesh->descTable.getDescriptor(0).getGPUHandle());

        auto rawVtxBufView = mesh->vertexBuffer.getView();
        ctx->IASetVertexBuffers(0, 1, &rawVtxBufView);

        ctx->DrawInstanced(mesh->vertexBuffer.getVertexCount(), 1, 0, 0);
    }
}

void MeshRenderer::doLightingPass(rg::PassContext &ctx)
{
    // state objs

    auto rawRTV = ctx.getDescriptor(renderTargetRsc_).getCPUHandle();
    ctx->OMSetRenderTargets(1, &rawRTV, false, nullptr);

    ctx->SetPipelineState(lightingPipeline_.Get());
    ctx->SetGraphicsRootSignature(lightingRootSignature_.Get());
    ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->RSSetViewports(1, &viewport_);
    ctx->RSSetScissorRects(1, &scissor_);

    // constant buffer

    psParams_.updateData(ctx.getFrameIndex(), psParamsData_);
    ctx->SetGraphicsRootConstantBufferView(
        0, psParams_.getGPUVirtualAddress(ctx.getFrameIndex()));

    // light buffer

    ctx->SetGraphicsRootShaderResourceView(
        1, lightBuffer_.getGPUVirtualAddress());

    // gbuffers

    auto psTable = ctx.getDescriptorRange(psTable_);
    ctx->SetGraphicsRootDescriptorTable(2, psTable[0]);

    ctx->DrawInstanced(3, 1, 0, 0);
}
