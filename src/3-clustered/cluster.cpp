#include <agz-utils/file.h>

#include "./cluster.h"

namespace
{

    Float3 getFrustumDirection(
        const Float3 &A,
        const Float3 &B,
        const Float3 &C,
        const Float3 &D,
        const Float2 &scrCoord)
    {
        const Float3 AB = lerp(A, B, scrCoord.x);//.normalize();
        const Float3 CD = lerp(C, D, scrCoord.x);//.normalize();
        return lerp(CD, AB, scrCoord.y).normalize();
    }

    float clusterI2Z(int i, int N, float nearZ, float farZ)
    {
        return nearZ * std::pow(
            farZ / nearZ, static_cast<float>(i) / static_cast<float>(N));
    }

    std::pair<Float3, Float3> getAABB(std::initializer_list<Float3> points)
    {
        Float3 lower((std::numeric_limits<float>::max)());
        Float3 upper(std::numeric_limits<float>::lowest());
        for(auto &p : points)
        {
            for(int i = 0; i < 3; ++i)
            {
                lower[i] = (std::min)(lower[i], p[i]);
                upper[i] = (std::max)(upper[i], p[i]);
            }
        }
        return { lower, upper };
    }

} // namespace anonymous

LightCluster::LightCluster(D3D12Context &d3d)
    : d3d_(d3d),
      clusterRange_(nullptr), lightIndex_(nullptr), uavTable_(nullptr),
      nearZ_(0), farZ_(0),
      lightBuffer_(nullptr), lightCount_(0)
{
    initRootSignature();
    initPipeline();
    initConstantBuffer();
}

void LightCluster::setClusterCount(const Int3 &count)
{
    clusterCount_ = count;
}

rg::Pass *LightCluster::addToRenderGraph(rg::Graph &graph, int thread, int queue)
{
    const int clusterCount              = clusterCount_.product();
    const int lightIndexCount           = clusterCount * MAX_LIGHTS_PER_CLUSTER;
    const size_t clusterRangeBufferSize = clusterCount * sizeof(ClusterRange);
    const size_t lightIndexBufferSize   = lightIndexCount * sizeof(int32_t);

    // create internal resources

    clusterRange_ = graph.addInternalResource("cluster range buffer");
    clusterRange_->setInitialState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    clusterRange_->setDescription(CD3DX12_RESOURCE_DESC::Buffer(
        clusterRangeBufferSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));

    lightIndex_ = graph.addInternalResource("light index buffer");
    lightIndex_->setInitialState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    lightIndex_->setDescription(CD3DX12_RESOURCE_DESC::Buffer(
        lightIndexBufferSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));

    // create pass

    auto pass = graph.addPass("cluster lights", thread, queue);

    // declare uav descriptor table

    uavTable_ = pass->addDescriptorTable(rg::Pass::GPUOnly);

    uavTable_->addUAV(clusterRange_, D3D12_UNORDERED_ACCESS_VIEW_DESC{
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer        = D3D12_BUFFER_UAV{
            .FirstElement         = 0,
            .NumElements          = static_cast<UINT>(clusterCount),
            .StructureByteStride  = sizeof(ClusterRange),
            .CounterOffsetInBytes = 0,
            .Flags                = D3D12_BUFFER_UAV_FLAG_NONE
        }
    });

    uavTable_->addUAV(lightIndex_, D3D12_UNORDERED_ACCESS_VIEW_DESC{
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer        = D3D12_BUFFER_UAV{
            .FirstElement         = 0,
            .NumElements          = static_cast<UINT>(lightIndexCount),
            .StructureByteStride  = sizeof(int32_t),
            .CounterOffsetInBytes = 0,
            .Flags                = D3D12_BUFFER_UAV_FLAG_NONE
        }
    });

    // set pass callback

    pass->setCallback(this, &LightCluster::doClusterPass);

    return pass;
}

rg::Resource *LightCluster::getClusterRangeBuffer() const
{
    return clusterRange_;
}

rg::Resource *LightCluster::getLightIndexBuffer() const
{
    return lightIndex_;
}

