
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

[numthreads(8, 8, 1)]
void RasterizeGlyphCS(uint3 threadID : SV_DispatchThreadID)
{
	uint2 pixelIndex = threadID.xy;
	if(any(pixelIndex > cPass.GlyphDimensions))
		return;

	uint numSampleLocations = 16;
	float2 sampleLocations[] = {
		float2(1, 1), float2(-1, -3), float2(-3, 2), float2(4, -1),
		float2(-5, -2), float2(2, 5), float2(5, 3), float2(3, -5),
		float2(-2, 6), float2(0, -7), float2(-4, -6), float2(-6, 4),
		float2(-8, 0), float2(7, -4), float2(6, 7), float2(-7, 8),
	};

	float2 sampleCenter;
	sampleCenter.x = pixelIndex.x + 0.5f;
	sampleCenter.y = cPass.GlyphDimensions.y - pixelIndex.y + 0.5f;

	uint shade = 0;
	for(uint sampleIndex = 0; sampleIndex < numSampleLocations; ++sampleIndex)
	{
		float2 sampleOffset = sampleLocations[sampleIndex];
		float2 location = (sampleCenter + (sampleOffset / 16)) / cPass.Scale;
		shade += IsInside(location);
	}

	float color = (float)shade / numSampleLocations;
	uOutput[pixelIndex + cPass.Location] = float4(color.xxx, 1);
}
