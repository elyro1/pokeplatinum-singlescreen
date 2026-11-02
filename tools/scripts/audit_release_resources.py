#!/usr/bin/env python3
"""Check mod history for added game resources and missing patch baselines."""
import argparse
from pathlib import Path
import subprocess

from make_map_patches import ROOT
from restore_patch_baseline import restore

UPSTREAM = 'c08911193bb3f56a85ac72d30cdc985c212cff10'


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def game_resource(path):
    suffix = Path(path).suffix.lower()
    return (suffix in {'.nds', '.narc', '.sdat', '.nsbmd', '.nsbtx', '.ncgr', '.nclr',
                       '.ncer', '.nanr', '.nscr', '.wav', '.sseq', '.sbnk', '.swar'}
            or (path.startswith(('res/', 'platinum.us/'))
                and suffix in {'.s', '.json', '.bin', '.png', '.pal', '.mid', '.key',
                               '.spa', '.nsbta', '.nsbca', '.nsbtp', '.dat', '.sbin'}))


def audit(base, history=True, head='HEAD'):
    errors = []
    upstream = set(git('ls-tree', '-r', '--name-only', base).decode().splitlines())
    current = set(git('ls-files').decode().splitlines())
    added = {p for p in current - upstream if (ROOT / p).exists() and game_resource(p)}
    errors += ['Added game resource in working tree: ' + p for p in sorted(added)]
    if history:
        commits = git('rev-list', base + '..' + head).decode().splitlines()
        for commit in commits:
            paths = git('diff-tree', '--root', '--no-commit-id', '--diff-filter=A',
                        '--name-only', '-r', commit).decode().splitlines()
            for path in paths:
                if path not in upstream and game_resource(path):
                    errors.append('Added game resource in history: %s %s' % (commit[:10], path))
        print('Audited %d mod commits against %s' % (len(commits), base))
    for kind, directory, extension in (('scripts', 'res/field/scripts', '.s'),
                                        ('battle_subscripts', 'res/battle/scripts/subscripts', '.s'),
                                        ('text', 'res/text', '.json')):
        covered = set((ROOT / 'res/port_patch_baselines' / kind / 'original.order').read_text().splitlines())
        changed = set()
        for path in sorted(upstream & current):
            if not path.startswith(directory + '/') or not path.endswith(extension):
                continue
            original = git('show', base + ':' + path)
            source = (ROOT / path).read_bytes()
            if source == original:
                continue
            name = Path(path).stem
            changed.add(name)
            if name not in covered:
                errors.append('Missing binary patch baseline: ' + path)
                continue
            patch = ROOT / 'res/port_patch_baselines' / kind / (name + '.patch')
            try:
                if restore(source, patch.read_bytes()) != original:
                    errors.append('Baseline differs from upstream: ' + path)
            except (OSError, ValueError) as error:
                errors.append('Invalid baseline for %s: %s' % (path, error))
        for name in covered - changed:
            errors.append('Baseline has no corresponding source change: ' + name)
        print('Checked %d changed %s against patch baselines' % (len(changed), kind))
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default=UPSTREAM)
    parser.add_argument('--tree-only', action='store_true', help='Skip historical commits')
    parser.add_argument('--head', default='HEAD', help='Release branch whose history to audit')
    args = parser.parse_args()
    errors = audit(args.base, not args.tree_only, args.head)
    for error in errors:
        print('FAIL: ' + error)
    if errors:
        raise SystemExit(1)
    print('PASS: no added game resources or complete scripts; all changed banks have baselines')


if __name__ == '__main__':
    main()
