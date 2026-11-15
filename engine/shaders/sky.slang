// The sky (ADR 0098): one triangle past the screen's corners, drawn before the scene without
// writing depth. Each pixel's view direction d (unit, world space) gives
// mix(horizon, zenith, max(d.y, 0)); inside the sun's disc, where d . sun >= cos(radius), the
// sun's colour instead.

struct Constants
{
    // The view direction of a pixel is forward + ndc.x * across + ndc.y * down (xyz each).
    float4 across;
    float4 down;
    float4 forward;
    float4 horizon;
    float4 zenith;
    // xyz: the unit direction towards the sun. w: the cosine of its angular radius; above 1 for no sun.
    float4 sun;
    float4 sunColour;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

// Positions arrive in clip space. Depth is reversed (0 is infinitely far), so any depth above 0
// passes against the cleared buffer; nothing is written, and the scene draws over the sky.
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position.xy, 0.5, 1.0);
    output.ndc = input.position.xy;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 direction =
        normalize(constants.forward.xyz + input.ndc.x * constants.across.xyz + input.ndc.y * constants.down.xyz);
    if (dot(direction, constants.sun.xyz) >= constants.sun.w)
    {
        return float4(constants.sunColour.rgb, 1.0);
    }
    return float4(lerp(constants.horizon.rgb, constants.zenith.rgb, max(direction.y, 0.0)), 1.0);
}
