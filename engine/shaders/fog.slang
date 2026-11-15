// Distance fog with the tone map (ADR 0099): the post pass of ADR 0087, which also reads the
// scene's depth. A pixel with a surface d metres from the camera becomes
// mix(scene, fog, min(maxOpacity, 1 - exp(-density * max(d - start, 0)))) before tone mapping.
// A pixel with nothing drawn (depth 0: the sky or the clear colour) is left as it is.

struct Constants
{
    // x: exposure (above 0). y: start in metres. z: density per metre. w: the most fog, 0..1.
    float4 settings;
    // rgb: the fog's linear colour.
    float4 colour;
    // x, y: the tangents of half the horizontal and vertical fields of view. z: the near plane
    // in metres. Depth is reversed and infinite (math::perspective): depth = near / view depth.
    float4 lens;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

// The scene's HDR colour (rhi::HdrTarget), linear.
[[vk::binding(0, 0)]] Sampler2D scene;
// The scene's depth (rhi::HdrTarget), unfiltered.
[[vk::binding(0, 1)]] Sampler2D<float> sceneDepth;

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
    float2 ndc : TEXCOORD1;
};

// Positions arrive in clip space; the normal is unused.
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position.xy, 0.0, 1.0);
    output.uv = input.uv;
    output.ndc = input.position.xy;
    return output;
}

// Fog, then Reinhard per channel as post.slang: e c / (1 + e c).
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float3 colour = max(scene.Sample(input.uv).rgb, float3(0.0));
    const float depth = sceneDepth.SampleLevel(input.uv, 0.0);
    if (depth > 0.0)
    {
        // The distance from the eye to the surface, not only along the view axis, so the fog
        // does not change when the camera turns.
        const float along = constants.lens.z / depth;
        const float distance =
            along * length(float3(input.ndc.x * constants.lens.x, input.ndc.y * constants.lens.y, 1.0));
        const float fog = min(constants.settings.w,
                              1.0 - exp(-constants.settings.z * max(distance - constants.settings.y, 0.0)));
        colour = lerp(colour, constants.colour.rgb, fog);
    }
    const float3 exposed = colour * constants.settings.x;
    return float4(exposed / (1.0 + exposed), 1.0);
}
