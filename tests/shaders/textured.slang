// rhi.texture's shader (ADR 0037): unlit, so pixels are exactly what the texture gives.
// Vertices use VertexLayout::PositionNormalUv; the position is already in clip space (the
// normal is ignored), and the fragment is the texture sampled at the texture coordinate.

[[vk::binding(0, 0)]] Sampler2D image;

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

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return image.Sample(input.uv);
}
