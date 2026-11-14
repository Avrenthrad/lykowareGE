// Transparent surfaces (ADR 0086): lit.slang's light, with an opacity. The block is lit.slang's
// 128 bytes plus one float4 whose x is the surface's opacity, so it is 144 bytes; the pipeline is
// blended (rhi::BlendMode::Alpha) and tests depth without writing it. The base colour texture's
// alpha multiplies the opacity.

struct Constants
{
    // projection * view * model, by column (math::Mat4 layout).
    float4 clip0;
    float4 clip1;
    float4 clip2;
    float4 clip3;
    // The model's normal matrix, by column; w is the light's direction (x, y, z).
    float4 normal0;
    float4 normal1;
    float4 normal2;
    // Base colour (rgb), multiplied by the texture; a is the ambient share.
    float4 colour;
    // x: opacity, 0 (invisible) .. 1. y, z, w unused.
    float4 surface;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

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

// lit.slang's fragmentMain, with alpha = opacity * texture alpha. Straight alpha: the blend
// multiplies the colour by it.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 light = normalize(float3(constants.normal0.w, constants.normal1.w, constants.normal2.w));
    const float ambient = constants.colour.a;
    const float diffuse = max(dot(normalize(input.normal), light), 0.0);
    const float4 texel = baseColour.Sample(input.uv);
    const float3 albedo = constants.colour.rgb * texel.rgb;
    return float4(albedo * (ambient + (1.0 - ambient) * diffuse), constants.surface.x * texel.a);
}
