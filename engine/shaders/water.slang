// Water surfaces (ADR 0097): transparent.slang's lit, blended surface, plus a mirror that grows
// at glancing angles. With N the surface normal and V the direction to the camera,
// F = 0.02 + 0.98 * (1 - max(N . V, 0))^5 (Schlick, water's 2% head-on). The colour is the lit
// body colour mixed towards the reflection colour by F, and the alpha is the opacity mixed
// towards 1 by F. The block is 192 bytes; the pipeline is blended and tests depth without
// writing it, as a transparent surface's.

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
    // Body colour (rgb), multiplied by the texture; a is the ambient share.
    float4 colour;
    // x: opacity seen head-on, 0 .. 1. yzw: the reflection colour (linear rgb).
    float4 surface;
    // The camera-relative model matrix: rotation * scale by column, w the translation.
    float4 model0;
    float4 model1;
    float4 model2;
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
    // Camera-relative world position: the camera is at the origin.
    float3 world : TEXCOORD2;
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
    output.world = constants.model0.xyz * input.position.x + constants.model1.xyz * input.position.y
        + constants.model2.xyz * input.position.z + float3(constants.model0.w, constants.model1.w, constants.model2.w);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 light = normalize(float3(constants.normal0.w, constants.normal1.w, constants.normal2.w));
    const float ambient = constants.colour.a;
    const float3 normal = normalize(input.normal);
    const float diffuse = max(dot(normal, light), 0.0);
    const float4 texel = baseColour.Sample(input.uv);
    const float3 body = constants.colour.rgb * texel.rgb * (ambient + (1.0 - ambient) * diffuse);
    const float facing = max(dot(normal, normalize(-input.world)), 0.0);
    const float away = 1.0 - facing;
    const float mirror = 0.02 + 0.98 * away * away * away * away * away;
    return float4(lerp(body, constants.surface.yzw, mirror), lerp(constants.surface.x * texel.a, 1.0, mirror));
}
