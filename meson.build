project('retro-vkquake', 'cpp',
  default_options: ['warning_level=1', 'buildtype=debug'])

sdl3 = dependency('sdl3')

dependencies = [sdl3]
link_args = []

if host_machine.system() == 'darwin'
  link_args += ['-Wl,-undefined,dynamic_lookup']
endif

executable('quake', [
    'src/quake.cpp',
  ],
  dependencies: dependencies,
  link_args: link_args)
