#include "Common.hlsli"

struct GlyphInstance
{
	float2 Position;
	uint Character;
	uint Color;
};

struct Glyph
{
	uint2 Location;
	uint2 Offset;
	uint2 Dimensions;
	uint Width;
};

RWStructuredBuffer<GlyphInstance> uGlyphInstances : register(u0);
RWBuffer<uint> uGlyphInstancesCounter : register(u1);
StructuredBuffer<Glyph> tGlyphBuffer : register(t0);

void PrintCharacter(uint character, inout float2 position, uint color = 0xFFFFFFFF)
{
	uint offset;
	InterlockedAdd(uGlyphInstancesCounter[0], 1, offset);
	GlyphInstance instance = (GlyphInstance)0;
	instance.Position = position;
	instance.Character = character;
	instance.Color = color;
	uGlyphInstances[offset] = instance;

	Glyph glyph = tGlyphBuffer[character];
	position.x += glyph.Width;
}

void NewLine(inout float2 position, uint x)
{
	Glyph glyph = tGlyphBuffer[0];
	position.y += glyph.Dimensions.y;
	position.x = x;
}

void PrintText(uint a, uint b, inout float2 position, uint color = 0xFFFFFFFF)
{
	PrintCharacter(a, position, color);
	PrintCharacter(b, position, color);
}

void PrintText(uint a, uint b, uint c, inout float2 position, uint color = 0xFFFFFFFF)
{
	PrintText(a, b, position, color);
	PrintCharacter(c, position, color);
}

void PrintText(uint a, uint b, uint c, uint d, inout float2 position, uint color = 0xFFFFFFFF)
{
	PrintText(a, b, position, color);
	PrintText(c, d, position, color);
}

void PrintText(uint a, uint b, uint c, uint d, uint e, inout float2 position, uint color = 0xFFFFFFFF)
{
	PrintText(a, b, c, position, color);
	PrintText(d, e, position, color);
}

void PrintText(uint a, uint b, uint c, uint d, uint e, uint f, inout float2 position, uint color = 0xFFFFFFFF)
{
	PrintText(a, b, c, position, color);
	PrintText(d, e, f, position, color);
}

void PrintInt(int value, inout float2 position, uint color = 0xFFFFFFFF)
{
	if(value < 0)
	{
		PrintCharacter('-', position, color);
		value = -value;
	}
	uint length = value > 0 ? log10(value) + 1 : 1;
	uint divider = round(pow(10, length - 1));

	while(length > 0)
	{
		uint digit = value / divider;
		PrintCharacter('0' + digit, position, color);
		--length;

		value = value - digit * divider;
		divider /= 10;
	}
}
