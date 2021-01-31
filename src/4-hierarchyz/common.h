#pragma once

#include "../common/light.h"
#include "../common/mesh.h"

using Light = common::PBSLight;

class Mesh : public common::SimpleMesh
{
public:

    struct VSTransform
    {
        Mat4 World;
        Mat4 WVP;
    };

    using SimpleMesh::SimpleMesh;

    using SimpleMesh::load;

    VSTransform                 vsTransformData;
    ConstantBuffer<VSTransform> vsTransform;
};
