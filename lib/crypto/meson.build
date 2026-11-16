libcrypto_public_includes = include_directories('include')

libcrypto_static_srcs = files(
    'src/rc4_init.c'
)

libcrypto_ov97_srcs = files(
    'src/sign.c',
    'src/bignum.c',
    'src/util.c'
)

libcrypto_static = static_library('crypto_static',
    sources: libcrypto_static_srcs,
    pic: false,
    include_directories: libcrypto_public_includes,
    dependencies: nitrosdk_dep,
    native: true
)

libcrypto_ov97 = static_library('crypto_ov97',
    sources: libcrypto_ov97_srcs,
    pic: false,
    include_directories: libcrypto_public_includes,
    dependencies: nitrosdk_dep,
    native: true
)
