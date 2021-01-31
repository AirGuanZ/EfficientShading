#include <iostream>

#include <agz-utils/graphics_api.h>
#include <agz-utils/misc.h>
#include <agz-utils/time.h>

#include "../common/camera.h"
#include "../common/sky.h"
#include "./hierarchy.h"

void run()
{
    enableDebugLayerInDebugMode(true);

    // d3d12 context

    WindowDesc windowDesc;
    windowDesc.title      = L"hierarchy-z";
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

    // hierarchy-z

    HierarchyZGenerator hiZ(d3d12);

    Mesh mesh;
    mesh.load(d3d12, uploader, "./asset/mesh/eglise/mesh.obj");
    mesh.vsTransform.initializeUpload(
        d3d12.getResourceManager(), d3d12.getFramebufferCount());
    hiZ.addMesh(&mesh);

    // camera

    common::Camera camera;
    camera.setSpeed(0.03f);
    camera.setPosition({ 0, -4, 0 });

    input->showCursor(false);
    input->setCursorLock(
        true, d3d12.getClientWidth() / 2, d3d12.getClientHeight() / 2);

    // render graph

    rg::Graph graph;
    rg::ExternalResource *framebufferRsc = nullptr;

    auto initGraph = [&]
    {
        graph = rg::Graph();
        graph.setFrameCount(d3d12.getFramebufferCount());
        graph.setQueueCount(1);
        graph.setThreadCount(1);

        // create resource nodes

        framebufferRsc = graph.addExternalResource("framebuffer");
        framebufferRsc->setDescription(d3d12.getFramebuffer()->GetDesc());
        framebufferRsc->setInitialState(D3D12_RESOURCE_STATE_PRESENT);
        framebufferRsc->setFinalState(D3D12_RESOURCE_STATE_PRESENT);

        // passes

        auto skyPass = skyRenderer.addToRenderGraph(graph, framebufferRsc);

        auto hiZPass = hiZ.addToRenderGraph(&graph, framebufferRsc);

        auto imguiPass = d3d12.addImGuiToRenderGraph(graph, framebufferRsc);

        graph.addDependency(skyPass, hiZPass);
        graph.addDependency(hiZPass, imguiPass);
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

        if(ImGui::Begin("hierarchy-z", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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

        const Mat4 world = Mat4::right_transform::scale(Float3(0.3f));
        mesh.vsTransformData = { world, world * camera.getViewProj() };
        mesh.vsTransform.updateData(
            d3d12.getFramebufferIndex(), mesh.vsTransformData);

        graph.setExternalResource(framebufferRsc, d3d12.getFramebuffer());
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
