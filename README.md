# TextureTaffy

A utility to create compressed textures, in BC1 (DXT1), BC3 (DXT5), BC4, BC5, BC6(U)H and BC7 compression formats, with the [KTX File Format Version 2.0](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html) (KTX2) file format.

## Requirements

* [The Meson build system](https://mesonbuild.com/)
* [Intel® Implicit SPMD Program Compiler](https://ispc.github.io/)

## Building

```
meson setup build [--buildtype=release]
meson compile -C build
```


## Notes and limitations
* Uses stb_image for image loading. Therefore only supports radiance HDR images, not OpenEXR.
* KTX2 writer probably isn't the most robust, but it works for what I need in my pipeline.
* Tested with LDR and HDR single images and cubemaps. May work with 3D textures and arrays, but not tested.
* Only supports BC1, BC3, BC4, BC5, BC6H and BC7 compression formats, ETC and ASTC are implemented by ispc_texcomp,
but I haven't had a need for them yet. Pull requests welcome!
* Based on my testing, the SSE4 code path is faster than AVX2 on modern AMD processors, but AVX2 is faster on intel.
from what I've read, AMD implements AVX2 in slow microcode. ISPC doesn't allow easily selecting the instruction set at runtime,
so I've added a little hack to detect AMD and use SSE4 in that case. Probably doesn't work on MSVC, that's likely to be the next thing I'll look at.
* Only supports x86/x64 processors. In theory ispc_texcomp has support for ARM NEON extensions for example, but I haven't had time to look at that.
