# Reconstruct pristine sources from deltas only inside the build directory.
restore_baseline = files('../../tools/scripts/restore_patch_baseline.py')
baseline_python = find_program('python3', native: true)
baseline_text_files = []
foreach name : fs.read(files('text/original.order')).strip().splitlines()
    baseline_text_files += custom_target('baseline_text_' + name,
        input: [files('../text' / name + '.json'), files('text' / name + '.patch')],
        output: name + '.json',
        command: [baseline_python, restore_baseline, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
        depend_files: restore_baseline,
    )
endforeach
baseline_text_gen = generator(msgenc_exe,
    arguments: ['-e', '--json', '-c', charmap_txt.full_path(), '@INPUT@', '@OUTPUT@'],
    output: '@BASENAME@',
)
baseline_text_narc = custom_target('original_text.narc',
    output: 'original_text.narc',
    input: baseline_text_gen.process(baseline_text_files),
    command: [nitroarc_exe, '--create', '--files-from', files('text/original.order'),
        '--file', '@OUTPUT@', '@PRIVATE_DIR@'],
)

baseline_script_files = []
foreach name : fs.read(files('scripts/original.order')).strip().splitlines()
    baseline_script_files += custom_target('baseline_script_' + name,
        input: [files('../field/scripts' / name + '.s'), files('scripts' / name + '.patch')],
        output: name + '.s',
        command: [baseline_python, restore_baseline, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
        depend_files: restore_baseline,
    )
endforeach
baseline_scripts_narc = custom_target('original_scripts.narc',
    output: 'original_scripts.narc',
    input: script_bin_gen.process(baseline_script_files,
        extra_args: ['--out-dir', fs.relative_to(meson.current_build_dir(), meson.project_build_root()) / 'original_scripts.narc.p'],
        depends: [events_headers, text_banks]),
    command: [nitroarc_exe, '--create', '--files-from', files('scripts/original.order'),
        '--file', '@OUTPUT@', '@PRIVATE_DIR@'],
)

baseline_subscript_files = []
foreach name : fs.read(files('battle_subscripts/original.order')).strip().splitlines()
    baseline_subscript_files += custom_target('baseline_subscript_' + name,
        input: [files('../battle/scripts/subscripts' / name + '.s'), files('battle_subscripts' / name + '.patch')],
        output: name + '.s',
        command: [baseline_python, restore_baseline, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
        depend_files: restore_baseline,
    )
endforeach
baseline_subscripts_narc = custom_target('original_battle_subscripts.narc',
    output: 'original_battle_subscripts.narc',
    input: script_bin_gen.process(baseline_subscript_files,
        extra_args: ['--out-dir', fs.relative_to(meson.current_build_dir(), meson.project_build_root()) / 'original_battle_subscripts.narc.p'],
        depends: [text_banks, pl_sound_data_sdat]),
    command: [nitroarc_exe, '--create', '--files-from', files('battle_subscripts/original.order'),
        '--file', '@OUTPUT@', '@PRIVATE_DIR@'],
)
