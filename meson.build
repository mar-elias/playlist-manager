project('rhythmbox', 'c',
  version: '3.5.1',
  meson_version: '>= 0.64.0',
  default_options: ['c_std=gnu89'])

gnome = import('gnome')

# plugin API versioning
# Increment on interface addition, reset on removal
RHYTHMBOX_CORE_AGE=0
# Increment on interface add, remove, or change
RHYTHMBOX_CORE_CURRENT=10
# Increment on source change, reset when CURRENT changes.
RHYTHMBOX_CORE_REVISION=0

cdata = configuration_data()
cc = meson.get_compiler('c')

common_cflags = [
  '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
]
test_cflags = [
  '-Wno-deprecated-declarations',
  '-Wformat=2',
  '-Wno-format-nonliteral',
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wpointer-arith',
  '-Wcast-align',
  '-Wstrict-prototypes',
]
common_cflags += cc.get_supported_arguments (test_cflags)
add_project_arguments(common_cflags, language: 'c')

libmath = cc.find_library('m')

glib_required = '2.76.0'
cdata.set('GLIB_VERSION_MIN_REQUIRED', 'GLIB_VERSION_2_76')
cdata.set('GLIB_VERSION_MAX_ALLOWED', 'GLIB_VERSION_2_76')

gstreamer_required = '1.4.0'

intl = dependency('intl', required: true)
cairo = dependency('cairo', required: true)
gdk_pixbuf = dependency('gdk-pixbuf-2.0', version: '>= 2.18.0')
gio = dependency('gio-2.0', version: '>=' + glib_required, required: true)
gio_unix = dependency('gio-unix-2.0', version: '>=' + glib_required, required: true)
glib = dependency('glib-2.0', version: '>=' + glib_required, required: true)
gobject = dependency('gobject-2.0', version: '>=' + glib_required, required: true)
gstreamer = dependency('gstreamer-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_audio = dependency('gstreamer-audio-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_base = dependency('gstreamer-base-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_controller = dependency('gstreamer-controller-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_plugins_base = dependency('gstreamer-plugins-base-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_pbutils = dependency('gstreamer-pbutils-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_tag = dependency('gstreamer-tag-1.0', version: '>= ' + gstreamer_required, required: true)
gtk = dependency('gtk+-3.0', version: '>= 3.16.0', required: true)
libxml = dependency('libxml-2.0', version: '>= 2.7.8', required: true)
pango = dependency('pango', required: true)
tdb = dependency('tdb', version: '>= 1.2.6', required: true)
totem_plparser = dependency('totem-plparser', version: '>= 3.2.0', required: true)

common_deps = [
  libmath,
  intl, glib, gobject, gio, gio_unix,
  cairo, pango, gdk_pixbuf, gtk,
  gstreamer, gstreamer_audio, gstreamer_base, gstreamer_controller, gstreamer_plugins_base, gstreamer_pbutils, gstreamer_tag,
  libxml, tdb, totem_plparser,
]

check = dependency('check', required: get_option('tests'))


have_gnu_fwrite_unlocked = false
if cc.has_function('fwrite_unlocked')
  have_gnu_fwrite_unlocked = cc.has_header_symbol('stdio.h', 'fwrite_unlocked')
endif
cdata.set('HAVE_GNU_FWRITE_UNLOCKED', have_gnu_fwrite_unlocked)

gtk_targets = gtk.get_variable(pkgconfig: 'targets').split()
if gtk_targets.contains('x11')
  common_deps += dependency('x11', required: true)
endif

have_prctl = cc.has_function('prctl', prefix: '#include <sys/prctl.h>')
cdata.set('HAVE_PRCTL', have_prctl)

have_pthread_getname_np = cc.links('''
  #define _GNU_SOURCE
  #include <pthread.h>
  int main() {
    char nm[17];
    pthread_getname_np(pthread_self(), nm, sizeof(nm));
  }''', name: 'pthread_getname_np')
cdata.set('HAVE_PTHREAD_GETNAME_NP', have_pthread_getname_np)

cdata.set('GETTEXT_PACKAGE', '"rhythmbox"')
cdata.set('PACKAGE', '"rhythmbox"')
cdata.set('VERSION', '@0@'.format(meson.project_version()))

# Needed so that we can build static libraries and later link them into
# shared libraries.
add_global_arguments('-fPIC', language: 'c')

use_no_strict_aliasing = cc.has_argument('-fno-strict-aliasing')

datadir = get_option('prefix') / get_option('datadir')
libdir = get_option('prefix') / get_option('libdir')
pkgdatadir = datadir / 'rhythmbox'

gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: true
)

configure_file(input : 'config.h.meson',
  output : 'config.h',
configuration : cdata)

summary({'Tests': check.found(),
        },
        section: 'General',
        bool_yn: true)
configinc = include_directories('.')
libinc = include_directories('lib')
metadatainc = include_directories('metadata')
rhythmdbinc = include_directories('rhythmdb')
backendsinc = include_directories('backends', 'backends/gstreamer')
shellinc = include_directories('shell')
sourcesinc = include_directories('sources')
widgetsinc = include_directories('widgets')

subdir('data')
subdir('data/icons')

subdir('lib')
subdir('metadata')
subdir('rhythmdb')
subdir('backends')
subdir('sources')
subdir('widgets')
subdir('shell')

if check.found()
  subdir('tests')
endif


