project('retro-vkquake', 'cpp',
  default_options: ['warning_level=1', 'buildtype=debug'])

sdl3 = dependency('sdl3')
vulkan = dependency('vulkan')

dependencies = [sdl3, vulkan]
link_args = []

if host_machine.system() == 'darwin'
  link_args += ['-Wl,-undefined,dynamic_lookup']
endif

# Compile the GLSL shaders to SPIR-V and embed them as C headers (glslangValidator
# --vn emits an `unsigned int <name>[]` array). The generated headers are listed in
# the executable sources so they are built first and their directory is on the
# include path; quake.cpp includes them directly, so no SPIR-V files are loaded at
# runtime.
glslang = find_program('glslangValidator')

quake_vert_spv = custom_target('quake_vert_spv',
  input: 'src/shaders/quake.vert',
  output: 'quake_vert_spv.h',
  command: [glslang, '-V', '@INPUT@', '--vn', 'quake_vert_spv', '-o', '@OUTPUT@'])

quake_frag_spv = custom_target('quake_frag_spv',
  input: 'src/shaders/quake.frag',
  output: 'quake_frag_spv.h',
  command: [glslang, '-V', '@INPUT@', '--vn', 'quake_frag_spv', '-o', '@OUTPUT@'])

executable('quake', [
    'src/quake.cpp',
    quake_vert_spv,
    quake_frag_spv,
  ],
  dependencies: dependencies,
  link_args: link_args)
