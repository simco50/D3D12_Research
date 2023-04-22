#include "Common.hlsli"
#include "D3D12.hlsli"
#include "ShaderDebugRender.hlsli"

RWByteAddressBuffer uRenderData : register(u0);
RWStructuredBuffer<D3D12_DRAW_ARGUMENTS> uDrawArgs : register(u1);

[numthreads(1, 1, 1)]
void BuildIndirectDrawArgsCS(uint threadID : SV_DispatchThreadID)
{
	uint numCharacters = uRenderData.Load(TEXT_COUNTER_OFFSET);
	uRenderData.Store(TEXT_COUNTER_OFFSET, 0);
	uint numLines = uRenderData.Load(LINE_COUNTER_OFFSET);
	uRenderData.Store(LINE_COUNTER_OFFSET, 0);

	uint offset = 0;
	D3D12_DRAW_ARGUMENTS args = (D3D12_DRAW_ARGUMENTS)0;
	{
		args.VertexCountPerInstance = 4;
		args.InstanceCount = min(numCharacters, MAX_NUM_TEXT);
		uDrawArgs[offset++] = args;
	}
	{
		args.VertexCountPerInstance = 2;
		args.InstanceCount = min(numLines, MAX_NUM_LINES);
		uDrawArgs[offset++] = args;
	}
}

struct RenderData
{
	float2 AtlasDimensionsInv;
	float2 TargetDimensionsInv;
};

ConstantBuffer<RenderData> cData : register(b0);
Texture2D<float> tFontAtlas : register(t0);
StructuredBuffer<Glyph> tGlyphData : register(t1);
ByteAddressBuffer tRenderData : register(t2);
Texture2D<float> tDepth : register(t3);

void RenderGlyphVS(
	uint vertexID : SV_VertexID,
	uint instanceID : SV_InstanceID,
	out float4 position : SV_Position,
	out float2 uv : TEXCOORD,
	out float4 color : COLOR
	)
{
	uv = float2(vertexID % 2, vertexID / 2);

	uint offset = instanceID * sizeof(PackedCharacterInstance);
	CharacterInstance instance = UnpackCharacterInstance(tRenderData.Load<PackedCharacterInstance>(TEXT_INSTANCES_OFFSET + offset));

	Glyph glyph = tGlyphData[instance.Character];

	float2 pos = float2(uv.x, uv.y);
	pos *= glyph.Dimensions;
	pos += instance.Position;
	pos *= cData.TargetDimensionsInv;

	position = float4(pos * float2(2, -2) + float2(-1, 1), 0, 1);
	uv = (uv * glyph.Dimensions + glyph.Location) * cData.AtlasDimensionsInv;
	color = instance.Color;
}

float4 RenderGlyphPS(
	float4 position : SV_Position,
	float2 uv : TEXCOORD,
	float4 color : COLOR
	) : SV_Target
{
	float alpha = tFontAtlas.SampleLevel(sLinearClamp, uv, 0);
	float4 c = alpha * color;
	return float4(c.rgb, alpha);
}

void RenderLineVS(
	uint vertexID : SV_VertexID,
	uint instanceID : SV_InstanceID,
	out float4 position : SV_Position,
	out float4 color : COLOR
	)
{
	uint offset = instanceID * sizeof(PackedLineInstance);
	LineInstance instance = UnpackLineInstance(tRenderData.Load<PackedLineInstance>(LINE_INSTANCES_OFFSET + offset));

	color = vertexID == 0 ? instance.ColorA : instance.ColorB;
	float3 wPos = vertexID == 0 ? instance.A : instance.B;
	if(instance.ScreenSpace)
	{
		position = float4(wPos.xy * 2 - 1, 1, 1);
		position.y *= -1;
	}
	else
	{
		position = mul(float4(wPos, 1), cView.ViewProjection);
	}
}

float4 RenderLinePS(
	float4 position : SV_POSITION,
	float4 color : COLOR
	) : SV_TARGET
{
	uint2 pixel = position.xy;
	float2 uv = pixel.xy * cView.TargetDimensionsInv;
	float depth = tDepth.SampleLevel(sPointClamp, uv, 0);

	bool occluded = depth > position.z;
	float checkers = any(pixel % 2 == 0) ? 1.0f : 0.0f;
	float alpha = occluded ? checkers * 0.7f : 1.0f;

	return float4(color.xyz, color.w * alpha);
}
