#include "Common.hlsli"
#include "D3D12.hlsli"
#include "GPUFont.hlsli"
#include "Random.hlsli"

struct Line
{
	float2 A;
	float2 B;
};

struct PassParameters
{
	uint2 Location;
	uint2 pad;
	uint2 GlyphDimensions;
	uint NumLines;
	float Scale;
	Line Lines[512];
};

ConstantBuffer<PassParameters> cPass : register(b1);
RWTexture2D<float> uOutput : register(u0);

uint IsInside(float2 location)
{
	uint isInside = 0;
	for(uint i = 0; i < cPass.NumLines; ++i)
	{
		Line currLine = cPass.Lines[i];
		if(currLine.A.y > location.y)
			break;
		float dy = currLine.A.y - currLine.B.y;
		if(dy == 0)
			continue;
		if(location.y >= currLine.A.y && location.y < currLine.B.y)
		{
			bool isLeft = (currLine.B.x - currLine.A.x) * (location.y - currLine.A.y) - (currLine.B.y - currLine.A.y) * (location.x - currLine.A.x) > 0;
			if (isLeft)
			{
				isInside = 1 - isInside;
			}
		}
	}
	return isInside;
}

static const int2 MSAA_16_Locations[] = {
	int2(1, 1), int2(-1, -3), int2(-3, 2), int2(4, -1),
	int2(-5, -2), int2(2, 5), int2(5, 3), int2(3, -5),
	int2(-2, 6), int2(0, -7), int2(-4, -6), int2(-6, 4),
	int2(-8, 0), int2(7, -4), int2(6, 7), int2(-7, 8),
};

static const int2 MSAA_8_Locations[] = {
	int2(1, -3), int2(-1, 3), int2(5, 1), int2(-3, -5),
	int2(-5, 5), int2(-7, -1), int2(3, 7), int2(7, -7),
};

static const int2 MSAA_4_Locations[] = {
	int2(-2, -6), int2(6, -2), int2(-6, 2), int2(2, 6),
};

static const int2 MSAA_1_Locations[] = {
	int2(0, 0)
};

[numthreads(8, 8, 1)]
void RasterizeGlyphCS(uint3 threadID : SV_DispatchThreadID)
{
	uint2 pixelIndex = threadID.xy;
	if(any(pixelIndex >= cPass.GlyphDimensions))
		return;

	float2 sampleCenter = pixelIndex + 0.5f;
	sampleCenter.y = cPass.GlyphDimensions.y - sampleCenter.y;

	uint insideSamples = 0;
	const uint numSamples = 16;
	for(uint sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
	{
		float2 location = sampleCenter + MSAA_16_Locations[sampleIndex] / 16.0f;
		insideSamples += IsInside(location / cPass.Scale);
	}
	float shade = (float)insideSamples / numSamples;
	uOutput[pixelIndex + cPass.Location] = shade;
}

RWStructuredBuffer<D3D12_DRAW_ARGUMENTS> uDrawArgs : register(u0);
RWStructuredBuffer<uint> uGlyphCounter : register(u1);

[numthreads(1, 1, 1)]
void BuildIndirectDrawArgsCS(uint threadID : SV_DispatchThreadID)
{
	D3D12_DRAW_ARGUMENTS args;
	args.VertexCountPerInstance = 4;
	args.InstanceCount = uGlyphCounter[0];
	args.StartVertexLocation = 0;
	args.StartInstanceLocation = 0;
	uDrawArgs[0] = args;
	uGlyphCounter[0] = 0;
}

struct RenderData
{
	float2 AtlasDimensionsInv;
	float2 TargetDimensions;
	float2 TargetDimensionsInv;
};

ConstantBuffer<RenderData> cData : register(b0);
Texture2D<float> tFontAtlas : register(t0);
StructuredBuffer<Glyph> tGlyphData : register(t1);
StructuredBuffer<GlyphInstance> tGlyphInstances : register(t2);

void RenderGlyphVS(
	uint vertexID : SV_VertexID,
	uint instanceID : SV_InstanceID,
	out float4 position : SV_Position,
	out float2 uv : TEXCOORD,
	out uint color : COLOR
	)
{
	uv = float2(vertexID % 2, vertexID / 2);

	GlyphInstance instance = tGlyphInstances[instanceID];
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
	uint color : COLOR
	) : SV_Target
{
	float alpha = tFontAtlas.SampleLevel(sLinearClamp, uv, 0);
	float4 c = alpha * UIntToColor(color);
	return float4(c.rgb, alpha);
}
