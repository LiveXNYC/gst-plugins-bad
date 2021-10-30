ndi_sources = [
  'gstndi.c',
  'gstndifinder.c',
  'gstndidevice.c',
  'gstndiinput.c',
  'gstndioutput.c',
  'gstndideviceprovider.c',
  'gstndiutil.c',
  'gstndivideosrc.c',
  'gstndiaudiosrc.c',
  'gstndivideosink.c',
  'gstndiaudiosink.c',
]

ndi_ldflags = []
ndi_libs = []

if host_system == 'windows'
  if cc.get_id() == 'msvc'
#    ndi_lib = cc.find_library('Processing.NDI.Lib.x64.lib', required : true)
#    ndi_lib = declare_dependency(link_args : ['-L./win', '-lProcessing.NDI.Lib.x64'])
    ndi_dep = dependency('ndi', version : '>=1.3.0', required : true)
    ndi_libs = [ndi_dep]
  endif
endif
if ['darwin', 'ios'].contains(host_system)
    ndi_dep = dependency('ndi', version : '>=1.3.0', required : true)
    ndi_libs = [ndi_dep]
endif
message('Building ndi plugin')
gstndi = library('gstndi',
    ndi_sources,
    cpp_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
    link_args : ndi_ldflags + noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstaudio_dep, gstbase_dep, gst_dep] + ndi_libs,
    install : true,
    install_dir : plugins_install_dir,
)
pkgconfig.generate(gstndi, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstndi]
