#include "Common.hlsli"

struct CharacterInstance
{
	float2 Position;
	uint Character;
	uint Color;
};

struct LineInstance
{
	float3 A;
	float3 B;
	uint Color;
};

static const uint TEXT_COUNTER_OFFSET = 0;
static const uint LINE_COUNTER_OFFSET = 1;
static const uint DATA_OFFSET = 4;
static const uint TEXT_INSTANCES_OFFSET = DATA_OFFSET;
static const uint LINE_INSTANCES_OFFSET = TEXT_INSTANCES_OFFSET + 256 * sizeof(CharacterInstance);

struct Glyph
{
	uint2 Location;
	int2 Offset;
	uint2 Dimensions;
	uint Width;
};

struct FontColor
{
	uint Color;
};

FontColor MakeFontColor(float4 color)
{
	FontColor clr = (FontColor)0;
	clr.Color |= (uint)(saturate(color.r) * 255.0f) << 0u;
	clr.Color |= (uint)(saturate(color.g) * 255.0f) << 8u;
	clr.Color |= (uint)(saturate(color.b) * 255.0f) << 16u;
	clr.Color |= (uint)(saturate(color.a) * 255.0f) << 24u;
	return clr;
}

void DrawChar(float2 position, uint character, uint color)
{
	RWByteAddressBuffer renderData = ResourceDescriptorHeap[cView.DebugRenderDataIndex];
	uint offset;
	renderData.InterlockedAdd(TEXT_COUNTER_OFFSET * 4, 1, offset);
	offset *= sizeof(CharacterInstance);
	offset += TEXT_INSTANCES_OFFSET;
	CharacterInstance instance;
	instance.Position = position;
	instance.Character = character;
	instance.Color = color;
	renderData.Store(offset * 4, instance);
}

void DrawLine(float3 a, float3 b, uint color)
{
	RWByteAddressBuffer renderData = ResourceDescriptorHeap[cView.DebugRenderDataIndex];
	uint offset;
	renderData.InterlockedAdd(LINE_COUNTER_OFFSET * 4, 1, offset);
	offset *= sizeof(LineInstance);
	offset += LINE_INSTANCES_OFFSET;
	LineInstance instance;
	instance.A = a;
	instance.B = b;
	instance.Color = color;
	renderData.Store(offset * 4, instance);
}

void DrawCube(float3 center, float3 extents, uint color)
{
	DrawLine(center + float3(-1, -1, -1) * extents, center + float3(1, -1, -1) * extents, color);
	DrawLine(center + float3(-1, -1, -1) * extents, center + float3(-1, 1, -1) * extents, color);
	DrawLine(center + float3(-1, -1, -1) * extents, center + float3(-1, -1, 1) * extents, color);
	DrawLine(center + float3(1, 1, 1) * extents, center + float3(1, 1, -1) * extents, color);
	DrawLine(center + float3(1, 1, 1) * extents, center + float3(-1, 1, 1) * extents, color);
	DrawLine(center + float3(1, 1, 1) * extents, center + float3(1, -1, 1) * extents, color);
	DrawLine(center + float3(-1, 1, 1) * extents, center + float3(-1, -1, 1) * extents, color);
	DrawLine(center + float3(1, 1, -1) * extents, center + float3(1, -1, -1) * extents, color);
	DrawLine(center + float3(1, -1, 1) * extents, center + float3(-1, -1, 1) * extents, color);
	DrawLine(center + float3(1, -1, -1) * extents, center + float3(1, -1, 1) * extents, color);
	DrawLine(center + float3(-1, 1, -1) * extents, center + float3(1, 1, -1) * extents, color);
	DrawLine(center + float3(-1, 1, -1) * extents, center + float3(-1, 1, 1) * extents, color);
}

struct TextWriter
{
	float2 StartLocation;
	float2 CursorLocation;
	uint Color;

	void SetColor(FontColor color)
	{
		Color = color.Color;
	}

	void Text(uint character)
	{
		StructuredBuffer<Glyph> glyphBuffer = ResourceDescriptorHeap[cView.FontDataIndex];
		Glyph glyph = glyphBuffer[character];

		DrawChar(
			CursorLocation + int2(-glyph.Offset.x, glyph.Offset.y),
			character,
			Color);

		CursorLocation.x += glyph.Width;
	}

	void NewLine()
	{
		StructuredBuffer<Glyph> glyphBuffer = ResourceDescriptorHeap[cView.FontDataIndex];
		Glyph glyph = glyphBuffer[0];
		CursorLocation.y += glyph.Dimensions.y;
		CursorLocation.x = StartLocation.x;
	}

	void Text(uint a, uint b)
	{
		Text(a);
		Text(b);
	}

	void Text(uint a, uint b, uint c)
	{
		Text(a, b);
		Text(c);
	}

	void Text(uint a, uint b, uint c, uint d)
	{
		Text(a, b, c);
		Text(d);
	}

	void Text(uint a, uint b, uint c, uint d, uint e)
	{
		Text(a, b, c, d);
		Text(e);
	}

	void Text(uint a, uint b, uint c, uint d, uint e, uint f)
	{
		Text(a, b, c, d, e);
		Text(f);
	}

	void Text(uint a, uint b, uint c, uint d, uint e, uint f, uint g)
	{
		Text(a, b, c, d, e, f);
		Text(g);
	}

	void Text(uint a, uint b, uint c, uint d, uint e, uint f, uint g, uint h)
	{
		Text(a, b, c, d, e, f, g);
		Text(h);
	}

	void Int(int value)
	{
		if(value < 0)
		{
			Text('-');
			value = -value;
		}
		uint length = value > 0 ? log10(value) + 1 : 1;
		uint divider = round(pow(10, length - 1));

		while(length > 0)
		{
			uint digit = value / divider;
			Text('0' + digit);
			--length;

			value = value - digit * divider;
			divider /= 10;
		}
	}

	void Float(float value)
	{
		if(isnan(value))
		{
			Text('N', 'a', 'N');
		}
		else if(!isfinite(value))
		{
			Text('I', 'N', 'F');
		}
		else
		{
			int v0 = floor(abs(value));
			Int(sign(value) * v0);
			Text('.');
			int v1 = floor(frac(value) * 10000);
			Int(v1);
		}
	}

	void Float(float2 value)
	{
		Float(value.x);
		Text(',', ' ');
		Float(value.y);
	}

	void Float(float3 value)
	{
		Float(value.x);
		Text(',', ' ');
		Float(value.y);
		Text(',', ' ');
		Float(value.z);
	}

	void Float(float4 value)
	{
		Float(value.x);
		Text(',', ' ');
		Float(value.y);
		Text(',', ' ');
		Float(value.z);
		Text(',', ' ');
		Float(value.w);
	}

	TextWriter operator+(uint character)
	{
		Text(character);
		TextWriter writer;
		writer.StartLocation = StartLocation;
		writer.CursorLocation = CursorLocation;
		writer.Color = Color;
		return writer;
	}

	TextWriter operator+(float value)
	{
		Float(value);
		TextWriter writer;
		writer.StartLocation = StartLocation;
		writer.CursorLocation = CursorLocation;
		writer.Color = Color;
		return writer;
	}
};

TextWriter CreateTextWriter(float2 position, FontColor color = MakeFontColor(1))
{
	TextWriter writer;
	writer.StartLocation = position;
	writer.CursorLocation = position;
	writer.Color = color.Color;
	return writer;
}
