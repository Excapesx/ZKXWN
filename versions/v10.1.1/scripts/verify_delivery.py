#!/usr/bin/env python3
"""Read-only package/Git/configuration checks. Never connects to a gateway."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


def git(root, *args):
    try:
        result = subprocess.run(
            ['git', '-C', str(root), *args], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=15, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout if result.returncode == 0 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-root', type=Path)
    parser.add_argument('--expected-commit')
    parser.add_argument('--require-llm', action='store_true')
    parser.add_argument('--official-gateway', help='Organizer-confirmed address; never printed')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    errors = []
    if args.expected_root and args.expected_root.resolve() != root:
        errors.append('actual root differs from expected root')

    count = 0
    decision_type = None
    decision_config_changed = False
    try:
        manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
        if not isinstance(manifest, list) or not manifest:
            raise ValueError('invalid manifest')
        seen = set()
        for row in manifest:
            name = row['path']
            rel = PurePosixPath(name)
            path = (root / name).resolve()
            if (not isinstance(name, str) or '\\' in name or rel.is_absolute()
                    or '..' in rel.parts or ':' in name or name in seen
                    or root not in path.parents):
                raise ValueError('unsafe or duplicate manifest entry')
            seen.add(name)
            if not re.fullmatch(r'[0-9a-f]{64}', row['sha256']):
                raise ValueError('invalid digest')
            if not path.is_file():
                errors.append('manifest file missing: ' + name)
                continue
            changed = (path.stat().st_size != row['bytes']
                       or hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256'])
            # Only this registered enum may change in the organizer's window.
            # The source manifest remains strict for every other file.
            allowed_config = False
            if name == 'decision_config.json':
                config = json.loads(path.read_text(encoding='utf-8'))
                allowed_config = (isinstance(config, dict) and set(config) == {'decision_type'}
                                  and config['decision_type'] in ('balanced', 'collect', 'fight'))
                if not allowed_config:
                    errors.append('invalid registered decision_config')
                else:
                    decision_type = config['decision_type']
                    decision_config_changed = changed
            if changed and not allowed_config:
                errors.append('manifest mismatch: ' + name)
            count += 1
    except (OSError, ValueError, TypeError, KeyError, AttributeError):
        errors.append('cannot read a valid current manifest')

    head = git(root, 'rev-parse', 'HEAD')
    commit = head.decode('ascii', errors='replace').strip() if head else None
    status = git(root, 'status', '--porcelain', '--untracked-files=normal') if head else None
    clean = status == b'' if status is not None else None
    if args.expected_commit:
        if not re.fullmatch(r'[0-9a-fA-F]{40}', args.expected_commit):
            errors.append('expected commit must be a full 40-character SHA')
        elif commit != args.expected_commit.lower():
            errors.append('Git commit differs from expected commit')
        if clean is not True:
            errors.append('formal snapshot requires an available, clean Git worktree')
        # A sibling untracked desktop copy must not inherit the parent repo HEAD.
        tracked = git(root, 'ls-files', '-z', '--', '.')
        tracked_count = len([p for p in (tracked or b'').split(b'\0') if p])
        if tracked_count != count + 1:
            errors.append('package files are not all tracked by the requested Git snapshot')

    if args.require_llm or args.official_gateway is not None:
        gateway = os.environ.get('RMTC_AI_GATEWAY', '')
        if not gateway.strip() or not os.environ.get('RMTC_TEAM_TOKEN', '').strip():
            errors.append('RMTC_AI_GATEWAY and RMTC_TEAM_TOKEN must be set')
        if os.environ.get('RMTC_MODEL', ''):
            errors.append('RMTC_MODEL must be empty')
        for key in ('RMTC_MAX_TOKENS', 'RMTC_HTTP_TIMEOUT_MS'):
            value = os.environ.get(key, '')
            if value:
                try:
                    if not re.fullmatch(r'[+-]?[0-9]+', value):
                        raise ValueError()
                    number = int(value)
                    if number < 0 or number > 2147483647:
                        raise ValueError()
                except ValueError:
                    errors.append(key + ' must be a nonnegative C++ int')
        if args.official_gateway is not None and gateway != args.official_gateway:
            errors.append('gateway differs from the supplied organizer-confirmed address')

    result = {
        'program_version': (root / 'VERSION').read_text(encoding='ascii').strip(),
        'absolute_root': str(root), 'manifest_files_checked': count,
        'git_commit': commit, 'git_clean': clean,
        'llm_config_checked': args.require_llm or args.official_gateway is not None,
        'gateway_contacted': False,
        'decision_type': decision_type,
        'decision_config_changed': decision_config_changed,
        'binary_source_provenance_verified': False,
        'errors': errors, 'ok': not errors,
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if not errors else 1


if __name__ == '__main__':
    sys.exit(main())
