project('fydelisbot-gui', 'c',
  version: '2.0.0',
  default_options: ['c_std=c11', 'warning_level=3', 'b_ndebug=if-release'])

gtk = dependency('gtk4', version: '>=4.10')

executable('fydelisbot-gui',
  sources: ['main.c', 'transport.c'],
  dependencies: gtk,
  install: true)