D3D12_SHADER_RESOURCE_VIEW_DESC LightCluster::getClusterRangeSRV() const
{
    return D3D12_SHADER_RESOURCE_VIEW_DESC{
        .Format                  = DXGI_FORMAT_UNKNOWN,
        .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer                  = D3D12_BUFFER_SRV{
            .FirstElement        = 0,
            .NumElements         = static_cast<UINT>(clusterCount_.product()),
            .StructureByteStride = sizeof(ClusterRange),
            .Flags               = D3D12_BUFFER_SRV_FLAG_NONE
        }
    };
}

D3D12_SHADER_RESOURCE_VIEW_DESC LightCluster::getLightIndexSRV() const
{
    const int indexCount = MAX_LIGHTS_PER_CLUSTER * clusterCount_.product();
    return D3D12_SHADER_RESOURCE_VIEW_DESC{
        .Format                  = DXGI_FORMAT_UNKNOWN,
        .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer                  = D3D12_BUFFER_SRV{
            .FirstElement        = 0,
            .NumElements         = static_cast<UINT>(indexCount),
            .StructureByteStride = sizeof(int32_t),
            .Flags               = D3D12_BUFFER_SRV_FLAG_NONE
        }
    };
}

void LightCluster::setProj(float nearZ, float farZ, const Mat4 &proj)
{
    nearZ_ = nearZ;
    farZ_ = farZ;
    proj_ = proj;
}

void LightCluster::setView(const Float3 &eye, const Mat4 &view)
{
    eye_  = eye;
    view_ = view;
}

void LightCluster::updateClusterAABBs(ResourceUploader &uploader)
{
    initClusterAABBBuffer(uploader);
}

void LightCluster::setLights(const Buffer &lightBuffer, size_t lightCount)
{
    lightBuffer_ = &lightBuffer;
    lightCount_  = lightCount;
}

void LightCluster::initRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0);

    CD3DX12_ROOT_PARAMETER params[4];
    params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    params[1].InitAsShaderResourceView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    params[2].InitAsShaderResourceView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
    params[3].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);

    RootSignatureBuilder builder(d3d_.getDevice());
    for(auto &p : params)
        builder.addParameter(p);

    rootSignature_ = builder.build();
}

void LightCluster::initPipeline()
{
    FXC compiler;
    compiler.setWarnings(true);

    const char       *shaderFilename = "./asset/clustered/cluster.hlsl";
    const std::string shaderSource   = agz::file::read_txt_file(shaderFilename);

    auto cs = compiler.compile(
        shaderSource, "cs_5_1", FXC::Options{
            .includes   = D3D_COMPILE_STANDARD_FILE_INCLUDE,
            .sourceName = shaderFilename,
            .entry      = "CSMain"
        });

    D3D12_SHADER_BYTECODE csByteCode = {
        .pShaderBytecode = cs->GetBufferPointer(),
        .BytecodeLength  = cs->GetBufferSize()
    };

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
    desc.pRootSignature = rootSignature_.Get();
    desc.CS             = csByteCode;
    desc.NodeMask       = 0;
    desc.CachedPSO      = {};
    desc.Flags          = D3D12_PIPELINE_STATE_FLAG_NONE;

    AGZ_D3D12_CHECK_HR(
        d3d_.getDevice()->CreateComputePipelineState(
            &desc, IID_PPV_ARGS(pipeline_.GetAddressOf())));
}

void LightCluster::initConstantBuffer()
{
    csParams_.initializeUpload(
        d3d_.getResourceManager(), d3d_.getFramebufferCount());
}

