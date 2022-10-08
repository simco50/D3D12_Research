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
RWStructuredBuffer<uint> uGlyphInstancesCounter : register(u1);
StructuredBuffer<Glyph> tGlyphBuffer : register(t0);

struct TextWriter
{
	float2 StartLocation;
	float2 CursorLocation;

	void Character(uint character, uint color)
	{
		Glyph glyph = tGlyphBuffer[character];

		uint offset;
		InterlockedAdd(uGlyphInstancesCounter[0], 1, offset);
		GlyphInstance instance = (GlyphInstance)0;
		instance.Position = CursorLocation;
		instance.Character = character;
		instance.Color = color;
		uGlyphInstances[offset] = instance;

		CursorLocation.x += glyph.Width;
	}

	void NewLine()
	{
		Glyph glyph = tGlyphBuffer[0];
		CursorLocation.y += glyph.Dimensions.y;
		CursorLocation.x = StartLocation.x;
	}

	void Text(uint a, uint b, uint color)
	{
		Character(a, color);
		Character(b, color);
	}

	void Text(uint a, uint b, uint c, uint color)
	{
		Text(a, b, color);
		Character(c, color);
	}

	void Text(uint a, uint b, uint c, uint d, uint color)
	{
		Text(a, b, color);
		Text(c, d, color);
	}

	void Text(uint a, uint b, uint c, uint d, uint e, uint color)
	{
		Text(a, b, c, color);
		Text(d, e, color);
	}

	void Text(uint a, uint b, uint c, uint d, uint e, uint f, uint color)
	{
		Text(a, b, c, color);
		Text(d, e, f, color);
	}

	void Int(int value, uint color)
	{
		if(value < 0)
		{
			Character('-', color);
			value = -value;
		}
		uint length = value > 0 ? log10(value) + 1 : 1;
		uint divider = round(pow(10, length - 1));

		while(length > 0)
		{
			uint digit = value / divider;
			Character('0' + digit, color);
			--length;

			value = value - digit * divider;
			divider /= 10;
		}
	}

	void Float(float value, uint color)
	{
		if(isnan(value))
		{
			Text('N', 'a', 'N', color);
		}
		else if(!isfinite(value))
		{
			Text('I', 'N', 'F', color);
		}
		else
		{
			int v0 = floor(abs(value));
			Int(sign(value) * v0, color);
			Character('.', color);
			int v1 = floor(frac(value) * 10000);
			Int(v1, color);
		}
	}
};

TextWriter CreateTextWriter(float2 position)
{
	TextWriter writer;
	writer.StartLocation = position;
	writer.CursorLocation = position;
	return writer;
}
