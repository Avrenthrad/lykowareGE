// Test shader: one hard-coded triangle with no vertex buffers. The vertex
// colours are pure red, green and blue so a test can check interpolation by
// reading back pixels near each corner.

struct VertexOutput
{
    float4 position : SV_Position;
    float3 colour : COLOR0;
};

static const float2 kPositions[3] = { float2(0.0, -0.6), float2(0.6, 0.6), float2(-0.6, 0.6) };
static const float3 kColours[3] = { float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0) };

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(kPositions[vertexIndex], 0.0, 1.0);
    output.colour = kColours[vertexIndex];
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.colour, 1.0);
}
