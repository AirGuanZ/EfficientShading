#include <iostream>

#include <agz-utils/time.h>

#include "../common/camera.h"
#include "../common/sky.h"
#include "./depth.h"
#include "./forward.h"

void run()
{
    enableDebugLayerInDebugMode(false);

    // d3d12 context

    WindowDesc windowDesc;
    windowDesc.title      = L"predepth";
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

    Mesh mesh;
    mesh.load(
        d3d12, uploader,
        "./asset/mesh/eglise/mesh.obj",
        "./asset/mesh/eglise/albedo.png",
        "./asset/mesh/eglise/metallic.png",
        "./asset/mesh/eglise/roughness.png");

    // camera

    common::Camera camera;
    camera.setSpeed(0.03f);
    camera.setPosition({ 0, -4, 0 });

    input->showCursor(false);
    input->setCursorLock(
        true, d3d12.getClientWidth() / 2, d3d12.getClientHeight() / 2);

    // renderers

    PreDepthRenderer depthRenderer(d3d12);
    ForwardRenderer forwardRenderer(d3d12);
    
    std::vector<ForwardRenderer::Light> lights = {
        ForwardRenderer::Light{
            .lightPosition    = { 2, 0, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(0, 1, 2),
            .lightAmbient     = Float3(0)
        },
        ForwardRenderer::Light{
            .lightPosition    = { 0, -3, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(2, 0, 1),
            .lightAmbient     = Float3(0)
        },
        ForwardRenderer::Light{
            .lightPosition    = { -10, -1.5, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(1, 2, 0),
            .lightAmbient     = Float3(0)
        },
        ForwardRenderer::Light{
            .lightPosition    = { -10, -9, 0 },
            .maxLightDistance = 3,
            .lightIntensity   = Float3(7, 0, 0),
            .lightAmbient     = Float3(0)
        }
    };

    forwardRenderer.setLights(
        lights.data(), lights.size(), d3d12.getResourceManager(), uploader);

    depthRenderer.addMesh(&mesh);
    forwardRenderer.addMesh(&mesh);

    // render graph

    rg::Graph graph;
    rg::ExternalResource *framebuffer = nullptr;
    rg::InternalResource *depthBuffer = nullptr;

    auto initGraph = [&]
    {
        graph = rg::Graph();
        graph.setFrameCount(d3d12.getFramebufferCount());
        graph.setQueueCount(1);
        graph.setThreadCount(1);

        // create resource nodes

        framebuffer = graph.addExternalResource("framebuffer");
        framebuffer->setDescription(d3d12.getFramebuffer()->GetDesc());
        framebuffer->setInitialState(D3D12_RESOURCE_STATE_PRESENT);
        framebuffer->setFinalState(D3D12_RESOURCE_STATE_PRESENT);

        depthBuffer = graph.addInternalResource("depth buffer");
        depthBuffer->setDescription(CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT,
            framebuffer->getDescription().Width,
            framebuffer->getDescription().Height,
            1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
        depthBuffer->setClearValue(D3D12_CLEAR_VALUE{
            .Format       = DXGI_FORMAT_D32_FLOAT,
            .DepthStencil = { 1, 0 }
            });

        // passes

        auto skyPass = skyRenderer.addToRenderGraph(graph, framebuffer);

        auto predepthPass = depthRenderer.addToRenderGraph(graph, depthBuffer);

        auto forwardPass = forwardRenderer.addToRenderGraph(
            graph, framebuffer, depthBuffer);

        auto imguiPass = d3d12.addImGuiToRenderGraph(graph, framebuffer);

        // compile

        graph.addDependency(skyPass, predepthPass);
        graph.addDependency(predepthPass, forwardPass);
        graph.addDependency(forwardPass, imguiPass);

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

    agz::time::fps_counter_t fpsCounter;

    while(!d3d12.getCloseFlag())
    {
        d3d12.startFrame();

        if(input->isPressed(KEY_ESCAPE))
            d3d12.setCloseFlag(true);

        if(d3d12.getInput()->isDown(KEY_LCTRL))
        {
            input->showCursor(!input->isCursorVisible());
            input->setCursorLock(!input->isCursorLocked());
        }

        if(ImGui::Begin("PreDepth", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("fps: %d", fpsCounter.fps());
            ImGui::Text(
                "camera position: %s", camera.getPosition().to_string().c_str());
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
        forwardRenderer.setCamera(camera.getPosition());

        const Mat4 world = Mat4::right_transform::scale(Float3(0.3f));
        mesh.vsTransform.updateData(
            d3d12.getFramebufferIndex(),
            { world, world * camera.getViewProj() });

        graph.setExternalResource(framebuffer, d3d12.getFramebuffer());
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
