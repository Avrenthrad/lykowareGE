// The engine's lit mesh shader (ADR 0025, ADR 0026, ADR 0037): a position, a normal and a
// texture coordinate per vertex (VertexLayout::PositionNormalUv), one 128-byte push-constant
// block per mesh, and the base colour texture at set 0, binding 0.
// Matrices arrive as float4 columns and are applied by hand, so there is no
// row-major or column-major ambiguity between C++ and Slang.

struct Constants
{
    // projection * view * model, by column (math::Mat4 layout).
    float4 clip0;
    float4 clip1;
    float4 clip2;
    float4 clip3;
    // The model rotation, by column, for normals; w is the light's direction (x, y, z).
    float4 normal0;
    float4 normal1;
    float4 normal2;
    // Base colour (rgb), multiplied by the texture; a is the ambient share.
    float4 colour;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

// The base colour texture: sRGB textures arrive here as linear values.
[[vk::binding(0, 0)]] Sampler2D baseColour;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = constants.clip0 * input.position.x + constants.clip1 * input.position.y
        + constants.clip2 * input.position.z + constants.clip3;
    output.normal = constants.normal0.xyz * input.normal.x + constants.normal1.xyz * input.normal.y
        + constants.normal2.xyz * input.normal.z;
    output.uv = input.uv;
    return output;
}

// One directional light plus ambient (ADR 0082): colour * texture * (ambient + (1 - ambient) *
// max(0, n . light)). The light's direction rides in the normal columns' w, the ambient in colour.a.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 light = normalize(float3(constants.normal0.w, constants.normal1.w, constants.normal2.w));
    const float ambient = constants.colour.a;
    const float diffuse = max(dot(normalize(input.normal), light), 0.0);
    const float3 albedo = constants.colour.rgb * baseColour.Sample(input.uv).rgb;
    return float4(albedo * (ambient + (1.0 - ambient) * diffuse), 1.0);
}
