// The post pass (ADR 0087): one triangle past the screen's corners samples the scene's 16-bit
// float colour and tone-maps it. Push constants: x the exposure.

struct Constants
{
    // x: exposure (above 0). y, z, w unused.
    float4 settings;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

// The scene's HDR colour (rhi::HdrTarget), linear.
[[vk::binding(0, 0)]] Sampler2D scene;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Positions arrive in clip space; the normal is unused.
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position.xy, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}

// Reinhard per channel: e c / (1 + e c). Negative values (none are drawn) clamp to 0.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 exposed = max(scene.Sample(input.uv).rgb, float3(0.0)) * constants.settings.x;
    return float4(exposed / (1.0 + exposed), 1.0);
}
