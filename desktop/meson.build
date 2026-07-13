project('vaxp-desktop', 'c',
  version : '1.0.0',
  default_options : ['warning_level=2', 'c_std=gnu11'])

cc = meson.get_compiler('c')

gtk_dep = dependency('gtk+-3.0')
gio_dep = dependency('gio-2.0')
layer_shell_dep = dependency('gtk-layer-shell-0')
m_dep = cc.find_library('m', required : false)
dl_dep = cc.find_library('dl', required : false)

deps = [gtk_dep, gio_dep, layer_shell_dep, m_dep, dl_dep]

inc = include_directories('include')

srcs = [
  'src/desktop.c',
  'src/desktop_config.c',
  'src/filesystem.c',
  'src/icons.c',
  'src/menu.c',
  'src/selection.c',
  'src/widgets_manager.c'
]

executable('desktop',
  srcs,
  include_directories : inc,
  dependencies : deps,
  install : true)

# Compile Widgets
widgets = [
  'sidebar',
  'mpris-player',
  'date_widget'
]

foreach w : widgets
  shared_module(w,
    'src/widgets/' + w + '.c',
    name_prefix : '',
    include_directories : inc,
    dependencies : deps,
    install : false)
endforeach

meson.add_install_script('install_widgets.sh')
