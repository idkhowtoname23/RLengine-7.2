cbuffer PostProcessCB : register(b0)
{
    float Exposure;
    float BloomThreshold;
    float BloomIntensity;
    float VignetteIntensity;
    float2 TexelSize;
    float2 Pad;
};

Texture2D g_HdrTexture : register(t0);
SamplerState g_LinearClamp : register(s0);

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.Position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    output.UV = uv;
    return output;
}

float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(VSOutput input) : SV_Target
{
    float3 hdrColor = g_HdrTexture.Sample(g_LinearClamp, input.UV).rgb;

    float3 bloom = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    static const float weights[3] = { 0.25f, 0.5f, 0.25f };

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float w = weights[x + 1] * weights[y + 1];
            float2 offset = float2((float)x, (float)y) * TexelSize * 2.0f;
            float3 s = g_HdrTexture.Sample(g_LinearClamp, input.UV + offset).rgb;
            float luma = dot(s, float3(0.2126f, 0.7152f, 0.0722f));
            float3 bright = (luma > BloomThreshold) ? (s * ((luma - BloomThreshold) / max(luma, 1e-4f))) : float3(0.0f, 0.0f, 0.0f);
            bloom += bright * w;
            totalWeight += w;
        }
    }

    bloom /= max(totalWeight, 1e-4f);
    hdrColor += bloom * BloomIntensity;
    hdrColor *= Exposure;

    float3 color = ACESFilm(hdrColor);
    color = pow(saturate(color), 1.0f / 2.2f);

    float2 centered = input.UV - float2(0.5f, 0.5f);
    float dist = length(centered);
    float vignette = smoothstep(0.8f, 0.25f, dist);
    color *= lerp(1.0f, vignette, saturate(VignetteIntensity));

    return float4(color, 1.0f);
}
