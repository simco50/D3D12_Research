cbuffer Data : register(b0)
{
      float4x4 cViewProj;
}

struct VS_INPUT
{
      float2 position : POSITION;
      float2 texCoord  : TEXCOORD0;
      float4 color : COLOR0;
};

struct PS_INPUT
{
      float4 position : SV_POSITION;
      float2 texCoord  : TEXCOORD0;
      float4 color : COLOR0;
};

PS_INPUT VSMain(VS_INPUT input)
{
      PS_INPUT output = (PS_INPUT)0;

      output.position = mul(float4(input.position.xy, 0.5f, 1.f), cViewProj);
      output.color = input.color;
      output.texCoord = input.texCoord;

      return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
      return input.color;
}