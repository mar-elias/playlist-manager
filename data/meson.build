desktop_files = [ 'org.gnome.Rhythmbox3.desktop' ]
foreach name : desktop_files
  configure_file(
    input: name + '.in',
    output: name,
    copy: true,
    install: true,
    install_dir: datadir / 'applications',
  )
endforeach

install_data('org.gnome.rhythmbox.gschema.xml',
  install_dir: datadir / 'glib-2.0' / 'schemas')

glib_compile_schemas = find_program('glib-compile-schemas', required: true)
gschemas_compiled = custom_target('gschemas.compiled',
  input: 'org.gnome.rhythmbox.gschema.xml',
  output: 'gschemas.compiled',
  command: [glib_compile_schemas, '--targetdir=' + meson.current_build_dir(), meson.current_source_dir()]
)

playlists_xml = configure_file(
  input: 'playlists.xml.in',
  output: 'playlists.xml',
  copy: true,
)

install_data('rhythmbox.gep',
  install_dir: datadir / 'rhythmbox')


mans = [
  'rhythmbox.1',
]

install_man(mans)

metainfo = 'org.gnome.Rhythmbox3.metainfo.xml'
metainfo_file = configure_file(
  input: metainfo + '.in',
  output: metainfo,
  copy: true,
  install: true,
  install_dir: datadir / 'metainfo',
)

# Validate AppStream metadata
appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test(
      'validate-metainfo', appstreamcli,
      args: ['validate', '--no-net', '--explain', meson.current_build_dir() / metainfo]
      )
endif
