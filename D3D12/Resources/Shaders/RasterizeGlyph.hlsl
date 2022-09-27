#include "Common.hlsli"

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
	Line Lines[1024];
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture2D<float4> uOutput : register(u0);

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

static const int2 MSAA_1_Locations[] = {
	int2(0, 0)
};

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
	float3 shade = (float)insideSamples / numSamples;

	if(any(pixelIndex == 0) || any(pixelIndex + 1 == cPass.GlyphDimensions))
	{
		//shade = float3(1, 0, 0);
	}
	uOutput[pixelIndex + cPass.Location] = float4(shade.xyz, 1);
}

struct Glyph
{
	uint2 Location;
	uint2 Offset;
	uint2 Dimensions;
};

struct GlyphInstance
{
	float2 Position;
	uint GlyphIndex;
	uint padd;
};

struct RenderData
{
	GlyphInstance Instances[128];
	float2 AtlasDimensionsInv;
	float2 TargetDimensions;
	float2 TargetDimensionsInv;
};

Texture2D<float4> tFontAtlas : register(t0);
StructuredBuffer<Glyph> tGlyphData : register(t1);
ConstantBuffer<RenderData> cData : register(b0);

void RenderGlyphVS(
	uint vertexID : SV_VertexID,
	uint instanceID : SV_InstanceID,
	out float4 position : SV_Position,
	out float2 uv : TEXCOORD
	)
{
	uv = float2(vertexID % 2,vertexID / 2);

	GlyphInstance instance = cData.Instances[instanceID];
	Glyph glyph = tGlyphData[instance.GlyphIndex];

	float2 pos = float2(uv.x, uv.y);
	pos *= glyph.Dimensions;
	pos += instance.Position;
	pos *= cData.TargetDimensionsInv;

	position = float4(pos * float2(2, -2) + float2(-1, 1), 0, 1);
	uv = (uv * glyph.Dimensions + glyph.Location) * cData.AtlasDimensionsInv;
}

float4 RenderGlyphPS(
	float4 position : SV_Position,
	float2 uv : TEXCOORD
	) : SV_Target
{
	return tFontAtlas.SampleLevel(sLinearClamp, uv, 0);
}
