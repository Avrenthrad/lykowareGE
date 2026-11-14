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

// --- Receiving a shadow (ADR 0084) ------------------------------------------------------
// The same light, with the diffuse share scaled by how much of the surface the light's shadow
// map sees. Set 1 is an rhi::ShadowMap's set; these entry points are a separate pipeline, so
// vertexMain and fragmentMain above are unchanged.

struct ShadowBlock
{
    // Camera clip space to the light's clip space, by column.
    float4 toLight0;
    float4 toLight1;
    float4 toLight2;
    float4 toLight3;
    // x: depth bias in light depth units; y: one texel in map coordinates.
    float4 params;
};

[[vk::binding(0, 1)]] Sampler2D<float> shadowMap;
[[vk::binding(1, 1)]] ConstantBuffer<ShadowBlock> shadow;

struct ShadowedOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 light : TEXCOORD1;
};

[shader("vertex")]
ShadowedOutput vertexShadowed(VertexInput input)
{
    const VertexOutput lit = vertexMain(input);
    ShadowedOutput output;
    output.position = lit.position;
    output.normal = lit.normal;
    output.uv = lit.uv;
    // Linear in the clip position, so interpolation keeps it exact.
    output.light = shadow.toLight0 * lit.position.x + shadow.toLight1 * lit.position.y
        + shadow.toLight2 * lit.position.z + shadow.toLight3 * lit.position.w;
    return output;
}

// The share of the light reaching this point, 0..1: nine taps around it (3 x 3 texels). Outside
// the map, behind the light or past its far plane, the point is lit.
float lightReaching(float4 light)
{
    if (light.w <= 0.0)
    {
        return 1.0;
    }
    const float3 p = light.xyz / light.w;
    const float2 uv = p.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z <= 0.0 || p.z > 1.0)
    {
        return 1.0;
    }
    float reaching = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            // Reversed depth: an occluder nearer the light is larger.
            const float occluder = shadowMap.SampleLevel(uv + float2(float(x), float(y)) * shadow.params.y, 0.0);
            reaching += occluder > p.z + shadow.params.x ? 0.0 : 1.0;
        }
    }
    return reaching / 9.0;
}

[shader("fragment")]
float4 fragmentShadowed(ShadowedOutput input) : SV_Target
{
    const float3 light = normalize(float3(constants.normal0.w, constants.normal1.w, constants.normal2.w));
    const float ambient = constants.colour.a;
    const float diffuse = max(dot(normalize(input.normal), light), 0.0) * lightReaching(input.light);
    const float3 albedo = constants.colour.rgb * baseColour.Sample(input.uv).rgb;
    return float4(albedo * (ambient + (1.0 - ambient) * diffuse), 1.0);
}
