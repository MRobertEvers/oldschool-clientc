@echo off
rem Regenerate 3rd\trspk\d3d9\d3d9_rev241_shaders.h from shaders\rev241.hlsl.
rem Windows only (d3dcompiler_47.dll); MinGW gcc on PATH (toolchains\mingw64\bin).
setlocal
cd /d %~dp0..\..
gcc -O2 -o tools\d3d9_shaderc\d3d9_shaderc.exe tools\d3d9_shaderc\d3d9_shaderc.c || exit /b 1
tools\d3d9_shaderc\d3d9_shaderc.exe 3rd\trspk\d3d9\shaders\rev241.hlsl 3rd\trspk\d3d9\d3d9_rev241_shaders.h ^
    d3d9_rev241_world_vs=world_vs:vs_3_0 ^
    d3d9_rev241_projected_vs=projected_vs:vs_3_0 ^
    d3d9_rev241_world_ps=world_ps:ps_3_0 ^
    d3d9_rev241_projected_ps=projected_ps:ps_3_0
