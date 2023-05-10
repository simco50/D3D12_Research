#include "Common.hlsli"
#include "Packing.hlsli"

struct CharacterInstance
{
	float4 Color;
	float2 Position;
	uint Character;
};

struct PackedCharacterInstance
{
	uint Position;
	uint Character;
	uint Color;
};

PackedCharacterInstance PackCharacterInstance(CharacterInstance c)
{
	PackedCharacterInstance p;
	p.Position = Pack_RG16_FLOAT(c.Position);
	p.Character = c.Character;
	p.Color = Pack_RGBA8_UNORM(c.Color);
	return p;
}

CharacterInstance UnpackCharacterInstance(PackedCharacterInstance p)
{
	CharacterInstance c;
	c.Position = Unpack_RG16_FLOAT(p.Position);
	c.Character = p.Character;
	c.Color = Unpack_RGBA8_UNORM(p.Color);
	return c;
}

struct LineInstance
{
	float4 ColorA;
	float3 A;
	float4 ColorB;
	float3 B;
	bool ScreenSpace;
};

struct PackedLineInstance
{
	float3 A;
	uint ColorA;
	float3 B;
	uint ColorB;
};

PackedLineInstance PackLineInstance(LineInstance l)
{
	PackedLineInstance p;
	p.A = l.A;
	p.ColorA = Pack_RGBA8_UNORM(l.ColorA) & 0xFFFFFFFE;
	p.ColorA |= l.ScreenSpace ? 0x1 : 0x0;
	p.B = l.B;
	p.ColorB = Pack_RGBA8_UNORM(l.ColorB);
	return p;
}

LineInstance UnpackLineInstance(PackedLineInstance p)
{
	LineInstance l;
	l.A = p.A;
	l.ColorA = Unpack_RGBA8_UNORM(p.ColorA);
	l.B = p.B;
	l.ColorB = Unpack_RGBA8_UNORM(p.ColorB);
	l.ScreenSpace = (p.ColorA & 0x1) > 0;
	return l;
}

static const uint MAX_NUM_COUNTERS = 4;
static const uint MAX_NUM_TEXT = 8192;
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

namespace Private
{
	RWByteAddressBuffer GetRenderData()
	{
		RWByteAddressBuffer renderData = ResourceDescriptorHeap[cView.DebugRenderDataIndex];
		return renderData;
	}

	bool ReserveLines(uint numLines, out uint offset)
	{
		GetRenderData().InterlockedAdd(LINE_COUNTER_OFFSET, numLines, offset);
		return offset + numLines < MAX_NUM_LINES;
	}

	bool ReserveCharacters(uint numCharacters, out uint offset)
	{
		GetRenderData().InterlockedAdd(TEXT_COUNTER_OFFSET, numCharacters, offset);
		return offset + numCharacters < MAX_NUM_TEXT;
	}

	void AddLine(float3 a, float3 b, float4 colorA, float4 colorB, bool screenspace, uint offset)
	{
		LineInstance instance;
		instance.A = a;
		instance.B = b;
		instance.ColorA = colorA;
		instance.ColorB = colorB;
		instance.ScreenSpace = screenspace;
		GetRenderData().Store(LINE_INSTANCES_OFFSET + offset * sizeof(PackedLineInstance), PackLineInstance(instance));
	}

	void AddCharacter(uint character, float2 position, float4 color, uint offset)
	{
		CharacterInstance instance;
		instance.Position = position;
		instance.Character = character;
		instance.Color = color;
		GetRenderData().Store(TEXT_INSTANCES_OFFSET + offset * sizeof(PackedCharacterInstance), PackCharacterInstance(instance));
	}
}

void DrawLine(float3 a, float3 b, float4 color = float4(1, 1, 1, 1))
{
	uint offset;
	if(Private::ReserveLines(1, offset))
	{
		Private::AddLine(a, b, color, color, false, offset);
	}
}

void DrawAxisBase(float3 position, float3x3 rotation, float axisLength = 0.25f, float4 color = float4(1, 1, 1, 1))
{
	uint offset;
	if(Private::ReserveLines(3, offset))
	{
		Private::AddLine(position, position + axisLength * normalize(mul(float3(1, 0, 0), rotation)), float4(1, 0, 0, 1), float4(1, 0, 0, 1), false, offset++);
		Private::AddLine(position, position + axisLength * normalize(mul(float3(0, 1, 0), rotation)), float4(0, 1, 0, 1), float4(0, 1, 0, 1), false, offset++);
		Private::AddLine(position, position + axisLength * normalize(mul(float3(0, 0, 1), rotation)), float4(0, 0, 1, 1), float4(0, 0, 1, 1), false, offset++);
	}
}

void DrawAABB(float3 center, float3 extents, float4 color = float4(1, 1, 1, 1))
{
	uint offset;
	if(Private::ReserveLines(12, offset))
	{
		const float3 p0 = center + float3(-1, -1, -1) * extents;
		const float3 p1 = center + float3(1, -1, -1) * extents;
		const float3 p2 = center + float3(-1, 1, -1) * extents;
		const float3 p3 = center + float3(-1, -1, 1) * extents;
		const float3 p4 = center + float3(1, 1, 1) * extents;
		const float3 p5 = center + float3(1, 1, -1) * extents;
		const float3 p6 = center + float3(-1, 1, 1) * extents;
		const float3 p7 = center + float3(1, -1, 1) * extents;

		Private::AddLine(p0, p1, color, color, false, offset++);
		Private::AddLine(p0, p2, color, color, false, offset++);
		Private::AddLine(p0, p3, color, color, false, offset++);
		Private::AddLine(p4, p5, color, color, false, offset++);
		Private::AddLine(p4, p6, color, color, false, offset++);
		Private::AddLine(p4, p7, color, color, false, offset++);
		Private::AddLine(p6, p3, color, color, false, offset++);
		Private::AddLine(p5, p1, color, color, false, offset++);
		Private::AddLine(p7, p3, color, color, false, offset++);
		Private::AddLine(p1, p7, color, color, false, offset++);
		Private::AddLine(p2, p5, color, color, false, offset++);
		Private::AddLine(p2, p6, color, color, false, offset++);
	}
}

