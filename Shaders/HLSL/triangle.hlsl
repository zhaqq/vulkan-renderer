struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

static const float2 positions[3] =
{
    float2(0.0, -0.5),
    float2(0.5, 0.5),
    float2(-0.5, 0.5)
};

static const float3 colors[3] =
{
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0)
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.color = colors[vertexID];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}

/*
// GLSL equivalent for reference
// #version 450
// layout(location = 0) out vec3 fragColor;
// vec2 positions[3] = vec2[](vec2(0.0,-0.5), vec2(0.5,0.5), vec2(-0.5,0.5));
// vec3 colors[3] = vec3[](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1));
// void main() { gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0); fragColor = colors[gl_VertexIndex]; }
*/