#include <iostream>
#include <random>

#include <agz-utils/graphics_api.h>
#include <agz-utils/misc.h>
#include <agz-utils/time.h>

#include "../common/camera.h"
#include "../common/sky.h"
#include "./hierarchy.h"
#include "./renderer.h"

Mesh load_mesh(
    D3D12Context      &d3d12,
    ResourceUploader  &uploader,
    const std::string &filename)
{
    Mesh mesh;
    mesh.load(d3d12, uploader, filename);
    return mesh;
}

void run()
{
    enableDebugLayerInDebugMode(true);

    // d3d12 context

    WindowDesc windowDesc;
    windowDesc.title      = L"hierarchy-z";
    windowDesc.clientSize = { 800, 600 };

    SwapChainDesc swapChainDesc;
    swapChainDesc.imageCount = 3;

    D3D12Context d3d12(windowDesc, swapChainDesc, 1024, true, true);

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

    // meshes

    std::vector<Mesh> cubes;
    std::vector<Mat4> cubeWorlds;

    std::default_random_engine rng{ std::random_device()() };

    for(int i = 0; i < 19999; ++i)
    {
        cubes.push_back(
            load_mesh(d3d12, uploader, "./asset/hierarchyz/cube.obj"));

        const float x = std::uniform_real_distribution<float>(-64, 64)(rng);
        const float z = std::uniform_real_distribution<float>(-64, 64)(rng);

        cubeWorlds.push_back(
            Trans4::scale(Float3(0.1f)) *
            Trans4::translate(x, 0.5f, z));
    }

    auto occluder = load_mesh(
        d3d12, uploader, "./asset/hierarchyz/occluder.obj");

    // hierarchy-z

    HierarchyZGenerator hiZ(d3d12);
    hiZ.addMesh(&occluder);

    // renderer

    Renderer renderer(d3d12);

    bool renderCulledMeshes = false;
    renderer.setCulledMeshRenderingEnabled(renderCulledMeshes);

    renderer.addMesh(&occluder);
    for(auto &cube : cubes)
        renderer.addMesh(&cube);

    // camera

    common::Camera camera;
    camera.setSpeed(0.03f);
    camera.setPosition({ 2, 1.2f, 2 });

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
        graph.setQueueCount(2);
        graph.setThreadCount(2);

        // create resource nodes

        framebufferRsc = graph.addExternalResource("framebuffer");
        framebufferRsc->setDescription(d3d12.getFramebuffer()->GetDesc());
        framebufferRsc->setInitialState(D3D12_RESOURCE_STATE_PRESENT);
        framebufferRsc->setFinalState(D3D12_RESOURCE_STATE_PRESENT);
        framebufferRsc->setPerFrame();

        // passes

        auto skyPass = skyRenderer.addToRenderGraph(graph, framebufferRsc);

        auto hiZPass = hiZ.addToRenderGraph(
            graph, framebufferRsc, 0, 0, 1, 1);

        auto renderPass = renderer.addToRenderGraph(
            graph, framebufferRsc, hiZ.getHierarchyZBuffer(), 1, 1, 0, 0);

        auto imguiPass = d3d12.addImGuiToRenderGraph(graph, framebufferRsc);

        graph.addDependency(skyPass, renderPass);
        graph.addDependency(hiZPass, renderPass);
        graph.addDependency(renderPass, imguiPass);

        graph.addCrossFrameDependency(renderPass, hiZPass);

        graph.compile(
            d3d12.getDevice(),
            d3d12.getResourceManager(),
            d3d12.getDescriptorAllocator(),
            { d3d12.getGraphicsQueue(), d3d12.getComputeQueue() });

        for(int i = 0; i < d3d12.getFramebufferCount(); ++i)
        {
            graph.setExternalResource(
                framebufferRsc, i, d3d12.getFramebuffer(i).Get());
        }
    };

    initGraph();

    d3d12.attach(std::make_shared<SwapChainPostResizeHandler>([&]
    {
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

        if(input->isDown(KEY_ESCAPE))
            d3d12.setCloseFlag(true);

        if(input->isDown(KEY_C))
        {
            renderCulledMeshes = !renderCulledMeshes;
            renderer.setCulledMeshRenderingEnabled(renderCulledMeshes);
        }

        if(ImGui::Begin("hierarchy-z", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("press C to show/hide culled meshes");

            ImGui::Separator();

            ImGui::Text("fps: %d", fpsCounter.fps());
            ImGui::Text(
                "camera position: %s", camera.getPosition().to_string().c_str());
            ImGui::Text(
                "draw culled meshes: %s", renderCulledMeshes ? "true" : "false");
        }
        ImGui::End();

        camera.setWOverH(d3d12.getFramebufferWOverH());
        camera.update({
            input->isPressed('W'),
            input->isPressed('A'),
            input->isPressed('D'),
            input->isPressed('S'),
            false, false,
            static_cast<float>(input->getCursorRelativeX()),
            static_cast<float>(input->getCursorRelativeY())
            });

        skyRenderer.setCamera(camera.getPosition(), camera.getViewProj());

        hiZ.setCamera(camera.getViewProj());
        renderer.setCamera(camera.getView(), camera.getProj());

        const Mat4 occluderWorld = Mat4::identity();
        occluder.updateVSTransform(
            d3d12.getFramebufferIndex(), { occluderWorld });

        for(size_t i = 0; i < cubes.size(); ++i)
        {
            const Mat4 &cubeWorld = cubeWorlds[i];
            cubes[i].updateVSTransform(
                d3d12.getFramebufferIndex(), { cubeWorld });
        }

        graph.run(d3d12.getFramebufferIndex());
        
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
