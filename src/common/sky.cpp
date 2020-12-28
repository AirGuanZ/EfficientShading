#include <agz/utility/file.h>
#include <agz/utility/image.h>

#include "sky.h"

SkyRenderer::SkyRenderer(
    D3D12Context     &d3d12,
    ResourceUploader &uploader)
    : d3d12_(d3d12),
      uploader_(uploader),
      viewport_(),
      scissor_()
{

}

void SkyRenderer::loadSkyBox(
    const std::string &posX,
    const std::string &negX,
    const std::string &posY,
    const std::string &negY,
    const std::string &posZ,
    const std::string &negZ)
{
    if(cubeTexSRV_)
    {
        d3d12_.freeStatic(cubeTexSRV_);
        cubeTexSRV_ = {};
    }

    const auto posXImg = agz::img::load_rgba_from_file(posX);
    const auto posYImg = agz::img::load_rgba_from_file(posY);
    const auto posZImg = agz::img::load_rgba_from_file(posZ);
    const auto negXImg = agz::img::load_rgba_from_file(negX);
    const auto negYImg = agz::img::load_rgba_from_file(negY);
    const auto negZImg = agz::img::load_rgba_from_file(negZ);

    const UINT width  = static_cast<UINT>(posXImg.shape()[1]);
    const UINT height = static_cast<UINT>(posXImg.shape()[0]);

    const D3D12_RESOURCE_DESC cubeTexDesc = {
        .Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment        = 0,
        .Width            = width,
        .Height           = height,
        .DepthOrArraySize = 6,
        .MipLevels        = 1,
        .Format           = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc       = { 1, 0 },
        .Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags            = D3D12_RESOURCE_FLAG_NONE
    };

    cubeTex_ = d3d12_.create(
        D3D12_HEAP_TYPE_DEFAULT,
        cubeTexDesc,
        D3D12_RESOURCE_STATE_COMMON);

    std::vector<ResourceUploader::Texture2DInitData> initData = {
        { posXImg.raw_data() }, { negXImg.raw_data() },
        { posYImg.raw_data() }, { negYImg.raw_data() },
        { posZImg.raw_data() }, { negZImg.raw_data() }
    };

    uploader_.uploadTexture2D(cubeTex_->resource.Get(), initData.data());
    uploader_.submit();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
        .Format                  = DXGI_FORMAT_R8G8B8A8_UNORM,
        .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURECUBE,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
    };

    srvDesc.TextureCube = D3D12_TEXCUBE_SRV{
        .MostDetailedMip     = 0,
        .MipLevels           = 1,
        .ResourceMinLODClamp = 0
    };

    cubeTexSRV_ = d3d12_.allocStatic();
    d3d12_.getDevice()->CreateShaderResourceView(
        cubeTex_->resource.Get(), &srvDesc, cubeTexSRV_);

    uploader_.sync();
}

rg::Vertex *SkyRenderer::addToRenderGraph(
    rg::Graph    &graph,
    rg::Resource *renderTarget)
{
    initVertexBuffer();
    initRootSignature();
    initPipeline(renderTarget->getDescription()->Format);
    initConstantBuffer();

    const auto desc = renderTarget->getDescription();

    viewport_ = D3D12_VIEWPORT{
        .TopLeftX = 0,
        .TopLeftY = 0,
        .Width    = static_cast<float>(desc->Width),
        .Height   = static_cast<float>(desc->Height),
        .MinDepth = 0,
        .MaxDepth = 1
    };

    scissor_ = D3D12_RECT{
        .left   = 0,
        .top    = 0,
        .right  = static_cast<LONG>(desc->Width),
        .bottom = static_cast<LONG>(desc->Height)
    };

    D3D12_RENDER_TARGET_VIEW_DESC RTVDesc;
    RTVDesc.Format        = DXGI_FORMAT_UNKNOWN;
    RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    RTVDesc.Texture2D     = { 0, 0 };

    auto vtx = graph.addVertex("render sky box");
    vtx->useResource(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, RTVDesc);
    vtx->setCallback([=](rg::PassContext &ctx)
    {
        if(!cubeTexSRV_)
            return;

        vsTransform_.updateData(ctx.getFrameIndex(), { eye_, 0, viewProj_ });

        auto RTV = ctx.getDescriptor(renderTarget).getCPUHandle();
        ctx->OMSetRenderTargets(1, &RTV, false, nullptr);

        ctx->SetPipelineState(pipeline_.Get());
        ctx->SetGraphicsRootSignature(rootSignature_.Get());

        ctx->RSSetViewports(1, &viewport_);
        ctx->RSSetScissorRects(1, &scissor_);

        ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const auto vertexBufferView = vertexBuffer_.getView();
        ctx->IASetVertexBuffers(0, 1, &vertexBufferView);

        ctx->SetGraphicsRootConstantBufferView(
            0, vsTransform_.getGPUVirtualAddress(ctx.getFrameIndex()));
        ctx->SetGraphicsRootDescriptorTable(
            1, cubeTexSRV_.getGPUHandle());
        
        ctx->DrawInstanced(vertexBuffer_.getVertexCount(), 1, 0, 0);
    });

    return vtx;
}

