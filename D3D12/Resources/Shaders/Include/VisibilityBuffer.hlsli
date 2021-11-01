#include "Common.hlsli"
#include "CommonBindings.hlsli"

struct DerivativesOutput
{
	float3 dbDx;
	float3 dbDy;
};

// Equation from Schied, Dachsbacher
// Computes the partial derivatives of point's barycentric coordinates from the projected screen space vertices
DerivativesOutput ComputePartialDerivatives(float2 v[3])
{
	DerivativesOutput result;
	float d = 1.0 / determinant(float2x2(v[2] - v[1], v[0] - v[1]));
	result.dbDx = float3(v[1].y - v[2].y, v[2].y - v[0].y, v[0].y - v[1].y) * d;
	result.dbDy = float3(v[2].x - v[1].x, v[0].x - v[2].x, v[1].x - v[0].x) * d;

	return result;
}

struct GradientInterpolationResults
{
	float2 Interp;
	float2 Dx;
	float2 Dy;
};

// Interpolate 2D attributes using the partial derivatives and generates dx and dy for texture sampling.
GradientInterpolationResults InterpolateAttributeWithGradient(float2x3 attributes, float3 db_dx, float3 db_dy, float2 d, float2 pTwoOverRes)
{
	float3 attr0 = attributes[0];
	float3 attr1 = attributes[1];
	float2 attribute_x = float2(dot(db_dx, attr0), dot(db_dx, attr1));
	float2 attribute_y = float2(dot(db_dy, attr0), dot(db_dy, attr1));
	float2 attribute_s = float2(attributes[0].x, attributes[1].x);

	GradientInterpolationResults result;
	result.Dx = attribute_x * pTwoOverRes.x;
	result.Dy = attribute_y * pTwoOverRes.y;
	result.Interp = (attribute_s + d.x * attribute_x + d.y * attribute_y);
	return result;
}

float InterpolateAttribute(float3 attributes, float3 db_dx, float3 db_dy, float2 d)
{
	float attribute_x = dot(attributes, db_dx);
	float attribute_y = dot(attributes, db_dy);
	float attribute_s = attributes[0];

	return (attribute_s + d.x * attribute_x + d.y * attribute_y);
}

// Interpolate 2D attributes using the partial derivatives and generates dx and dy for texture sampling.
float2 Interpolate2DAttributes(float2x3 attributes, float3 dbDx, float3 dbDy, float2 d)
{
	float3 attr0 = attributes[0];
	float3 attr1 = attributes[1];
	float2 attribute_x = float2(dot(dbDx,attr0), dot(dbDx,attr1));
	float2 attribute_y = float2(dot(dbDy,attr0), dot(dbDy,attr1));
	float2 attribute_s = float2(attributes[0].x, attributes[1].x);

	float2 result = (attribute_s + d.x * attribute_x + d.y * attribute_y);
	return result;
}

// Interpolate vertex attributes at point 'd' using the partial derivatives
float3 Interpolate3DAttributes(float3x3 attributes, float3 dbDx, float3 dbDy, float2 d)
{
	float3 attribute_x = mul(dbDx, attributes);
	float3 attribute_y = mul(dbDy, attributes);
	float3 attribute_s = attributes[0];

	return (attribute_s + d.x * attribute_x + d.y * attribute_y);
}
