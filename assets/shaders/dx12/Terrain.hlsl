// 布局必须与 CPU 侧 256 字节 TerrainConstants 保持一致
cbuffer TerrainConstants : register(b0)
{
    // CPU 使用 GLM 列主序矩阵并按 column_major 上传
    column_major float4x4 View;
    column_major float4x4 Projection;
    // 世界空间相机位置用于高光方向
    float4 CameraPosition;
    // 世界空间入射光方向
    float4 LightDirection;
    float4 LightColor;
    // x 环境光 y 漫反射 z 高光 w 调试叠加强度
    float4 LightingParameters;
    // x 为地形调试着色模式
    int4 DebugParameters;
};

// 地形材质纹理使用图形根签名中的 t0 和静态采样器 s0
Texture2D TerrainTexture : register(t0);
SamplerState TerrainSampler : register(s0);

// 输入布局直接映射 TerrainMeshVertex
struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float Height : TEXCOORD1;
    float3 DebugColor : COLOR0;
    float DebugHighlight : TEXCOORD2;
};

// 顶点阶段保留像素光照和调试着色所需的世界空间属性
struct VertexOutput
{
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD1;
    float Height : TEXCOORD2;
    float3 DebugColor : COLOR0;
    float DebugHighlight : TEXCOORD3;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.WorldPosition = input.Position;
    output.Normal = input.Normal;
    output.TexCoord = input.TexCoord;
    output.Height = input.Height;
    output.DebugColor = input.DebugColor;
    output.DebugHighlight = input.DebugHighlight;
    // 地形顶点已经位于世界空间，无需额外模型矩阵
    output.Position = mul(Projection, mul(View, float4(input.Position, 1.0)));
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    // 使用 Blinn-Phong 半程向量计算当前实验基线光照
    const float3 normal = normalize(input.Normal);
    const float3 lightDir = normalize(-LightDirection.xyz);
    const float3 viewDir = normalize(CameraPosition.xyz - input.WorldPosition);
    const float3 halfDir = normalize(lightDir + viewDir);

    // 固定平铺频率保持不同 LOD 算法之间材质尺度一致
    const float3 textureColor = TerrainTexture.Sample(TerrainSampler, input.TexCoord * 12.0).rgb;
    // 高处逐渐混入岩土色，避免单一纹理掩盖地形形态
    const float heightBlend = smoothstep(0.2, 0.92, input.Height);
    const float3 heightTint = lerp(float3(0.10, 0.32, 0.12), float3(0.66, 0.62, 0.48), heightBlend);
    const float3 baseColor = lerp(textureColor, heightTint, 0.35);

    const float diffuse = max(dot(normal, lightDir), 0.0);
    const float specular = pow(max(dot(normal, halfDir), 0.0), 32.0);
    float3 lighting = baseColor *
        (LightingParameters.x + diffuse * LightingParameters.y) * LightColor.rgb;
    lighting += LightColor.rgb * specular * LightingParameters.z;

    // 调试模式叠加算法生成的深度和本次更新颜色
    if (DebugParameters.x == 1)
    {
        const float highlight = saturate(input.DebugHighlight);
        float3 debugLit = input.DebugColor * (0.45 + 0.45 * diffuse);
        debugLit += input.DebugColor * highlight * 0.35;
        lighting = lerp(lighting, debugLit, saturate(LightingParameters.w));
    }

    return float4(lighting, 1.0);
}
