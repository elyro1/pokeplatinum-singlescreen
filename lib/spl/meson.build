libspl_public_includes = include_directories('include')

libspl_srcs = files(
    'src/spl_manager.c',
    'src/spl_emitter.c',
    'src/spl_draw.c',
    'src/spl_emit.c',
    'src/spl_anim.c',
    'src/spl_behavior.c',
    'src/spl_list.c',
    'src/spl_random.c'
)

libspl = static_library('spl',
    sources: libspl_srcs,
    include_directories: [public_includes, libspl_public_includes],
    dependencies: [nitrosdk_dep, nitrosystem_dep],
    pic: true,
    native: true
)
