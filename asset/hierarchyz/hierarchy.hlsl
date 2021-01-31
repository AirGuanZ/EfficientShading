#define THREAD_GROUP_SIZE_X 16
#define THREAD_GROUP_SIZE_Y 16

cbuffer CSParams : register(b0)
{
    int LastWidth;
    int LastHeight;
    int ThisWidth;
    int ThisHeight;
};

Texture2D<float> LastLevel : register(t0);

RWTexture2D<float> ThisLevel : register(u0);

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CSMain(int3 threadIdx : SV_DispatchThreadID)
{
    if(threadIdx.x >= ThisWidth || threadIdx.y >= ThisHeight)
        return;

    float2 uvMin = float2(
        float(threadIdx.x) / ThisWidth,
        float(threadIdx.y) / ThisHeight);

    float2 uvMax = float2(
        float(threadIdx.x + 1) / ThisWidth,
        float(threadIdx.y + 1) / ThisHeight);

    int xBeg = int(floor(uvMin.x * LastWidth));
    int yBeg = int(floor(uvMin.y * LastHeight));

    int xEnd = int(ceil(uvMax.x * LastWidth));
    int yEnd = int(ceil(uvMax.y * LastHeight));

    float depth = 0;
    for(int x = xBeg; x < xEnd; ++x)
    {
        for(int y = yBeg; y < yEnd; ++y)
            depth = max(depth, LastLevel.Load(int3(x, y, 0)));
    }

    ThisLevel[threadIdx.xy] = depth;
}
