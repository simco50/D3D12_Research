#pragma once

static const uint Font[] = 
{
	0x69f99, // A
	0x79797, // B
	0xe111e, // C
	0x79997, // D
	0xf171f, // E
	0xf1711, // F
	0xe1d96, // G
	0x99f99, // H
	0xf444f, // I
	0x88996, // J
	0x95159, // K
	0x1111f, // L
	0x9f999, // M
	0x9bd99, // N
	0x69996, // O
	0x79971, // P
	0x69b5a, // Q
	0x79759, // R
	0xe1687, // S
	0xf4444, // T
	0x99996, // U
	0x999a4, // V
	0x999f9, // W
	0x99699, // X
	0x99e8e, // Y
	0xf8438f,// Z
	
	0xF999F, // 0
	0x46444, // 1
	0xF8F1F, // 2
	0xF8F8F, // 3
	0x99f88, // 4
	0xF1F8F, // 5
	0xF1F9F, // 6
	0xf8421, // 7
	0xF9F9F, // 8
	0xF9F8F, // 9

	0x66400, // APST
	0x0faa9, // PI 
	0x0000f, // UNDS
	0x00600, // HYPH
	0x0a500, // TILD
	0x02720, // PLUS
	0x0f0f0, // EQUL
	0x08421, // SLSH
	0x33303, // EXCL
	0x69404, // QUES
	0x00032, // COMM
	0x00002, // FSTP
	0x55000, // QUOT
	0x00000, // BLNK
	0x00202, // COLN
	0x42224, // LPAR
	0x24442  // RPAR
};

static const uint CH_A 		= 0;
static const uint CH_B 		= 1;
static const uint CH_C 		= 2;
static const uint CH_D 		= 3;
static const uint CH_E 		= 4;
static const uint CH_F 		= 5;
static const uint CH_G 		= 6;
static const uint CH_H 		= 7;
static const uint CH_I 		= 8;
static const uint CH_J 		= 9;
static const uint CH_K 		= 10;
static const uint CH_L 		= 11;
static const uint CH_M 		= 12;
static const uint CH_N 		= 13;
static const uint CH_O 		= 14;
static const uint CH_P 		= 15;
static const uint CH_Q 		= 16;
static const uint CH_R 		= 17;
static const uint CH_S 		= 18;
static const uint CH_T 		= 19;
static const uint CH_U 		= 20;
static const uint CH_V 		= 21;
static const uint CH_W 		= 22;
static const uint CH_X 		= 23;
static const uint CH_Y 		= 24;
static const uint CH_Z 		= 25;
static const uint CH_0 		= 26;
static const uint CH_1 		= 27;
static const uint CH_2 		= 28;
static const uint CH_3 		= 29;
static const uint CH_4 		= 30;
static const uint CH_5 		= 31;
static const uint CH_6 		= 32;
static const uint CH_7 		= 33;
static const uint CH_8 		= 34;
static const uint CH_9 		= 35;
static const uint CH_APST 	= 36;
static const uint CH_PI 	= 37;
static const uint CH_UNDS 	= 38;
static const uint CH_HYPH 	= 39;
static const uint CH_TILD 	= 40;
static const uint CH_PLUS 	= 41;
static const uint CH_EQUL 	= 42;
static const uint CH_SLSH 	= 43;
static const uint CH_EXCL 	= 44;
static const uint CH_QUES 	= 45;
static const uint CH_COMM 	= 46;
static const uint CH_FSTP 	= 47;
static const uint CH_QUOT 	= 48;
static const uint CH_BLNK 	= 49;
static const uint CH_COLN 	= 50;
static const uint CH_LPAR 	= 51;
static const uint CH_RPAR 	= 52;

static const uint2 FontSize = uint2(4, 5);

int DrawChar(uint character, int2 pixelPos, int2 charPos, float scale)
{
	float2 localPos;
	localPos.x = pixelPos.x - charPos.x;
	localPos.y = charPos.y - pixelPos.y;

	localPos += 0.5f * FontSize * scale;

	localPos /= scale;
	int2 pos_i = round(localPos);
	if(any(pos_i < 0) || any(pos_i >= FontSize))
		return 0;
	uint index = FontSize.x * pos_i.y + pos_i.x;
	return (Font[character] >> index) & 1;
}

int DrawInt(int value, int2 pixelPos, inout int2 charPos, float scale)
{
	float s = sign(value);
	value *= s;
	
	int num_digits = int(floor(log10(value))) + 1;
	charPos.x += num_digits * FontSize.x * scale * 0.5f;

	int result = 0;
	for(int i = 0; value > 0 || i == 0; ++i)
	{
		int digit = value % 10;
		result |= DrawChar(CH_0 + digit, pixelPos, charPos, scale);
		charPos.x -= FontSize.x * scale * 1.1f;
		value /= 10;
	}

	if(s < 0.0f)
		result |= DrawChar(CH_HYPH, pixelPos, charPos, scale);

	return result;
}

int DrawFloat(float value, int2 pixelPos, int2 charPos, float scale, uint places)
{
	float val_f, val_i;
	val_f = modf(value, val_i);

	int result = 0;
	result |= DrawInt(round(val_f * places), pixelPos, charPos, scale);
	charPos.x += FontSize.x * scale * 0.5f;
	result |= DrawChar(CH_COMM, pixelPos, charPos, scale);
	charPos.x -= FontSize.x * scale * 1.5f;
	result |= DrawInt(val_i, pixelPos, charPos, scale);

	return result;
}