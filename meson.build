project('pokeplatinum', ['c', 'cpp', 'nasm'],
    version: '1.0',
    meson_version: '>=1.12.0',
    default_options : [
        'buildtype=plain',
        'warning_level=0'
    ]
)

fs = import('fs')
build_target = get_option('build_target')

############################################################
###                    INCLUDE PATHS                     ###
############################################################
public_includes = include_directories('include', 'asm')
toplevel_includes = include_directories('.')


############################################################
###                    COMPILER FLAGS                    ###
############################################################
shared_args = ['-DPOKEPLATINUM_GENERATED_ENUM', '-g']

# The PC builds follow retail revision 1
shared_args += '-DPOKEPLATINUM_REVISION=1'

if (build_target == 'win64')
	shared_args += [
		'-w'
	]
endif

if get_option('tracy_enable')
    shared_args += '-DTRACY_ENABLE'
endif


add_global_arguments(shared_args,
    language: 'c',
    native: true
)

add_global_arguments(shared_args,
    language: 'cpp',
    native: true
)

# Global so the SDK subprojects are instrumented too; -O1 keeps the game playable under ASan. The build tools get it
# as well: UBSan makes GCC report bogus null format strings, which tools built with -Werror would fail on, and
# `make linux-asan` turns off leak checks so the tools' exit-time leaks don't fail the build.
if get_option('sanitize')
    if build_target != 'linux'
        error('sanitize is only set up for the Linux PC target')
    endif
    # Not shift: the DS code shifts negative values everywhere, which GCC defines
    sanitize_args = ['-fsanitize=address,undefined', '-fno-sanitize=shift', '-fno-omit-frame-pointer', '-O1',
        '-Wno-error=format-overflow']
    add_global_arguments(sanitize_args, language: ['c', 'cpp'], native: true)
    add_global_link_arguments('-fsanitize=address,undefined', language: ['c', 'cpp'], native: true)
endif

pokeplatinum_args = [
    '-DPM_KEEP_ASSERTS',
    '-DGAME_VERSION=VERSION_PLATINUM',
    '-DGAME_LANGUAGE=LANGUAGE_ENGLISH'
]

if get_option('autoplay_tests')
    if build_target != 'linux'
        error('autoplay_tests currently requires the Linux PC target')
    endif
    pokeplatinum_args += '-DPLATINUM_AUTOPLAY_TEST'
    test('Autoplay controller', find_program('python3', native: true),
        args: ['-m', 'unittest', 'discover',
               '-s', meson.project_source_root() / 'tools/autoplay', '-p', 'test_*.py'],
        suite: 'autoplay')
endif

pokeplatinum_args += [
    '-std=c99',
    # Autosave uses monotonic time and transactional filesystem operations.
    '-D_POSIX_C_SOURCE=200809L'
]

if get_option('gdb_debugging')
    pokeplatinum_args += '-DGDB_DEBUGGING'
endif

if get_option('logging_enabled')
    pokeplatinum_args += [
        '-DLOGGING_ENABLED',
    ]
endif


link_args = []

############################################################
###                     DEPENDENCIES                     ###
############################################################
libntr_prj = subproject('libntr',
    default_options: [
        'card_game_code=CPUE',
        'card_maker_code=01'
    ])

nitrosdk_dep = dependency('NitroSDK',
    default_options: {
        'mwrap_sdk': '2.0/sp1p2',
        'sp1p3_conditionals': true,
    }
)

nitrosystem_dep = dependency('NitroSystem',
    default_options: {
        'mwrap_sdk': '2.0/sp2',
    }
)

nitrowifi_dep = dependency('NitroWiFi',
    default_options: {
        'mwrap_sdk': '2.0/sp1p2',
        'sp1p3_conditionals': true,
    }
)

nitrodwc_dep = dependency('NitroDWC',
    default_options: {
        'mwrap_sdk': '2.0/sp2p2',
        'link_ppwlobby': true,
    }
)

libvct_dep = dependency('libvct')

# The SDK dependencies pass -w, which no later -W flag can undo. Port code takes their flags without it and includes
# the SDK headers as system headers, so warnings show for our own files only.
port_sdk_deps = []
port_sdk_args = []
foreach arg : (subproject('libntr').get_variable('nitrosdk_args')
        + subproject('libntrsystem').get_variable('nitrosystem_args')
        + subproject('libntrwifi').get_variable('nitrowifi_args')
        + subproject('libntrdwc').get_variable('nitrodwc_args'))
    if arg != '-w' and arg not in port_sdk_args
        port_sdk_args += arg
    endif
endforeach
port_sdk_deps += declare_dependency(compile_args: port_sdk_args)
foreach dep : [nitrosdk_dep, nitrosystem_dep, nitrowifi_dep, nitrodwc_dep]
    port_sdk_deps += dep.partial_dependency(includes: true, sources: true).as_system()
endforeach
port_warning_args = ['-Wall']
if get_option('port_werror')
    port_warning_args += '-Werror'
endif

############################################################
###                       CONSTS                         ###
############################################################
subdir('generated')


