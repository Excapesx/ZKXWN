#!/usr/bin/env python3
"""Print a local launch fingerprint without credentials or gateway addresses."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--config',type=Path,required=True)
    args=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    binary=args.binary.resolve()
    if not binary.is_file() or not binary.is_relative_to(root):
        raise SystemExit('Resolved ROS executable is missing or belongs to another package root')
    config=args.config.resolve()
    data=json.loads(config.read_text(encoding='utf-8'))
    if not isinstance(data,dict) or set(data)!={'decision_type'} or data['decision_type'] not in ('balanced','collect','fight'):
        raise SystemExit('Configuration must contain one registered decision_type')
    def git(*words):
        try:
            r=subprocess.run(['git','-C',str(root),*words],capture_output=True,timeout=10)
            return r.stdout.decode('utf-8',errors='replace').strip() if r.returncode==0 else None
        except (OSError,subprocess.TimeoutExpired):return None
    h=hashlib.sha256()
    for f in sorted((root/'src').rglob('*')):
        if f.is_file():
            h.update(f.relative_to(root).as_posix().encode());h.update(b'\0');h.update(f.read_bytes())
    state=git('status','--porcelain')
    print(json.dumps({'program_version':(root/'VERSION').read_text().strip(),
        'package_root':str(root),'git_commit':git('rev-parse','HEAD'),
        'git_clean':None if state is None else state=='',
        'binary_path':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'core_tree_sha256':h.hexdigest(),'config_sha256':hashlib.sha256(config.read_bytes()).hexdigest(),
        'decision_type':data['decision_type'],
        'binary_source_provenance_verified':False},ensure_ascii=False))


if __name__=='__main__':main()
