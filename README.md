# D3D12/Windows Toy Renderer

This is a personal toy renderer meant as a playground for experimenting with ideas and rendering techniques.

## Requirements

- Visual Studio 2019
- Windows SDK 10.0.19041.0
- Optionally DXR compatible GPU

## Building

- Run `scripts/Generate_VS2019_Windows.bat` to generate VS project files.
- Open `D3D12.sln` and Compile/Run

## Images

### Dynamic Diffuse Global Illumination (DDGI)

| DDGI  | Disabled  |  Path Traced |
|---|---|---|
|  ![](Images/1_DDGI.jpg) | ![](Images/1_NoDDGI.jpg) | ![](Images/1_PathTraced.jpg) |
|  ![](Images/6_DDGI.jpg) | ![](Images/6_NoDDGI.jpg) | ![](Images/6_PathTraced.jpg) |
|  ![](Images/4_DDGI.jpg) | ![](Images/4_NoDDGI.jpg) | ![](Images/4_PathTraced.jpg) |
|  ![](Images/5_DDGI.jpg) | ![](Images/5_NoDDGI.jpg) | ![](Images/5_PathTraced.jpg) |

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


### Cascaded Shadow Maps

| View  | Color Coded Cascades |
|---|---|
|  ![](Images/CSM_02.jpg) | ![](Images/CSM_01.jpg) |

### Auto exposure

| View  |
|---|
|  ![](Images/AutoExposure.jpg) |

## Some noteable features

- Tiled light culling (Forward+)
- Clustered light culling (Clustered Forward+)
- Cascaded shadow maps
- Compute particles
- Dynamic eye adaptation
- Path tracing mode
- Volumetric lighting
- Screen space reflections (wip)
- Raytraced reflections (wip)
- Screen space ambient occlusion
- Raytraced ambient occlusion (wip)
- Bindless resources, no input layouts
- Temporal anti-aliasing
- Microfacet BRDF
- Shader hot-reloading

