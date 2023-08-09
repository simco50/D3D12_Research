# D3D12 Toy Renderer

This is a personal toy renderer meant as a playground for experimenting with ideas and rendering techniques.

## Requirements

- Visual Studio 2022
- Windows SDK 10.0.19041.0
- DXR compatible GPU with Resource Heap Tier 3 support

## Building

- Run `scripts/Generate_VS2022_Windows.bat` to generate VS project files.
- Open `D3D12.sln` and Compile/Run

## Images

### Visibility Buffer - Deferred Texturing

| 2 Phase Occlusion Culling | Visibility Buffer Deferred Texturing |
|---|---|
| ![](Images/OcclusionCulling.jpg) | ![](Images/VisibilityBuffer.jpg) |


GPU Driven rendering with mesh shaders using the 2 Phase Occlusion algorithm to a visibility (ID) buffer. 

### Dynamic Diffuse Global Illumination (DDGI)

| DDGI  | Disabled  |  Path Traced |
|---|---|---|
|  ![](Images/1_DDGI.jpg) | ![](Images/1_NoDDGI.jpg) | ![](Images/1_PathTraced.jpg) |
|  ![](Images/6_DDGI.jpg) | ![](Images/6_NoDDGI.jpg) | ![](Images/6_PathTraced.jpg) |
|  ![](Images/4_DDGI.jpg) | ![](Images/4_NoDDGI.jpg) | ![](Images/4_PathTraced.jpg) |
|  ![](Images/5_DDGI.jpg) | ![](Images/5_NoDDGI.jpg) | ![](Images/5_PathTraced.jpg) |

### Render Graph

| Resource re-use | Graph visualization |
|---|---|
|  ![](Images/RenderGraphTracking.jpg) | ![](Images/RenderGraphDump.jpg)  |


### CPU/GPU Profiler

![](Images/Profiler.jpg)

### Tiled and Clustered Light Culling

| View  | Clustered Buckets  |  Tiled Buckets |
|---|---|---|
|  ![](Images/LightCulling_02.jpg) | ![](Images/LightCulling_01.jpg) | ![](Images/LightCulling_03.jpg) |

### Volumetric Fog

| Enabled  |  Disabled  |
|---|---|
|  ![](Images/VolumetricFog_01.jpg) | ![](Images/VolumetricFog_02.jpg) |

### Reference path tracer

|   |    ||
|---|---|---|
|  ![](Images/PathTracer_01.jpg) | ![](Images/PathTracer_02.jpg) |![](Images/PathTracer_03.jpg) |

### Concurrent Binary Tree - GPU driven runtime subdivision

|   |  |
|---|---|
|  ![](Images/CBT_01.jpg) | ![](Images/CBT_02.jpg) |

### Volumetric Clouds

|  |  |
|---|---|
|  ![](Images/Clouds_01.jpg) | ![](Images/Clouds_02.jpg) |

### Cascaded Shadow Maps

| View  | Color Coded Cascades |
|---|---|
|  ![](Images/CSM_02.jpg) | ![](Images/CSM_01.jpg) |

### Auto exposure

| View  |
|---|
|  ![](Images/AutoExposure.jpg) |

## Some noteable features

- GPU driven rendering
- 2 phase occlusion culling
- Render graph
- Deferred texturing
- Path tracing mode
- Tiled light culling (Forward+)
- Clustered light culling (Clustered Forward+)
- Cascaded shadow maps
- Volumetric lighting
- Bindless resources, no input layouts
- Compute particles
- Dynamic eye adaptation
- Screen space reflections (wip)
- Raytraced reflections (wip)
- Screen space ambient occlusion
- Raytraced ambient occlusion (wip)
- Temporal anti-aliasing
- Microfacet BRDF
- Shader hot-reloading