void SkyRenderer::setCamera(const Float3 &eye, const Mat4 &viewProj) noexcept
{
    eye_      = eye;
    viewProj_ = viewProj;
}

void SkyRenderer::initVertexBuffer()
{
    const Vertex vertexData[] = {
        { { +1, -1, -1 } }, { { +1, +1, -1 } }, { { +1, +1, +1 } },
        { { +1, -1, -1 } }, { { +1, +1, +1 } }, { { +1, -1, +1 } },

        { { -1, -1, +1 } }, { { -1, +1, +1 } }, { { -1, +1, -1 } },
        { { -1, -1, +1 } }, { { -1, +1, -1 } }, { { -1, -1, -1 } },

        { { +1, +1, +1 } }, { { +1, +1, -1 } }, { { -1, +1, -1 } },
        { { +1, +1, +1 } }, { { -1, +1, -1 } }, { { -1, +1, +1 } },

        { { -1, -1, +1 } }, { { -1, -1, -1 } }, { { +1, -1, -1 } },
        { { -1, -1, +1 } }, { { +1, -1, -1 } }, { { +1, -1, +1 } },

        { { +1, -1, +1 } }, { { +1, +1, +1 } }, { { -1, +1, +1 } },
        { { +1, -1, +1 } }, { { -1, +1, +1 } }, { { -1, -1, +1 } },

        { { -1, -1, -1 } }, { { -1, +1, -1 } }, { { +1, +1, -1 } },
        { { -1, -1, -1 } }, { { +1, +1, -1 } }, { { +1, -1, -1 } }
    };

    vertexBuffer_.initializeDefault(
        d3d12_.getResourceManager(),
        agz::array_size(vertexData),
        D3D12_RESOURCE_STATE_COMMON);

    uploader_.upload(vertexBuffer_, vertexData, agz::array_size(vertexData));

    uploader_.submit();
    uploader_.sync();
}

void SkyRenderer::initRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE descRange;
    descRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_DESCRIPTOR_TABLE table;
    table.Init(1, &descRange);

    RootSignatureBuilder builder(d3d12_.getDevice());
    builder.addFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    builder.addParameterCBV(b0, D3D12_SHADER_VISIBILITY_VERTEX);
    builder.addParameter(table, D3D12_SHADER_VISIBILITY_PIXEL);
    builder.addStaticSampler(
        s0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    rootSignature_ = builder.build();
}

void SkyRenderer::initPipeline(DXGI_FORMAT RTFmt)
{
    FXC compiler;
    compiler.setWarnings(true);

    const auto VSSrc = agz::file::read_txt_file("./asset/common/sky_vs.hlsl");
    const auto PSSrc = agz::file::read_txt_file("./asset/common/sky_ps.hlsl");

    PipelineBuilder builder(d3d12_.getDevice());
    builder.addInputElement({
        "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
        0, offsetof(Vertex, position),
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        });
    builder.setRenderTargetCount(1);
    builder.setRenderTargetFormat(0, RTFmt);
    builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    builder.setVertexShader(compiler.compile(VSSrc, "vs_5_0"));
    builder.setPixelShader(compiler.compile(PSSrc, "ps_5_0"));
    builder.setRootSignature(rootSignature_);
    builder.setCullMode(D3D12_CULL_MODE_BACK, true);

    pipeline_ = builder.build();
}

void SkyRenderer::initConstantBuffer()
{
    vsTransform_.initializeUpload(
        d3d12_.getResourceManager(),
        d3d12_.getFramebufferCount());
}
