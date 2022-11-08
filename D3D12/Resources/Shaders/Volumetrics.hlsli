#include "Common.hlsli"

float HenyeyGreenstreinPhase(float LoV, float G)
{
	float result = 1.0f - G * G;
	result /= (4.0f * PI * pow(1.0f + G * G - (2.0f * G) * LoV, 1.5f));
	return result;
}
