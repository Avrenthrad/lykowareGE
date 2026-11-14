// Metal-roughness materials with a normal map (ADR 0085). The renderer draws every part with
// these entry points while materials are on. A part without a material (material.w = 1) is lit
// exactly as lit.slang's fragmentMain lights it, so turning materials on changes nothing else.
//
// Sets: 0 the base colour texture, 1 a ShadowMap (read only by the shadowed entry points), 2 the
// normal map (linear), 3 the metal-roughness texture (linear; green roughness, blue metalness).
// Matrices arrive as float4 columns and are applied by hand, as in lit.slang.

struct Constants
{
    // projection * view * model, by column (math::Mat4 layout).
    float4 clip0;
    float4 clip1;
    float4 clip2;
    float4 clip3;
    // The normal matrix by column; w is the light's direction (x, y, z), as in lit.slang.
    float4 normal0;
    float4 normal1;
    float4 normal2;
    // Base colour (rgb); a is the ambient share.
    float4 colour;
    // The camera-relative model matrix: rotation * scale by column, w the translation.
    float4 model0;
    float4 model1;
    float4 model2;
    // x roughness, y metalness, z normal scale, w 1 for a part without a material.
    float4 material;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

[[vk::binding(0, 0)]] Sampler2D baseColour;
[[vk::binding(0, 2)]] Sampler2D normalMap;
[[vk::binding(0, 3)]] Sampler2D metalRoughness;

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

VertexOutput transform(VertexInput input)
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

static const float pi = 3.14159265358979;

// The normal-mapped normal. The tangent frame comes from screen-space derivatives of the
// position and texture coordinate, so meshes need no tangents. The map's +y is up in the image,
// which is decreasing v.
float3 shadingNormal(VertexOutput input)
{
    const float3 n = normalize(input.normal);
    const float scale = constants.material.z;
    if (scale == 0.0)
    {
        return n;
    }
    const float3 dp1 = ddx(input.world);
    const float3 dp2 = ddy(input.world);
    const float2 duv1 = ddx(input.uv);
    const float2 duv2 = ddy(input.uv);
    const float3 dp2perp = cross(dp2, n);
    const float3 dp1perp = cross(n, dp1);
    // These are dP/du and dP/dv times the determinant n . (dp1 x dp2), whose sign depends on the
    // screen's handedness (Vulkan's y points down), so the determinant's sign is divided back out.
    const float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 b = -(dp2perp * duv1.y + dp1perp * duv2.y);
    const float longest = max(dot(t, t), dot(b, b));
    const float determinant = dot(n, cross(dp1, dp2));
    if (longest <= 0.0 || determinant == 0.0)
    {
        return n;
    }
    const float inverse = rsqrt(longest) * sign(determinant);
    float3 m = normalMap.Sample(input.uv).xyz * 2.0 - 1.0;
    m.xy *= scale;
    return normalize(t * inverse * m.x + b * inverse * m.y + n * m.z);
}

// colour * texture * (ambient + (1 - ambient) * direct), where direct is Lambert for a part
// without a material and GGX metal-roughness for one with, scaled by the light reaching it.
float3 shade(VertexOutput input, float reaching)
{
    const float3 light = normalize(float3(constants.normal0.w, constants.normal1.w, constants.normal2.w));
    const float ambient = constants.colour.a;
    const float3 albedo = constants.colour.rgb * baseColour.Sample(input.uv).rgb;
    if (constants.material.w != 0.0)
    {
        const float diffuse = max(dot(normalize(input.normal), light), 0.0) * reaching;
        return albedo * (ambient + (1.0 - ambient) * diffuse);
    }
    const float3 n = shadingNormal(input);
    const float3 v = normalize(-input.world);
    const float4 texel = metalRoughness.Sample(input.uv);
    const float roughness = clamp(constants.material.x * texel.g, 0.03, 1.0);
    const float metalness = saturate(constants.material.y * texel.b);
    const float nl = max(dot(n, light), 0.0);
    const float nv = max(dot(n, v), 1e-4);
    const float3 h = normalize(light + v);
    const float nh = max(dot(n, h), 0.0);
    const float vh = max(dot(v, h), 0.0);
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = nh * nh * (a2 - 1.0) + 1.0;
    const float distribution = a2 / (pi * d * d);
    const float k = a * 0.5;
    const float geometry = (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    const float3 specular = distribution * geometry * fresnel / (4.0 * nv * max(nl, 1e-4));
    const float3 diffuse = (1.0 - fresnel) * (1.0 - metalness) * albedo;
    // The light's radiance is pi, so a white Lambert surface facing it gives 1, as in lit.slang.
    const float3 direct = (diffuse + pi * specular) * nl * reaching;
    return albedo * ambient + (1.0 - ambient) * direct;
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    return transform(input);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(shade(input, 1.0), 1.0);
}

// --- With a shadow (ADR 0084) --------------------------------------------------------------
// The same map lookup as lit.slang's lightReaching; kept in step with it.

struct ShadowBlock
{
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
    VertexOutput surface;
    float4 light : TEXCOORD1;
};

[shader("vertex")]
ShadowedOutput vertexShadowed(VertexInput input)
{
    ShadowedOutput output;
    output.surface = transform(input);
    const float4 p = output.surface.position;
    output.light = shadow.toLight0 * p.x + shadow.toLight1 * p.y + shadow.toLight2 * p.z + shadow.toLight3 * p.w;
    return output;
}

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
            const float occluder = shadowMap.SampleLevel(uv + float2(float(x), float(y)) * shadow.params.y, 0.0);
            reaching += occluder > p.z + shadow.params.x ? 0.0 : 1.0;
        }
    }
    return reaching / 9.0;
}

[shader("fragment")]
float4 fragmentShadowed(ShadowedOutput input) : SV_Target
{
    return float4(shade(input.surface, lightReaching(input.light)), 1.0);
}
