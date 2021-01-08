#include <iostream>

#include <agz-utils/time.h>

#include "../common/camera.h"
#include "../common/sky.h"
#include "./cluster.h"
#include "./forward.h"

void run()
{
    enableDebugLayer(false);

    // d3d12 context

    WindowDesc windowDesc;
    windowDesc.title      = L"clustered";
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

    // camera

    common::Camera camera;
    camera.setWOverH(d3d12.getFramebufferWOverH());
    camera.setSpeed(0.03f);
    camera.setPosition({ 0, -4, 0 });
    camera.recalculateMatrics();

    // lights

    std::vector<Light> lightData = {
        Light{
            .lightPosition    = { 2, 0, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(0, 1, 2),
            .lightAmbient     = Float3(0.01f)
        },
        Light{
            .lightPosition    = { 0, -3, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(2, 0, 1),
            .lightAmbient     = Float3(0)
        },
        Light{
            .lightPosition    = { -10, -1.5, 0 },
            .maxLightDistance = 15,
            .lightIntensity   = Float3(1, 2, 0),
            .lightAmbient     = Float3(0)
        },
        Light{
            .lightPosition    = { -10, -9, 0 },
            .maxLightDistance = 3,
            .lightIntensity   = Float3(7, 0, 0),
            .lightAmbient     = Float3(0)
        }
    };

    Buffer lightBuffer = d3d12.createDefaultBuffer(
        sizeof(Light) * lightData.size(), D3D12_RESOURCE_STATE_COMMON);

    uploader.upload(lightBuffer, lightData.data(), lightBuffer.getByteSize());
    uploader.submitAndSync();

    // cluster

    const Int3 CLUSTER_COUNT = { 24, 18, 24 };

    LightCluster lightCluster(d3d12);
    lightCluster.setClusterCount(CLUSTER_COUNT);
    lightCluster.setProj(camera.getNearZ(), camera.getFarZ(), camera.getProj());
    lightCluster.updateClusterAABBs(uploader);
    lightCluster.setLights(lightBuffer, lightData.size());

    // forward renderer

    ForwardRenderer forwardRenderer(d3d12);
    forwardRenderer.setLights(&lightBuffer, lightData.size());
    forwardRenderer.setCluster(
        camera.getNearZ(), camera.getFarZ(), CLUSTER_COUNT);

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

    forwardRenderer.addMesh(&mesh);

    // render graph

    rg::Graph graph;
    rg::ExternalResource *framebuffer = nullptr;
    rg::InternalResource *depthBuffer = nullptr;

    auto initGraph = [&]
    {
        graph = rg::Graph();
        graph.setFrameCount(d3d12.getFramebufferCount());
        graph.setQueueCount(2);
        graph.setThreadCount(2);

        // create resources

        framebuffer = graph.addExternalResource("framebuffer");
        framebuffer->setDescription(d3d12.getFramebuffer()->GetDesc());
        framebuffer->setInitialState(D3D12_RESOURCE_STATE_PRESENT);
        framebuffer->setFinalState(D3D12_RESOURCE_STATE_PRESENT);

        depthBuffer = graph.addInternalResource("depth buffer");
        depthBuffer->setInitialState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depthBuffer->setClearValue(D3D12_CLEAR_VALUE{
            .Format       = DXGI_FORMAT_D32_FLOAT,
            .DepthStencil = { 1, 0 }
            });
        depthBuffer->setHeapType(D3D12_HEAP_TYPE_DEFAULT);
        depthBuffer->setDescription(CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT,
            framebuffer->getDescription().Width,
            framebuffer->getDescription().Height,
            1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));

        // passes

        auto skyPass = skyRenderer.addToRenderGraph(graph, framebuffer);

        auto lightClusterPass = lightCluster.addToRenderGraph(graph, 1, 1);

        auto forwardPass = forwardRenderer.addToRenderGraph(
            ForwardRenderer::RenderGraphInput{
                .graph              = &graph,
                .renderTarget       = framebuffer,
                .depthBuffer        = depthBuffer,
                .clusterRangeBuffer = lightCluster.getClusterRangeBuffer(),
                .clusterRangeSRV    = lightCluster.getClusterRangeSRV(),
                .lightIndexBuffer   = lightCluster.getLightIndexBuffer(),
                .lightIndexSRV      = lightCluster.getLightIndexSRV()
            });

        auto imguiPass = d3d12.addImGuiToRenderGraph(graph, framebuffer);

        // compile

        graph.addDependency(skyPass, forwardPass);
        graph.addDependency(lightClusterPass, forwardPass);
        graph.addDependency(forwardPass, imguiPass);

        graph.compile(
            d3d12.getDevice(),
            d3d12.getResourceManager(),
            d3d12.getDescriptorAllocator(),
            { d3d12.getGraphicsQueue(), d3d12.getComputeQueue() });
    };

    initGraph();

    d3d12.attach(std::make_shared<SwapChainPostResizeHandler>(
        [&]
    {
        d3d12.waitForIdle();

        camera.setWOverH(d3d12.getFramebufferWOverH());
        lightCluster.setProj(camera.getNearZ(), camera.getFarZ(), camera.getProj());
        lightCluster.updateClusterAABBs(uploader);

        input->setCursorLock(
            input->isCursorLocked(),
            d3d12.getClientWidth() / 2,
            d3d12.getClientHeight() / 2);
        initGraph();
    }));

    agz::time::fps_counter_t fpsCounter;

    input->showCursor(false);
    input->setCursorLock(
        true, d3d12.getClientWidth() / 2, d3d12.getClientHeight() / 2);

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

        if(ImGui::Begin("Clustered", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("fps: %d", fpsCounter.fps());

            const auto &eyePos = camera.getPosition();
            ImGui::Text(
                "camera position: %f, %f, %f",
                eyePos.x, eyePos.y, eyePos.z);
        }
        ImGui::End();

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
        lightCluster.setView(camera.getPosition(), camera.getView());
        forwardRenderer.setCamera(camera.getPosition());

        const Mat4 world = Mat4::right_transform::scale(Float3(0.3f));
        mesh.vsTransform.updateData(
            d3d12.getFramebufferIndex(),
            { world, world * camera.getView(), world * camera.getViewProj() });

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