void LightCluster::initClusterAABBBuffer(ResourceUploader &uploader)
{
    std::vector<ClusterAABB> clusterAABBBufferData;
    clusterAABBBufferData.reserve(clusterCount_.product());

    const Mat4 invProj = proj_.inv();

    const Float3 frustumA =
        (Float4(-1, +1, 0.5f, 1) * invProj).homogenize().normalize();
    const Float3 frustumB =
        (Float4(+1, +1, 0.5f, 1) * invProj).homogenize().normalize();
    const Float3 frustumC =
        (Float4(-1, -1, 0.5f, 1) * invProj).homogenize().normalize();
    const Float3 frustumD =
        (Float4(+1, -1, 0.5f, 1) * invProj).homogenize().normalize();

    for(int xi = 0; xi < clusterCount_.x; ++xi)
    {
        const float lowerScrX = static_cast<float>(xi    ) / clusterCount_.x;
        const float upperScrX = static_cast<float>(xi + 1) / clusterCount_.x;

        for(int yi = 0; yi < clusterCount_.y; ++yi)
        {
            const float lowerScrY = static_cast<float>(yi    ) / clusterCount_.y;
            const float upperScrY = static_cast<float>(yi + 1) / clusterCount_.y;

            const Float3 A = getFrustumDirection(
                frustumA, frustumB, frustumC, frustumD, { lowerScrX, upperScrY });
            const Float3 B = getFrustumDirection(
                frustumA, frustumB, frustumC, frustumD, { upperScrX, upperScrY });
            const Float3 C = getFrustumDirection(
                frustumA, frustumB, frustumC, frustumD, { lowerScrX, lowerScrY });
            const Float3 D = getFrustumDirection(
                frustumA, frustumB, frustumC, frustumD, { upperScrX, lowerScrY });

            for(int zi = 0; zi < clusterCount_.z; ++zi)
            {
                const float lowerZ = clusterI2Z(
                    zi, clusterCount_.z, nearZ_, farZ_);
                const float upperZ = clusterI2Z(
                    zi + 1, clusterCount_.z, nearZ_, farZ_);

                static auto getClusterVertex = [](const Float3 &dir, float z)
                {
                    return dir * z / dir.z;
                };

                const Float3 Alz = getClusterVertex(A, lowerZ);
                const Float3 Blz = getClusterVertex(B, lowerZ);
                const Float3 Clz = getClusterVertex(C, lowerZ);
                const Float3 Dlz = getClusterVertex(D, lowerZ);

                const Float3 Auz = getClusterVertex(A, upperZ);
                const Float3 Buz = getClusterVertex(B, upperZ);
                const Float3 Cuz = getClusterVertex(C, upperZ);
                const Float3 Duz = getClusterVertex(D, upperZ);

                const auto [lower, upper] = getAABB(
                    { Alz, Blz, Clz, Dlz, Auz, Buz, Cuz, Duz });

                clusterAABBBufferData.push_back(ClusterAABB{
                    .lower = lower,
                    .upper = upper
                });
            }
        }
    }

    clusterAABBBuffer_ = d3d_.createDefaultBuffer(
        sizeof(ClusterAABB) * clusterAABBBufferData.size(),
        D3D12_RESOURCE_STATE_COMMON);

    uploader.upload(
        clusterAABBBuffer_,
        clusterAABBBufferData.data(),
        clusterAABBBuffer_.getByteSize());
    uploader.submitAndSync();
}

void LightCluster::updateCSParams()
{
    csParams_.updateData(d3d_.getFramebufferIndex(), CSParams{
        .view               = view_,
        .clusterXCount      = clusterCount_.x,
        .clusterYCount      = clusterCount_.y,
        .clusterZCount      = clusterCount_.z,
        .lightCount         = static_cast<int>(lightCount_),
        .maxLightPerCluster = MAX_LIGHTS_PER_CLUSTER
    });
}

void LightCluster::doClusterPass(rg::PassContext &ctx)
{
    ctx->SetComputeRootSignature(rootSignature_.Get());
    ctx->SetPipelineState(pipeline_.Get());

    updateCSParams();
    ctx->SetComputeRootConstantBufferView(
        0, csParams_.getGPUVirtualAddress(ctx.getFrameIndex()));

    ctx->SetComputeRootShaderResourceView(
        1, lightBuffer_->getGPUVirtualAddress());

    ctx->SetComputeRootShaderResourceView(
        2, clusterAABBBuffer_.getGPUVirtualAddress());

    auto uavTable = ctx.getDescriptorRange(uavTable_);
    ctx->SetComputeRootDescriptorTable(3, uavTable[0]);

    const UINT dispatchCountX = agz::upalign_to(clusterCount_.x, 8) / 8;
    const UINT dispatchCountY = agz::upalign_to(clusterCount_.y, 8) / 8;
    const UINT dispatchCountZ = agz::upalign_to(clusterCount_.z, 1) / 1;

    ctx->Dispatch(dispatchCountX, dispatchCountY, dispatchCountZ);
}
