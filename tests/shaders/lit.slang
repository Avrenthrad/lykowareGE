// Test shader for lit meshes (ADR 0025): a position and a normal per vertex
// (VertexLayout::PositionNormal) and one 128-byte push-constant block per mesh.
// Matrices arrive as float4 columns and are applied by hand, so there is no
// row-major or column-major ambiguity between C++ and Slang.

struct Constants
{
    // projection * view * model, by column (math::Mat4 layout).
    float4 clip0;
    float4 clip1;
    float4 clip2;
    float4 clip3;
    // The model rotation, by column, for normals (w unused).
    float4 normal0;
    float4 normal1;
    float4 normal2;
    // Base colour (rgb; a unused).
    float4 colour;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = constants.clip0 * input.position.x + constants.clip1 * input.position.y
        + constants.clip2 * input.position.z + constants.clip3;
    output.normal = constants.normal0.xyz * input.normal.x + constants.normal1.xyz * input.normal.y
        + constants.normal2.xyz * input.normal.z;
    return output;
}

// One directional light from above and slightly in front, plus ambient:
// colour * (0.25 + 0.75 * max(0, n . light)).
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 light = normalize(float3(0.4, 1.0, 0.3));
    const float diffuse = max(dot(normalize(input.normal), light), 0.0);
    return float4(constants.colour.rgb * (0.25 + 0.75 * diffuse), 1.0);
}
