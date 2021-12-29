#include <iostream>

#include <agz-utils/file.h>

#include "../common/common.h"

void run()
{
    enableDebugLayerInDebugMode(true);

    // d3d12 context

    WindowDesc windowDesc;
    windowDesc.title      = L"bindless";
    windowDesc.clientSize = { 600, 600 };

    SwapChainDesc swapChainDesc;
    swapChainDesc.imageCount = 3;

    D3D12Context d3d12(windowDesc, swapChainDesc);

    Input *input = d3d12.getInput();

    ResourceUploader uploader(
        d3d12.getDevice(),
        d3d12.createCopyQueue(),
        3,
        d3d12.getResourceManager());

    // textures

    std::vector<UniqueResource> textures;
    {
        const char *TEXTURE_FILENAMES[] = {
            "./asset/bindless/tex0.png",
            "./asset/bindless/tex1.png",
            "./asset/bindless/tex2.png",
            "./asset/bindless/tex3.png"
        };

        TextureLoader textureLoader(d3d12.getResourceManager(), uploader);

        for(auto filename : TEXTURE_FILENAMES)
        {
            textures.push_back(textureLoader.loadFromFile(
                DXGI_FORMAT_R8G8B8A8_UNORM, 1, filename));
        }
    }

    // shader resource views

    auto srvTable = d3d12.getDescriptorAllocator()
        .allocStaticRange(static_cast<uint32_t>(textures.size()));

    for(size_t i = 0; i < textures.size(); ++i)
    {
        const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
            .Format                  = DXGI_FORMAT_R8G8B8A8_UNORM,
            .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D               = D3D12_TEX2D_SRV{
                .MostDetailedMip     = 0,
                .MipLevels           = 1,
                .PlaneSlice          = 0,
                .ResourceMinLODClamp = 0
            }
        };

        d3d12.getDevice()->CreateShaderResourceView(
            textures[i]->resource.Get(), &srvDesc, srvTable[i]);
    }

    // root signature

    // 0: 32-bit psTextureIndex (b0)
    // 1: psTable               (t0)
    ComPtr<ID3D12RootSignature> rootSignature;

    {
        CD3DX12_DESCRIPTOR_RANGE srvTableRanges[1] = {};
        srvTableRanges[0].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            static_cast<UINT>(textures.size()), 0);

        CD3DX12_ROOT_PARAMETER params[2] = {};
        params[0].InitAsConstants(1, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsDescriptorTable(
            1, srvTableRanges, D3D12_SHADER_VISIBILITY_PIXEL);

        RootSignatureBuilder builder;
        builder.addParameters(params);

        builder.addStaticSampler(
            s0, D3D12_SHADER_VISIBILITY_PIXEL,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        builder.addFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        rootSignature = builder.build(d3d12.getDevice());
    }

    // vertex buffer

    struct Vertex
    {
        Float2 position;
        Float2 texCoord;
    };

    const Vertex vertexData[] = {
        { { -1, -1 }, { 0, 1 } },
        { { -1, +0 }, { 0, 0 } },
        { { +0, +0 }, { 1, 0 } },
        { { +0, -1 }, { 1, 1 } },

        { { -1, +0 }, { 0, 1 } },
        { { -1, +1 }, { 0, 0 } },
        { { +0, +1 }, { 1, 0 } },
        { { +0, +0 }, { 1, 1 } },

        { { +0, +0 }, { 0, 1 } },
        { { +0, +1 }, { 0, 0 } },
        { { +1, +1 }, { 1, 0 } },
        { { +1, +0 }, { 1, 1 } },

        { { +0, -1 }, { 0, 1 } },
        { { +0, +0 }, { 0, 0 } },
        { { +1, +0 }, { 1, 0 } },
        { { +1, -1 }, { 1, 1 } },
    };

    const uint16_t indexData[] = { 0,  1,  2,  0,  2,  3 };

    VertexBuffer<Vertex> vertexBuffer;
    vertexBuffer.initializeDefault(
        d3d12.getResourceManager(),
        agz::array_size(vertexData),
        D3D12_RESOURCE_STATE_COMMON);
    uploader.upload(vertexBuffer, vertexData, sizeof(vertexData));

    IndexBuffer<uint16_t> indexBuffer;
    indexBuffer.initializeDefault(
        d3d12.getResourceManager(),
        agz::array_size(indexData),
        D3D12_RESOURCE_STATE_COMMON);
    uploader.upload(indexBuffer.getBuffer(), indexData, sizeof(indexData));

    uploader.submitAndSync();

    // pipeline state

    ComPtr<ID3D12PipelineState> pipelineState;

    {
        FXC compiler;
        compiler.setWarnings(true);

        const std::string textureCountStr = std::to_string(textures.size());
        D3D_SHADER_MACRO macros[2] = {
            { "TEXTURE_COUNT", textureCountStr.c_str() },
            { nullptr, nullptr }
        };

        const char *shaderFilename = "./asset/bindless/bindless.hlsl";
        const std::string shaderSource =
            agz::file::read_txt_file(shaderFilename);

        auto vs = compiler.compile(
            shaderSource, "vs_5_1", FXC::Options{
                .macros = macros,
                .entry  = "VSMain"
            });

        auto ps = compiler.compile(
            shaderSource, "ps_5_1", FXC::Options{
                .macros = macros,
                .entry  = "PSMain"
            });

        PipelineBuilder builder;

        builder.addInputElement({
            "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,
            0, offsetof(Vertex, position),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        });
        builder.addInputElement({
            "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
            0, offsetof(Vertex, texCoord),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        });
        builder.setRootSignature(rootSignature.Get());
        builder.setVertexShader(vs);
        builder.setPixelShader(ps);
        builder.setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        builder.setRenderTargetCount(1);
        builder.setRenderTargetFormat(0, d3d12.getFramebufferFormat());
        builder.setDepthTest(false, false);

        pipelineState = builder.build(d3d12.getDevice());
    }

    // texture indices

    int32_t textureIndices[4] = { };
    for(int i = 0; i < 4; ++i)
        textureIndices[i] = i % textures.size();

    // render graph

    rg::Graph graph;

    auto initGraph = [&]
    {
        graph = rg::Graph();
        graph.setFrameCount(d3d12.getFramebufferCount());
        graph.setQueueCount(1);
        graph.setThreadCount(1);

        auto framebuffer = graph.addExternalResource("framebuffer");
        framebuffer->setInitialState(D3D12_RESOURCE_STATE_PRESENT);
        framebuffer->setFinalState(D3D12_RESOURCE_STATE_PRESENT);
        framebuffer->setDescription(d3d12.getFramebuffer()->GetDesc());
        framebuffer->setPerFrame();

        auto renderPass = graph.addPass("render");
        renderPass->addRTV(
            framebuffer,
            D3D12_RENDER_TARGET_VIEW_DESC{
                .Format        = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                .Texture2D     = D3D12_TEX2D_RTV{ 0, 0 }
            });
        renderPass->setCallback(
            [
                &textureIndices,
                &rootSignature,
                &pipelineState,
                &srvTable,
                &vertexBuffer,
                &indexBuffer,
                &graph,
                framebuffer,
                viewport = framebuffer->getDefaultViewport(),
                scissor = framebuffer->getDefaultScissor()
            ](rg::PassContext &ctx)
        {
            const float BACKGROUND[4] = { 0, 1, 1, 0 };

            auto rawRTV = ctx.getDescriptor(framebuffer).getCPUHandle();
            ctx->ClearRenderTargetView(rawRTV, BACKGROUND, 1, &scissor);
            ctx->OMSetRenderTargets(1, &rawRTV, false, nullptr);

            ctx->RSSetViewports(1, &viewport);
            ctx->RSSetScissorRects(1, &scissor);

            ctx->SetGraphicsRootSignature(rootSignature.Get());
            ctx->SetPipelineState(pipelineState.Get());

            ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            auto vtxBufView = vertexBuffer.getView();
            ctx->IASetVertexBuffers(0, 1, &vtxBufView);

            auto indexBufView = indexBuffer.getView();
            ctx->IASetIndexBuffer(&indexBufView);

            ctx->SetGraphicsRootDescriptorTable(1, srvTable[0]);

            ctx->SetGraphicsRoot32BitConstants(0, 1, &textureIndices[0], 0);
            ctx->DrawIndexedInstanced(6, 1, 0, 0, 0);

            ctx->SetGraphicsRoot32BitConstants(0, 1, &textureIndices[1], 0);
            ctx->DrawIndexedInstanced(6, 1, 0, 4, 0);

            ctx->SetGraphicsRoot32BitConstants(0, 1, &textureIndices[2], 0);
            ctx->DrawIndexedInstanced(6, 1, 0, 8, 0);

            ctx->SetGraphicsRoot32BitConstants(0, 1, &textureIndices[3], 0);
            ctx->DrawIndexedInstanced(6, 1, 0, 12, 0);
        });

        auto imguiPass = d3d12.addImGuiToRenderGraph(graph, framebuffer);

        graph.addDependency(renderPass, imguiPass);

        graph.compile(
            d3d12.getDevice(),
            d3d12.getResourceManager(),
            d3d12.getDescriptorAllocator(),
            { d3d12.getGraphicsQueue() });

        for(int i = 0; i < d3d12.getFramebufferCount(); ++i)
        {
            graph.setExternalResource(
                framebuffer, i, d3d12.getFramebuffer(i).Get());
        }
    };

    initGraph();

    d3d12.attach(std::make_shared<SwapChainPostResizeHandler>([&]
    {
        initGraph();
    }));

    while(!d3d12.getCloseFlag())
    {
        d3d12.startFrame();

        if(input->isDown(KEY_ESCAPE))
            d3d12.setCloseFlag(true);

        if(ImGui::Begin("bindless", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const int textureCount = static_cast<int>(textures.size());
            ImGui::SliderInt("quad0", &textureIndices[0], 0, textureCount - 1);
            ImGui::SliderInt("quad1", &textureIndices[1], 0, textureCount - 1);
            ImGui::SliderInt("quad2", &textureIndices[2], 0, textureCount - 1);
            ImGui::SliderInt("quad3", &textureIndices[3], 0, textureCount - 1);
        }
        ImGui::End();

        graph.run(d3d12.getFramebufferIndex());

        d3d12.swapFramebuffers();
        d3d12.endFrame();
    }

    d3d12.waitForIdle();
}

int main()
{
    try
    {
        run();
    }
    catch(const std::exception &e)
    {
        agz::misc::extract_hierarchy_exceptions(
            e, std::ostream_iterator<std::string>(std::cerr, "\n"));
        return -1;
    }
}
