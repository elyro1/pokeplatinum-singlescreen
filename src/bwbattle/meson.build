# BW2's battle view (docs/bwbattle.md). These files are BW2 code and see only BW2's headers (include/bwbattle) and the
# SDK's, never Platinum's: the two games have headers of the same names. Platinum's side talks to them through
# include/bwbattle/bwbattle.h.

libbwbattle_includes = include_directories('../../include/bwbattle')

trainer_back_assets = files('../../res/bwbattle/trainers/back_sprites.bin')
trainer_backs_header = custom_target('trainer_backs.inc',
    output: 'trainer_backs.inc',
    input: trainer_back_assets,
    command: [find_program('python3', native: true),
        files('../../tools/scripts/make_bw_trainer_backs.py'), '@INPUT@', '@OUTPUT@'],
)

libbwbattle_srcs = files(
    'assets/bw2_assets.c',
    'assets/iris_assets.c',
    'assets/trainer_backs.c',
    'view/btlv_b_gauge.c',
    'view/btlv_bg.c',
    'view/btlv_bg3d.c',
    'view/btlv_camera.c',
    'view/btlv_clact.c',
    'view/btlv_effect.c',
    'view/btlv_effvm.c',
    'view/btlv_field.c',
    'view/btlv_gauge.c',
    'view/btlv_mcss.c',
    'view/btlv_stage.c',
    'gfl/areaman.c',
    'gfl/arc_tool.c',
    'gfl/arc_util.c',
    'gfl/bg_sys.c',
    'gfl/bmp.c',
    'gfl/calctool.c',
    'gfl/clact.c',
    'gfl/g3d_camera.c',
    'gfl/g3d_system.c',
    'gfl/gf_standard.c',
    'gfl/gx_layers.c',
    'gfl/particle.c',
    'gfl/strbuf.c',
    'gfl/tcb.c',
    'gfl/vman.c',
    'host/bw_view_host.c',
    'host/bw_ability_popup.c',
    'shim/battle_shim.c',
    'shim/heap_shim.c',
    'shim/sound_shim.c',
    'shim/sys_shim.c',
    'shim/text_shim.c',
    'system/hp_gauge.c',
    'system/mcss.c',
    'system/mcss_tool.c',
    'system/palanm.c',
    'system/poke_personal.c',
    'system/pokegra.c',
    'system/sin_wave_table.c',
    'system/tr_sprite.c',
    'system/vm.c',
)

libbwbattle = static_library('bwbattle',
    sources: [libbwbattle_srcs, trainer_backs_header],
    c_args: [
        '-std=gnu99',
        '-DBWBATTLE',
        # pokebw2's fixes for BW2's own bugs (marked BUG in the copies): double frees, reads after frees, writes
        # outside arrays, a loop that never ends
        '-DBUGFIX',
        '-include', meson.current_source_dir() / '../../include/bwbattle/bwbattle_compat.h',
        port_warning_args,
        # NNS file signatures are multi-character constants
        '-Wno-multichar',
    ],
    include_directories: [libbwbattle_includes, libspl_public_includes, include_directories('.')],
    # Only the SDK's headers and flags (without -w): the game links the SDK itself
    dependencies: port_sdk_deps,
    pic: true,
    native: true
)

# Platinum's side of the interface, built with the game's own files
port_c += files(
    'adapter/bw_battle_view.c',
    'adapter/bw_services.c',
)
