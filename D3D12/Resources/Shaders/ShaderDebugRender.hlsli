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
	uint ScreenSpace;
};

// Must match .cpp!
static const uint MAX_NUM_COUNTERS = 4;
static const uint MAX_NUM_TEXT = 1024;
static const uint MAX_NUM_LINES = 8192;

static const uint TEXT_COUNTER_OFFSET = 0;
static const uint LINE_COUNTER_OFFSET = 4;
static const uint TEXT_INSTANCES_OFFSET = MAX_NUM_COUNTERS * sizeof(uint);
static const uint LINE_INSTANCES_OFFSET = TEXT_INSTANCES_OFFSET + MAX_NUM_TEXT * sizeof(CharacterInstance);

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

void DrawChar(float2 position, uint character, uint color = 0xFFFFFFFF)
{
	CharacterInstance instance;
	instance.Position = position;
	instance.Character = character;
	instance.Color = color;

	RWByteAddressBuffer renderData = ResourceDescriptorHeap[cView.DebugRenderDataIndex];
	uint offset;
	renderData.InterlockedAdd(TEXT_COUNTER_OFFSET, 1, offset);
	renderData.Store(TEXT_INSTANCES_OFFSET + offset * sizeof(CharacterInstance), instance);
}

void DrawLine(float3 a, float3 b, uint color = 0xFFFFFFFF)
{
	LineInstance instance;
	instance.A = a;
	instance.B = b;
	instance.Color = color;
	instance.ScreenSpace = 0;

	RWByteAddressBuffer renderData = ResourceDescriptorHeap[cView.DebugRenderDataIndex];
	uint offset;
	renderData.InterlockedAdd(LINE_COUNTER_OFFSET, 1, offset);
	renderData.Store(LINE_INSTANCES_OFFSET + offset * sizeof(LineInstance), instance);
}

void DrawAxisBase(float3 position, float3x3 rotation, float axisLength = 0.25f, uint color = 0xFFFFFFFF)
{
	DrawLine(position, position + axisLength * normalize(mul(float3(1, 0, 0), rotation)), 0xFF0000FF);
	DrawLine(position, position + axisLength * normalize(mul(float3(0, 1, 0), rotation)), 0x00FF00FF);
	DrawLine(position, position + axisLength * normalize(mul(float3(0, 0, 1), rotation)), 0x0000FFFF);
}

void DrawCube(float3 center, float3 extents, uint color = 0xFFFFFFFF)
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

void DrawScreenLine(float2 a, float2 b, uint color = 0xFFFFFFFF)
{
	LineInstance instance;
	instance.A = float3(a, 0);
	instance.B = float3(b, 0);
	instance.Color = color;
	instance.ScreenSpace = 1;

	RWByteAddressBuffer renderData = ResourceDescriptorHeap[cView.DebugRenderDataIndex];
	uint offset;
	renderData.InterlockedAdd(LINE_COUNTER_OFFSET, 1, offset);
	renderData.Store(LINE_INSTANCES_OFFSET + offset * sizeof(LineInstance), instance);
}

void DrawRect(float2 a, float2 b, uint color = 0xFFFFFFFF)
{
	DrawScreenLine(float2(a.x, a.y), float2(b.x, a.y), color);
	DrawScreenLine(float2(a.x, a.y), float2(a.x, b.y), color);
	DrawScreenLine(float2(b.x, a.y), float2(b.x, b.y), color);
	DrawScreenLine(float2(a.x, b.y), float2(b.x, b.y), color);
}


struct TextWriter
{
	float2 StartLocation;
	float2 Cursor;
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
			Cursor + int2(-glyph.Offset.x, glyph.Offset.y),
			character,
			Color);

		Cursor.x += glyph.Width;
	}

	void NewLine()
	{
		StructuredBuffer<Glyph> glyphBuffer = ResourceDescriptorHeap[cView.FontDataIndex];
		Glyph glyph = glyphBuffer[0];
		Cursor.y += glyph.Dimensions.y;
		Cursor.x = StartLocation.x;
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
		writer.Cursor = Cursor;
		writer.Color = Color;
		return writer;
	}

	TextWriter operator+(float value)
	{
		Float(value);
		TextWriter writer;
		writer.StartLocation = StartLocation;
		writer.Cursor = Cursor;
		writer.Color = Color;
		return writer;
	}
};

TextWriter CreateTextWriter(float2 position, FontColor color = MakeFontColor(1))
{
	TextWriter writer;
	writer.StartLocation = position;
	writer.Cursor = position;
	writer.Color = color.Color;
	return writer;
}
