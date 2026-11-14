// The shadow caster (ADR 0084): depth only, drawn from the light into an rhi::ShadowMap.
// One 64-byte push-constant block per mesh: the light's clip * model, by column. The vertex
// layout is the lit shader's (PositionNormalUv); only the position is read. No fragment stage.

struct CasterConstants
{
    float4 clip0;
    float4 clip1;
    float4 clip2;
    float4 clip3;
};

[[vk::push_constant]] ConstantBuffer<CasterConstants> caster;

struct CasterInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

[shader("vertex")]
float4 vertexMain(CasterInput input) : SV_Position
{
    return caster.clip0 * input.position.x + caster.clip1 * input.position.y + caster.clip2 * input.position.z
        + caster.clip3;
}