void DrawOBB(float3 center, float3 extents, float4x4 world, float4 color = float4(1, 1, 1, 1))
{
	uint offset;
	if(Private::ReserveLines(12, offset))
	{
		const float3 p0 = mul(float4(center + float3(-1, -1, -1) * extents, 1), world).xyz;
		const float3 p1 = mul(float4(center + float3(1, -1, -1) * extents, 1), world).xyz;
		const float3 p2 = mul(float4(center + float3(-1, 1, -1) * extents, 1), world).xyz;
		const float3 p3 = mul(float4(center + float3(-1, -1, 1) * extents, 1), world).xyz;
		const float3 p4 = mul(float4(center + float3(1, 1, 1) * extents, 1), world).xyz;
		const float3 p5 = mul(float4(center + float3(1, 1, -1) * extents, 1), world).xyz;
		const float3 p6 = mul(float4(center + float3(-1, 1, 1) * extents, 1), world).xyz;
		const float3 p7 = mul(float4(center + float3(1, -1, 1) * extents, 1), world).xyz;

		Private::AddLine(p0, p1, color, color, false, offset++);
		Private::AddLine(p0, p2, color, color, false, offset++);
		Private::AddLine(p0, p3, color, color, false, offset++);
		Private::AddLine(p4, p5, color, color, false, offset++);
		Private::AddLine(p4, p6, color, color, false, offset++);
		Private::AddLine(p4, p7, color, color, false, offset++);
		Private::AddLine(p6, p3, color, color, false, offset++);
		Private::AddLine(p5, p1, color, color, false, offset++);
		Private::AddLine(p7, p3, color, color, false, offset++);
		Private::AddLine(p1, p7, color, color, false, offset++);
		Private::AddLine(p2, p5, color, color, false, offset++);
		Private::AddLine(p2, p6, color, color, false, offset++);
	}
}

void DrawScreenLine(float2 a, float2 b, float4 color = float4(1, 1, 1, 1))
{
	uint offset;
	if(Private::ReserveLines(1, offset))
	{
		Private::AddLine(float3(a, 0), float3(b, 0), color, color, true, offset++);
	}
}

enum class RectMode
{
	MinMax,
	CenterExtents,
};

void DrawRect(float2 a, float2 b, RectMode mode = RectMode::MinMax, float4 color = float4(1, 1, 1, 1))
{
	uint offset;
	if(Private::ReserveLines(4, offset))
	{
		if(mode == RectMode::CenterExtents)
		{
			float2 minP = a - b;
			float2 maxP = a + b;
			a = minP;
			b = maxP;
		}

		Private::AddLine(float3(a.x, a.y, 0), float3(b.x, a.y, 0), color, color, true, offset++);
		Private::AddLine(float3(a.x, a.y, 0), float3(a.x, b.y, 0), color, color, true, offset++);
		Private::AddLine(float3(b.x, a.y, 0), float3(b.x, b.y, 0), color, color, true, offset++);
		Private::AddLine(float3(a.x, b.y, 0), float3(b.x, b.y, 0), color, color, true, offset++);
	}
}

struct TextWriter
{
	float2 StartLocation;
	float2 Cursor;
	float4 Color;

	void SetColor(float4 color)
	{
		Color = color;
	}

	void Text_(uint character, uint offset)
	{
		StructuredBuffer<Glyph> glyphBuffer = ResourceDescriptorHeap[cView.FontDataIndex];
		Glyph glyph = glyphBuffer[character];

		float2 position = Cursor + int2(-glyph.Offset.x, glyph.Offset.y);
		Cursor.x += glyph.Width;

		Private::AddCharacter(character, position, Color, offset);
	}

	void Text(uint character)
	{
		uint offset;
		if(Private::ReserveCharacters(1, offset))
		{
			Text_(character, offset);
		}
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

	void Int(int value, bool seperators = false)
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
			if(seperators && length > 0 && length % 3 == 0)
				Text(',');

			value = value - digit * divider;
			divider /= 10;
		}
	}

	void Int(int2 value)
	{
		Int(value.x);
		Text(',', ' ');
		Int(value.y);
	}

	void Int(int3 value)
	{
		Int(value.xy);
		Text(',', ' ');
		Int(value.z);
	}

	void Int(int4 value)
	{
		Int(value.xyz);
		Text(',', ' ');
		Int(value.w);
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
			float v = abs(value);
			Int(sign(value) * floor(v));
			Text('.');
			int v1 = floor(frac(v) * 10000);
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
		Float(value.xy);
		Text(',', ' ');
		Float(value.z);
	}

	void Float(float4 value)
	{
		Float(value.xyz);
		Text(',', ' ');
		Float(value.w);
	}

	TextWriter This()
	{
		TextWriter writer;
		writer.StartLocation = StartLocation;
		writer.Cursor = Cursor;
		writer.Color = Color;
		return writer;
	}

	TextWriter operator+(uint character)
	{
		Text(character);
		return This();
	}

	TextWriter operator+(float value)
	{
		Float(value);
		return This();
	}
};

TextWriter CreateTextWriter(float2 position, float4 color = float4(1, 1, 1, 1))
{
	TextWriter writer;
	writer.StartLocation = position;
	writer.Cursor = position;
	writer.Color = color;
	return writer;
}
