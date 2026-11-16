libgds_srcs = files(
    'src/ov61_0222CEDC.c',
    'src/ov61_0222D498.c',
    'src/ov61_0222DCDC.c',
    'src/ov61_0222E0B8.c',
    'src/ov61_0222E478.c'
)

libgds_public_includes = include_directories('include')

libgds = static_library('gds',
    sources: libgds_srcs,
    include_directories: [public_includes, libgds_public_includes],
    dependencies: [nitrosdk_dep, nitrosystem_dep, nitrowifi_dep, nitrodwc_dep],
    pic: true,
    native: (build_target == 'linux' or build_target == 'win64')
)
