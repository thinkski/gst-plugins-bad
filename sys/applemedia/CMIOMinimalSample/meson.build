cmio_plugin_sources = [
    'Device.mm',
    'ObjectStore.mm',
    'PlugIn.mm',
    'PlugInInterface.mm',
    'PlugInMain.mm',
    'Stream.mm',
]

coremediaio_dep = dependency('CoreMediaIO', required : applemedia_option)
if coremediaio_dep.found()
    applemedia_frameworks += [coremediaio_dep]
    cmio_plugin = executable('CMIOGStreamer',
        cmio_plugin_sources,
        dependencies : applemedia_frameworks,
        install : true,
        install_dir : coremediaio_plugins_install_dir,
	)
endif
