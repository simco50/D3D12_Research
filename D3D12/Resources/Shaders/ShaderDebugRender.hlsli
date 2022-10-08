#include "Common.hlsli"

static const uint TEXT_COUNTER_OFFSET = 0;
static const uint TEXT_INSTANCES_OFFSET = 4;

struct Glyph
{
	uint2 Location;
	int2 Offset;
	uint2 Dimensions;
	uint Width;
};

struct CharacterInstance
{
	float2 Position;
	uint Character;
	uint Color;
};

void WriteChar(CharacterInstance instance, RWStructuredBuffer<uint> renderData)
{
	uint offset;
	InterlockedAdd(renderData[TEXT_COUNTER_OFFSET], 1, offset);
	offset *= sizeof(CharacterInstance);
	offset += TEXT_INSTANCES_OFFSET;
	renderData[offset++] = instance.Position.x;
	renderData[offset++] = instance.Position.y;
	renderData[offset++] = instance.Character;
	renderData[offset++] = instance.Color;
}

CharacterInstance ReadChar(uint index, StructuredBuffer<uint> renderData)
{
	CharacterInstance instance;
	uint offset = index * sizeof(CharacterInstance) + TEXT_INSTANCES_OFFSET;
	instance.Position.x = renderData[offset++];
	instance.Position.y = renderData[offset++];
	instance.Character = renderData[offset++];
	instance.Color = renderData[offset++];
	return instance;
}

struct TextWriter
{
	float2 StartLocation;
	float2 CursorLocation;

	void Text(uint character, uint color)
	{
		StructuredBuffer<Glyph> glyphBuffer = ResourceDescriptorHeap[cView.FontDataIndex];
		Glyph glyph = glyphBuffer[character];

		RWStructuredBuffer<uint> debugData = ResourceDescriptorHeap[cView.DebugRenderDataIndex];
		CharacterInstance instance;
		instance.Position = CursorLocation + int2(-glyph.Offset.x, glyph.Offset.y);
		instance.Character = character;
		instance.Color = color;
		WriteChar(instance, debugData);

		CursorLocation.x += glyph.Width;
	}

	void NewLine()
	{
		StructuredBuffer<Glyph> glyphBuffer = ResourceDescriptorHeap[cView.FontDataIndex];
		Glyph glyph = glyphBuffer[0];
		CursorLocation.y += glyph.Dimensions.y;
		CursorLocation.x = StartLocation.x;
	}

	void Text(uint a, uint b, uint color)
	{
		Text(a, color);
		Text(b, color);
	}

	void Text(uint a, uint b, uint c, uint color)
	{
		Text(a, b, color);
		Text(c, color);
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

	void Text(uint a, uint b, uint c, uint d, uint e, uint f, uint g, uint color)
	{
		Text(a, b, c, color);
		Text(d, e, f, g, color);
	}

	void Int(int value, uint color)
	{
		if(value < 0)
		{
			Text('-', color);
			value = -value;
		}
		uint length = value > 0 ? log10(value) + 1 : 1;
		uint divider = round(pow(10, length - 1));

		while(length > 0)
		{
			uint digit = value / divider;
			Text('0' + digit, color);
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
			Text('.', color);
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
