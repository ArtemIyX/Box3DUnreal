# Vendored box3d

| | |
| --- | --- |
| Upstream | https://github.com/erincatto/box3d |
| Version | **v0.1.0** |
| License | MIT (`Source/Box3DCore/box3d.LICENSE`) |
| Local modifications | **None** - upstream files are byte-identical |

## Layout mapping

| Upstream | Vendored |
| --- | --- |
| `include/box3d/*.h` | `Source/Box3DCore/Public/box3d/*.h` |
| `src/*.c`, `src/*.h` | `Source/Box3DCore/Private/*` |
| `LICENSE` | `Source/Box3DCore/box3d.LICENSE` |

Everything else (CMake, samples, docs, tests) is intentionally not vendored -</br>
UnrealBuildTool compiles the sources directly (`Box3DCore.Build.cs`: C17, no </br>
PCH/unity, exports via box3d's own `box3d_EXPORTS`/`BOX3D_DLL` scheme).

