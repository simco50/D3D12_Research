# D3D12/Windows Toy Renderer

This is a personal toy renderer meant as a playground for experimenting with ideas and rendering techniques.

## Requirements

- Visual Studio 2019
- Windows SDK 10.0.19041.0
- Optionally DXR compatible GPU

## Building

- Run `scripts/Generate_VS2019_Windows.bat` to generate VS project files.
- Open `D3D12.sln` and Compile/Run

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

