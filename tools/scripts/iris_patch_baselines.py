"""Reconstruct retail bases for Iris's appended resources without storing archives."""
import functools
import json
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOUND = 'data/sound/pl_sound_data.sdat'


def build_tool(build, name):
    # Use Meson's actual Windows filename rather than relying on suffix lookup.
    path = build / 'tools' / name / name
    executable = path.with_suffix('.exe')
    return str(executable if executable.is_file() else path)


def remove_script_entry(data, index, count, end=None):
    """Remove an appended entry; offsets are relative to the following word."""
    data = bytearray(data[:end])
    del data[index * 4:index * 4 + 4]
    for i in range(index):
        value = struct.unpack_from('<I', data, i * 4)[0]
        struct.pack_into('<I', data, i * 4, value - 4)
    return bytes(data)


@functools.lru_cache(maxsize=None)
def resources(build, members):
    """Return retail member lists for added/changed NARCs, plus retail SDAT."""
    result = {}
    with tempfile.TemporaryDirectory(prefix='iris-baselines-') as temp:
        work = Path(temp)
        # Re-encode the original portions of changed text banks with their retail keys.
        text = members((build / 'res/text/pl_msg.narc').read_bytes())
        order = [s.removeprefix('TEXT_BANK_').lower() for s in
                 (ROOT / 'generated/text_banks.txt').read_text().splitlines()]
        for name, source, count in [
            ('iron_island', ROOT / 'res/text/iron_island.json', 6),
            ('scratch_off_cards_app', ROOT / 'res/text/scratch_off_cards_app.json', 13),
            ('trainer_class_names', ROOT / 'res/text/trainer_class_names.json', 105),
            ('trainer_class_names_with_articles', ROOT / 'res/text/trainer_class_names_with_articles.json', 105),
            ('npc_trainer_names', build / 'res/trainers/npc_trainer_names.json', 928),
        ]:
            data = json.loads(source.read_text())
            data['messages'] = data['messages'][:count]
            source = work / (name + '.json')
            source.write_text(json.dumps(data))
            output = work / name
            subprocess.run([build_tool(build, 'msgenc'), '-e', '--json', '-c',
                            str(ROOT / 'tools/msgenc/charmap.txt'), str(source), str(output)], check=True)
            text[order.index(name)] = output.read_bytes()
        result['msgdata/pl_msg.narc'] = (build / 'res/text/pl_msg.narc', text)

        archive = build / 'res/field/scripts/scr_seq.narc'
        scripts = members(archive.read_bytes())
        order = (ROOT / 'res/field/scripts/scripts.order').read_text().splitlines()
        index = order.index('scripts_iron_island')
        data = scripts[index]
        iris_start = 16 + struct.unpack_from('<I', data, 12)[0]
        scripts[index] = remove_script_entry(data, 3, 4, iris_start)
        index = order.index('scripts_battles')
        scripts[index] = remove_script_entry(scripts[index], 928, 930)
        result['fielddata/script/scr_seq.narc'] = (archive, scripts)

        archive = build / 'res/field/events/zone_event.narc'
        events = members(archive.read_bytes())
        order = (ROOT / 'res/field/events/zone_event.order').read_text().splitlines()
        index = order.index('events_iron_island')
        data = bytearray(events[index])
        objects = 4 + struct.unpack_from('<I', data)[0] * 20
        assert struct.unpack_from('<I', data, objects)[0] == 3
        struct.pack_into('<I', data, objects, 2)
        del data[objects + 4 + 2 * 32:objects + 4 + 3 * 32]
        events[index] = bytes(data)
        result['fielddata/eventdata/zone_event.narc'] = (archive, events)

        for name, path in [('trdata', 'poketool/trainer/trdata.narc'),
                           ('trpoke', 'poketool/trainer/trpoke.narc')]:
            archive = build / ('res/trainers/' + name + '.narc')
            result[path] = (archive, members(archive.read_bytes())[:928])
        archive = build / 'res/trainers/trtblofs.narc'
        result['poketool/trmsg/trtblofs.narc'] = (archive, [members(archive.read_bytes())[0][:928 * 2]])
        for path, relative, count in [
            ('data/mmodel/mmodel.narc', 'res/graphics/field_sprites/field_sprites.narc', None),
            ('poketool/pokegra/pl_otherpoke.narc', 'res/pokemon/pl_otherpoke.narc', 253),
            ('poketool/trgra/trfgra.narc', 'res/trainers/classes/trfgra.narc', 105 * 5),
        ]:
            archive = build / relative
            files = members(archive.read_bytes())
            result[path] = (archive, files[:count] if count is not None else files[:-1])
        # The original SDAT rebuilds exactly from its original registry and unchanged files.
        registry = json.loads((ROOT / 'res/sound/pl_sound_data.json').read_text())
        assert registry['seqInfo'][-1]['name'] == 'SEQ_BA_IRIS'
        registry['seqInfo'].pop()
        source = work / 'sound.json'
        source.write_text(json.dumps(registry))
        output = work / 'sound.sdat'
        subprocess.run([build_tool(build, 'nitrosfx'),
                        str(build / 'res/sound/pl_sound_data.sdat.p'), str(output), str(source), '-symb'], check=True)
        sound = output.read_bytes()
    return result, sound
