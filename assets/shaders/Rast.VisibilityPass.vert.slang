import Common;

[[vk::binding(0, 0)]]
ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(1, 0)]]
StructuredBuffer<NodeProxy> NodeProxies;

[[vk::binding(2, 0)]]
StructuredBuffer<uint> Reorders;

[[vk::binding(3, 0)]]
StructuredBuffer<half4> Vertices;

[[vk::push_constant]]
ConstantBuffer<GPUScene> gpuScene;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint primitive_index : PRIMITIVE_INDEX;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID, uint baseVertex: SV_StartVertexLocation, uint instanceIndex: SV_InstanceID, uint instanceBase: SV_StartInstanceLocation)
{
    VertexOutput output;
    uint absouluteInstanceIdx = instanceIndex + instanceBase;
    NodeProxy* gpuScenePtr = (NodeProxy*)gpuScene.NodesAddress;
    NodeProxy proxy = gpuScenePtr[absouluteInstanceIdx];
    half4 Position = Vertices[Reorders[vertexIndex]];

    UniformBufferObject* CameraPtr = (UniformBufferObject*)gpuScene.CameraAddress;
    output.position = mul(mul(Camera.ViewProjection, proxy.worldTS), float4(Position.xyz, 1.0));

    uint instanceIdx = absouluteInstanceIdx + 1;
    uint triangleIdx = vertexIndex - baseVertex;
    // we use a 32bit visibility buffer, with 1 bit shift to left, support up to 32768 instances and 131072 triangles per instance
    // if exceed, render result will be undefined
    output.primitive_index = ((instanceIdx & 0x7FFF) << 17) | (triangleIdx & 0x1FFFF);
    return output;
}