############################################################
###                        TOOLS                         ###
############################################################
subdir('tools')


############################################################
###                      SPEC FILES                      ###
############################################################
subdir('platinum.us')


############################################################
###                  INTERNAL LIBRARIES                  ###
############################################################
subdir('lib')


############################################################
###                     FILE SYSTEM                      ###
############################################################
subdir('res')

############################################################
###                     ARM9 BINARY                      ###
############################################################
subdir('src')

trainer_ai_ss = custom_target(
    'trainer_ai_script.ss',
    output: 'trainer_ai_script.ss',
    input: files('src/battle/trainer_ai/script.s'),
    command: [
        'gcc',
        '-D__ASSEMBLER__',
        '-I', meson.global_source_root() / 'asm',
        '-I', meson.global_build_root(),
        '-I', meson.global_source_root() / 'include',
        '-MD', '-MF', '@DEPFILE@',
        '-E', '-nostdinc', '-'
    ],
    depfile: '@BASENAME@.d',
    capture: true,
    feed: true
)

trainer_ai_o = custom_target(
    'trainer_ai_script.o',
    output: 'trainer_ai_script.o',
    input: trainer_ai_ss,
    command: [
        'as',
        '-I', meson.global_source_root() / 'asm',
        '-I', meson.global_build_root(),
        '-o', '@OUTPUT@',
        '@INPUT@',
    ],
    depfile: '@BASENAME@.d'
)

# Port code (src/port, the bwbattle adapter) with warnings on; linked into main as plain objects
libport = static_library('port',
    sources: [
        port_c,
        port_generated,
        c_consts_generators,
        naix_headers,
        h_headers,
        embed_headers,
    ],
    c_args: [
        pokeplatinum_args,
        port_warning_args,
    ],
    cpp_args: port_warning_args,
    c_pch: 'include/pch/global_pch.h',
    include_directories: [
        public_includes,
        libcrypto_public_includes,
        libgds_public_includes,
        libspl_public_includes,
        ppwlobby_public_includes,
    ],
    dependencies: port_sdk_deps,
    native: true
)

if (build_target == 'linux')
main = executable('main',
    sources: [
        pokeplatinum_c,
        c_consts_generators,
        naix_headers,
        h_headers,
        embed_headers,
    ],
    objects: [trainer_ai_o, libport.extract_all_objects(recursive: false)],
    c_args: [
        pokeplatinum_args,
    ],
    c_pch: 'include/pch/global_pch.h',
    include_directories: [
        public_includes,
        libcrypto_public_includes,
        libgds_public_includes,
        libspl_public_includes,
        ppwlobby_public_includes,
    ],
    dependencies: [
        nitrosdk_dep,
        nitrosystem_dep,
        nitrowifi_dep,
        nitrodwc_dep,
        libvct_dep,
    ],
    link_with: [
        libspl,
        libbwbattle,
        libcrypto_ov97,
        libcrypto_static,
        libgds,
        libdwcenc,
        libdwcppwlobby,
        libdwcilobby,
    ],
    link_args: [
        link_args,
    ],
    native: true
)
endif
if (build_target == 'win64')
ws2_32_lib = meson.get_compiler('c').find_library('ws2_32')

ws2_32_dep = declare_dependency(dependencies: [ws2_32_lib])

main = executable('main',
    sources: [
        pokeplatinum_c,
        c_consts_generators,
        naix_headers,
        h_headers,
        embed_headers,
    ],
    objects: [trainer_ai_o, libport.extract_all_objects(recursive: false)],
    c_args: [
        pokeplatinum_args,
    ],
    c_pch: 'include/pch/global_pch.h',
    include_directories: [
        public_includes,
        libcrypto_public_includes,
        libgds_public_includes,
        libspl_public_includes,
        ppwlobby_public_includes,
    ],
    dependencies: [
        nitrosdk_dep,
        nitrosystem_dep,
        nitrowifi_dep,
        nitrodwc_dep,
        libvct_dep,
        ws2_32_dep,
    ],
    link_with: [
        libspl,
        libbwbattle,
        libcrypto_ov97,
        libcrypto_static,
        libgds,
        libdwcenc,
        libdwcppwlobby,
        libdwcilobby,
    ],
    link_args: [
        link_args,
    ],
    native: true
)
endif

pc_copy_options = []
if get_option('skip_anim_scripts')
    pc_copy_options += ['--skip-anim-scripts']
endif


############################################################
###                     PC FILE COPY                     ###
############################################################
# Copy files into 'pc' folder
copy_files_script = files('tools/scripts/copy_pc_files.sh')

pokeplatinum_nds = custom_target('pokeplatinum.us',
    output: 'pokeplatinum.us',
    input: [
        main,
        nitrofs_files,
    ],
    command : [
        copy_files_script,
        meson.global_build_root(),
        filesys_csv,
        'pokeplatinum',
        pc_copy_options,
    ],
    build_by_default: true
)


############################################################
###                       POSTCONF                       ###
############################################################
meson.add_postconf_script(postconf_py)
