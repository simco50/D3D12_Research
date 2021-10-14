#define UINT_MAX 0xFFFFFFFFu
#define INT_MAX  0x7FFFFFFF

#define PI          3.14159265359
#define PI_DIV_2    1.57079632679
#define PI_DIV_4    0.78539816339
#define INV_PI      0.31830988618379067154

#define _INLINE_RT (_SM_MAJ >= 6 && _SM_MIN >= 5)

#if _PAYLOAD_QUALIFIERS
#define RAYPAYLOAD [raypayload]
#define RAYQUALIFIER(qualifiers) : qualifiers
#else
#define RAYPAYLOAD
#define RAYQUALIFIER(qualifiers)
#endif