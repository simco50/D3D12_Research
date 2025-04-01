#include "Common.hlsli"
#include "D3D12.hlsli"
#include "ShaderDebugRender.hlsli"

struct BuildArgsParams
{
	RWByteBufferH RenderData;
	RWStructuredBufferH<D3D12_DRAW_ARGUMENTS> DrawArgs;
};
DEFINE_CONSTANTS(BuildArgsParams, 0);

[numthreads(1, 1, 1)]
void BuildIndirectDrawArgsCS(uint threadID : SV_DispatchThreadID)
{
	uint numCharacters = cBuildArgsParams.RenderData.Load<uint>(TEXT_COUNTER_OFFSET);
	cBuildArgsParams.RenderData.Store<uint>(TEXT_COUNTER_OFFSET, 0);
	uint numLines = cBuildArgsParams.RenderData.Load<uint>(LINE_COUNTER_OFFSET);
	cBuildArgsParams.RenderData.Store<uint>(LINE_COUNTER_OFFSET, 0);

	uint offset = 0;
	D3D12_DRAW_ARGUMENTS args = (D3D12_DRAW_ARGUMENTS)0;
	{
		args.VertexCountPerInstance = 4;
		args.InstanceCount = min(numCharacters, MAX_NUM_TEXT);
		cBuildArgsParams.DrawArgs.Store(offset++, args);
	}
	{
		args.VertexCountPerInstance = 2;
		args.InstanceCount = min(numLines, MAX_NUM_LINES);
		cBuildArgsParams.DrawArgs.Store(offset++, args);
	}
}

struct RenderParams
{
	float2 TargetDimensionsInv;
	Texture2DH<float4> FontAtlas;
	StructuredBufferH<Glyph> GlyphData;
	ByteBufferH RenderData;
	Texture2DH<float> Depth;
};
DEFINE_CONSTANTS(RenderParams, 0);

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
	CharacterInstance instance = UnpackCharacterInstance(cRenderParams.RenderData.Load<PackedCharacterInstance>(TEXT_INSTANCES_OFFSET + offset));

	Glyph glyph = cRenderParams.GlyphData[instance.Character];

	float2 pos = float2(uv.x, uv.y);
	pos *= glyph.Dimensions;
	pos *= instance.Scale;
	pos += instance.Position;
	pos *= cRenderParams.TargetDimensionsInv;

	position = float4(pos * float2(2, -2) + float2(-1, 1), 0, 1);
	uv = lerp(glyph.MinUV, glyph.MaxUV, uv);
	color = instance.Color;
}

float4 RenderGlyphPS(
	float4 position : SV_Position,
	float2 uv : TEXCOORD,
	float4 color : COLOR
	) : SV_Target
{
	return cRenderParams.FontAtlas.SampleLevel(sLinearClamp, uv, 0) * color;
}

void RenderLineVS(
	uint vertexID : SV_VertexID,
	uint instanceID : SV_InstanceID,
	out float4 position : SV_Position,
	out float4 color : COLOR
	)
{
	uint offset = instanceID * sizeof(PackedLineInstance);
	LineInstance instance = UnpackLineInstance(cRenderParams.RenderData.Load<PackedLineInstance>(LINE_INSTANCES_OFFSET + offset));

	color = vertexID == 0 ? instance.ColorA : instance.ColorB;
	float3 wPos = vertexID == 0 ? instance.A : instance.B;
	if(instance.ScreenSpace)
	{
		position = float4(wPos.xy * 2 - 1, 1, 1);
		position.y *= -1;
	}
	else
	{
		position = mul(float4(wPos, 1), cView.WorldToClip);
	}
}

float4 RenderLinePS(
	float4 position : SV_POSITION,
	float4 color : COLOR
	) : SV_TARGET
{
	uint2 pixel = position.xy;
	float2 uv = pixel.xy * cView.ViewportDimensionsInv;
	float depth = cRenderParams.Depth.SampleLevel(sPointClamp, uv, 0);

	bool occluded = depth > position.z;
	float checkers = any(pixel % 2 == 0) ? 1.0f : 0.0f;
	float alpha = occluded ? checkers * 0.7f : 1.0f;

	return float4(color.xyz, color.w * alpha);
}
