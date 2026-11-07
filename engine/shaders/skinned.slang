// GPU linear-blend skinning (ADR 0052), with the same lit fragment interface.
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
    // The model rotation, by column, for normals (w unused).
    float4 normal0;
    float4 normal1;
    float4 normal2;
    // Base colour (rgb; a unused), multiplied by the texture.
    float4 colour;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

// The base colour texture: sRGB textures arrive here as linear values.
[[vk::binding(0, 0)]] Sampler2D baseColour;

struct JointMatrix
{
    float4 c0;
    float4 c1;
    float4 c2;
    float4 c3;
};
struct Palette { JointMatrix joints[256]; };
[[vk::binding(0, 1)]] ConstantBuffer<Palette> palette;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] uint4 joints : BLENDINDICES;
    [[vk::location(4)]] float4 weights : BLENDWEIGHT;
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
    // Static geometry in an animation-enabled renderer keeps its original vertex math.
    // Registered skinned meshes always have positive, normalised weights.
    if (all(input.weights == float4(0)))
    {
        VertexOutput output;
        output.position = constants.clip0 * input.position.x + constants.clip1 * input.position.y
            + constants.clip2 * input.position.z + constants.clip3;
        output.normal = constants.normal0.xyz * input.normal.x + constants.normal1.xyz * input.normal.y
            + constants.normal2.xyz * input.normal.z;
        output.uv = input.uv;
        return output;
    }
    // CPU mesh registration checks indices/weights; clamp fetches defensively for raw RHI callers.
    uint4 ids = min(input.joints, uint4(255));
    JointMatrix a = palette.joints[ids.x];
    JointMatrix b = palette.joints[ids.y];
    JointMatrix c = palette.joints[ids.z];
    JointMatrix d = palette.joints[ids.w];
    float4 w = input.weights;
    float sum = w.x + w.y + w.z + w.w;
    w = sum > 0.0 ? w / sum : float4(1, 0, 0, 0);
    float4 c0 = a.c0 * w.x + b.c0 * w.y + c.c0 * w.z + d.c0 * w.w;
    float4 c1 = a.c1 * w.x + b.c1 * w.y + c.c1 * w.z + d.c1 * w.w;
    float4 c2 = a.c2 * w.x + b.c2 * w.y + c.c2 * w.z + d.c2 * w.w;
    float4 c3 = a.c3 * w.x + b.c3 * w.y + c.c3 * w.z + d.c3 * w.w;
    float3 position = (c0 * input.position.x + c1 * input.position.y + c2 * input.position.z + c3).xyz;
    float3 co0 = cross(c1.xyz, c2.xyz);
    float3 co1 = cross(c2.xyz, c0.xyz);
    float3 co2 = cross(c0.xyz, c1.xyz);
    float determinant = dot(c0.xyz, co0);
    float3 normal = co0 * input.normal.x + co1 * input.normal.y + co2 * input.normal.z;
    normal *= determinant < 0.0 ? -1.0 : 1.0;
    float magnitude = max(abs(normal.x), max(abs(normal.y), abs(normal.z)));
    normal = determinant != 0.0 && isfinite(magnitude) && magnitude > 0.0
        ? normalize(normal / magnitude) : input.normal;
    VertexOutput output;
    output.position = constants.clip0 * position.x + constants.clip1 * position.y
        + constants.clip2 * position.z + constants.clip3;
    output.normal = constants.normal0.xyz * normal.x + constants.normal1.xyz * normal.y
        + constants.normal2.xyz * normal.z;
    output.uv = input.uv;
    return output;
}

// One directional light from above and slightly in front, plus ambient:
// colour * texture * (0.25 + 0.75 * max(0, n . light)).
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 light = normalize(float3(0.4, 1.0, 0.3));
    const float diffuse = max(dot(normalize(input.normal), light), 0.0);
    const float3 albedo = constants.colour.rgb * baseColour.Sample(input.uv).rgb;
    return float4(albedo * (0.25 + 0.75 * diffuse), 1.0);
}
