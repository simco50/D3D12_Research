#ifndef H_CONSTANTS
#define H_CONSTANTS

#ifndef BLOCK_SIZE
#define BLOCK_SIZE 16
#endif

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

#ifndef DEBUG_VISUALIZE
#define DEBUG_VISUALIZE 0
#endif

#ifndef SPLITZ_CULLING
#define SPLITZ_CULLING 1
#endif

#ifndef FORWARD_PLUS
#define FORWARD_PLUS 1
#endif 

#ifndef PCF_KERNEL_SIZE
#define PCF_KERNEL_SIZE 3
#endif

#ifndef SHADOWMAP_DX
#define SHADOWMAP_DX 0.000244140625f
#endif

#ifndef MAX_SHADOW_CASTERS
#define MAX_SHADOW_CASTERS 8
#endif

#define PI 3.14159265359

#endif