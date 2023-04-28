sources = files([
  'createdfd.cpp',
  'HalfFloat.cpp',
  'Main.cpp',
  'stb_image_resize.cpp',
  'stb_image.cpp',
  'vk2dfd.cpp'
])

incdirs = include_directories([
])

dependencies = [
  dependency('vulkan')
]

ispc_kernel = custom_target('ipsc_kernel', input: ['ispc_texcomp/kernel.ispc'], output: ['kernel_ispc.o', 'kernel_ispc_avx2.o', 'kernel_ispc_sse4.o', 'kernel_ispc.h'], command: ['ispc', '-O3', '--arch=x86_64', '--target=sse4,avx2', '--opt=fast-math', '--pic', '@INPUT@', '-h', '@OUTDIR@/kernel_ispc.h', '-o', '@OUTPUT0@'])

ispc_sources = [
  ispc_kernel,
  'ispc_texcomp/ispc_texcomp.cpp',
  'ispc_texcomp/ispc_texcomp.h'
]

ispc_texcomp = static_library('ispc_texcomp', ispc_sources)

executable('TextureTaffy', sources, dependencies: dependencies, include_directories: incdirs, install: true, install_dir: '', install_tag: 'exe', link_with: ispc_texcomp) 