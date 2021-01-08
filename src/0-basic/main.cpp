#include <iostream>

#include <agz-utils/graphics_api.h>
#include <agz-utils/time.h>

#include "../common/camera.h"
#include "../common/sky.h"

#include "./mesh.h"

void run()
{
    enableDebugLayer(false);

    // d3d12 context

    WindowDesc windowDesc;
    windowDesc.title      = L"forward";
    windowDesc.clientSize = { 800, 600 };

    SwapChainDesc swapChainDesc;
    swapChainDesc.imageCount = 3;
    
    D3D12Context d3d12(windowDesc, swapChainDesc);

    Input *input = d3d12.getInput();

    ResourceUploader uploader(
        d3d12.getDevice(),
        d3d12.createCopyQueue(),
        3,
        d3d12.getResourceManager());

    // sky

    common::SkyRenderer skyRenderer(d3d12, uploader);

    skyRenderer.loadSkyBox(
        "./asset/sky/1ft.jpg",
        "./asset/sky/1bk.jpg",
        "./asset/sky/1up.jpg",
        "./asset/sky/1dn.jpg",
        "./asset/sky/1lt.jpg",
        "./asset/sky/1rt.jpg");

    // mesh

    MeshRenderer meshRenderer(d3d12);

    MeshRenderer::Mesh mesh;
    mesh.load(
        d3d12, uploader,
        "./asset/mesh/eglise/mesh.obj",
        "./asset/mesh/eglise/albedo.png",
        "./asset/mesh/eglise/metallic.png",
        "./asset/mesh/eglise/roughness.png");

    meshRenderer.addToRenderQueue(&mesh);

    // camera

    common::Camera camera;
    camera.setSpeed(0.03f);
    camera.setPosition({ 0, -4, 0 });

    input->showCursor(false);
    input->setCursorLock(
        true, d3d12.getClientWidth() / 2, d3d12.getClientHeight() / 2);

    // render graph

    rg::Graph graph;
    rg::ExternalResource *renderTargetRsc = nullptr;
    rg::InternalResource *depthStencilRsc = nullptr;

    auto initGraph = [&]
    {
        graph = rg::Graph();
        graph.setFrameCount(d3d12.getFramebufferCount());
        graph.setQueueCount(1);
        graph.setThreadCount(1);

        // create resource nodes

        D3D12_CLEAR_VALUE depthStencilClearValue;
        depthStencilClearValue.Format               = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthStencilClearValue.DepthStencil.Depth   = 1;
        depthStencilClearValue.DepthStencil.Stencil = 0;
        
        renderTargetRsc = graph.addExternalResource("framebuffer");
        renderTargetRsc->setDescription(d3d12.getFramebuffer()->GetDesc());
        renderTargetRsc->setInitialState(D3D12_RESOURCE_STATE_PRESENT);
        renderTargetRsc->setFinalState(D3D12_RESOURCE_STATE_PRESENT);

        depthStencilRsc = graph.addInternalResource("depth stencil buffer");
        depthStencilRsc->setDescription(CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D24_UNORM_S8_UINT,
            d3d12.getClientWidth(),
            d3d12.getClientHeight(),
            1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
        depthStencilRsc->setClearValue(depthStencilClearValue);
        depthStencilRsc->setInitialState(D3D12_RESOURCE_STATE_DEPTH_WRITE);

        // clear pass

        D3D12_RENDER_TARGET_VIEW_DESC RTVDesc;
        RTVDesc.Format        = DXGI_FORMAT_UNKNOWN;
        RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        RTVDesc.Texture2D     = { 0, 0 };

        D3D12_DEPTH_STENCIL_VIEW_DESC DSVDesc;
        DSVDesc.Format        = DXGI_FORMAT_UNKNOWN;
        DSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        DSVDesc.Flags         = D3D12_DSV_FLAG_NONE;
        DSVDesc.Texture2D     = { 0 };

        auto clearPass = graph.addPass("clear");
        clearPass->addRTV(
            renderTargetRsc, RTVDesc);
        clearPass->addDSV(
            depthStencilRsc, DSVDesc);
        clearPass->setCallback([&](rg::PassContext &ctx)
        {
            const float background[4] = { 0, 1, 1, 0 };
            ctx->ClearRenderTargetView(
                ctx.getDescriptor(renderTargetRsc), background,
                1, &d3d12.getDefaultScissorRect());

            ctx->ClearDepthStencilView(
                ctx.getDescriptor(depthStencilRsc),
                D3D12_CLEAR_FLAG_DEPTH, 1, 0, 1,
                &d3d12.getDefaultScissorRect());
        });

        // sky pass

        auto skyPass = skyRenderer.addToRenderGraph(
            graph, renderTargetRsc);

        // mesh pass

        auto meshPass = meshRenderer.addToRenderGraph(
            graph, renderTargetRsc, depthStencilRsc);

        // imgui pass

        auto imguiPass = d3d12.addImGuiToRenderGraph(
            graph, renderTargetRsc);

        // arcs

        graph.addDependency(clearPass, skyPass);
        graph.addDependency(skyPass, meshPass);
        graph.addDependency(meshPass, imguiPass);

        graph.compile(
            d3d12.getDevice(),
            d3d12.getResourceManager(),
            d3d12.getDescriptorAllocator(),
            { d3d12.getGraphicsQueue() });
    };

    initGraph();

    d3d12.attach(std::make_shared<SwapChainPostResizeHandler>(
        [&]
    {
        d3d12.waitForIdle();
        input->setCursorLock(
            input->isCursorLocked(),
            d3d12.getClientWidth() / 2,
            d3d12.getClientHeight() / 2);
        initGraph();
    }));

    std::vector<MeshRenderer::Light> lights = {
        MeshRenderer::Light{
            .lightPosition    = { 2, 0, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(0, 1, 2),
            .lightAmbient     = Float3(0)
        },
        MeshRenderer::Light{
            .lightPosition    = { 0, -3, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(2, 0, 1),
            .lightAmbient     = Float3(0)
        },
        MeshRenderer::Light{
            .lightPosition    = { -10, -1.5, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(1, 2, 0),
            .lightAmbient     = Float3(0)
        },
        MeshRenderer::Light{
            .lightPosition    = { -10, -9, 0 },
            .maxLightDistance = 3,
            .lightIntensity   = Float3(7, 0, 0),
            .lightAmbient     = Float3(0)
        }
    };

    meshRenderer.setLights(
        lights.data(), lights.size(), d3d12.getResourceManager(), uploader);

    agz::time::fps_counter_t fpsCounter;
    
    // mainloop

    while(!d3d12.getCloseFlag())
    {
        d3d12.startFrame(true);
        
        if(input->isPressed(KEY_ESCAPE))
            d3d12.setCloseFlag(true);

        if(d3d12.getInput()->isDown(KEY_LCTRL))
        {
            input->showCursor(!input->isCursorVisible());
            input->setCursorLock(!input->isCursorLocked());
        }

        if(ImGui::Begin("forward", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("fps: %d", fpsCounter.fps());

            const auto &eyePos = camera.getPosition();
            ImGui::Text(
                "camera position: %f, %f, %f",
                eyePos.x, eyePos.y, eyePos.z);
        }
        ImGui::End();

        camera.setWOverH(d3d12.getFramebufferWOverH());
        camera.update({
            input->isPressed('W'),
            input->isPressed('A'),
            input->isPressed('D'),
            input->isPressed('S'),
            input->isPressed(KEY_SPACE),
            input->isPressed(KEY_LSHIFT),
            static_cast<float>(input->getCursorRelativeX()),
            static_cast<float>(input->getCursorRelativeY())
        });

        skyRenderer.setCamera(camera.getPosition(), camera.getViewProj());
        meshRenderer.setCamera(camera.getPosition());

        const Mat4 world = Mat4::right_transform::scale(Float3(0.3f));
        mesh.vsTransform.updateData(
            d3d12.getFramebufferIndex(),
            { world, world * camera.getViewProj() });

        graph.setExternalResource(renderTargetRsc, d3d12.getFramebuffer());
        graph.run(d3d12.getFramebufferIndex());
        graph.clearExternalResources();

        d3d12.swapFramebuffers();
        d3d12.endFrame();
        fpsCounter.frame_end();
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
