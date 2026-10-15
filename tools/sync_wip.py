"""Capture the isolated FML script-connection source snapshot.

Usage: python tools/sync_wip.py <reviewed FML root>
The application never compiles this snapshot. Local edits are preserved.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

APP = Path(__file__).resolve().parent.parent
OUTPUT = APP / 'WIP' / 'FMLScriptConnection'
SEEDS = (
    'src/fml_formats/PsychBehaviorPlan.cpp',
    'src/fml_formats/HscriptEmit.cpp',
    'src/fml_formats/PsychApiCatalog.cpp',
    'src/fml_formats/PsychCompat.cpp',
    'src/fml_formats/PsychCompatScript.cpp',
    'src/fml_formats/PsychSourceBinding.hpp',
    'src/fml_formats/PsychScriptRelations.hpp',
    'src/fml_formats/PsychBehaviorSource.hpp',
    'src/fml_formats/LuaAst.cpp',
    'src/fml_core/Hash.cpp',
    'src/fml_io/SafeWrite.cpp',
)
EXTRAS = ('src/fml_formats/psych-compat/fml-psych-compat.hx', 'tools/embed_compat.py')
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"\r\n]+)"', re.M)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def capture(root):
    pending, files = list(SEEDS), {}
    while pending:
        rel = pending.pop()
        path = (root / rel).resolve()
        if not path.is_relative_to(root):
            raise RuntimeError('Dependency escapes the reviewed source root: ' + rel)
        key = path.relative_to(root).as_posix()
        if key in files:
            continue
        data = path.read_bytes()
        files[key] = data
        for include in INCLUDE.findall(data.decode('utf-8-sig')):
            child = (path.parent / include).resolve()
            if not child.is_relative_to(root) or not child.is_file():
                raise RuntimeError('Missing or external local include: ' + key + ' -> ' + include)
            pending.append(child.relative_to(root).as_posix())
    for rel in EXTRAS:
        path=(root/rel).resolve()
        if not path.is_relative_to(root):raise RuntimeError('Extra source escapes the reviewed root: '+rel)
        files[rel] = path.read_bytes()
    return files

def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    root = Path(sys.argv[1]).resolve()
    files = capture(root)
    manifest_path = OUTPUT / 'SOURCE_MANIFEST.json'
    previous = json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.exists() else {}
    known = {entry['path']: entry for entry in previous.get('files', [])}
    snapshot = OUTPUT / 'upstream'
    # Check every old entry before writing anything. Never replace a local edit.
    for key, entry in known.items():
        path = (snapshot / key).resolve()
        if not path.is_relative_to(snapshot.resolve()) or not path.is_file() or digest(path.read_bytes()) != entry['sha256']:
            raise RuntimeError('Local snapshot change; review before synchronizing: ' + key)
        if key not in files:
            raise RuntimeError('Snapshot file no longer needed; review its removal manually: ' + key)
    for key, data in files.items():
        path = (snapshot / key).resolve()
        if not path.is_relative_to(snapshot.resolve()):raise RuntimeError('Snapshot destination escapes its root: '+key)
        if path.exists() and key not in known and path.read_bytes() != data:
            raise RuntimeError('Untracked snapshot file would be replaced: ' + key)
    revision = subprocess.run(['git', '-C', str(root), 'rev-parse', 'HEAD'], check=True, capture_output=True, text=True).stdout.strip()
    status = subprocess.run(['git', '-C', str(root), 'status', '--porcelain', '--untracked-files=no'], check=True, capture_output=True, text=True).stdout
    entries = []
    for key, data in sorted(files.items()):
        path = snapshot / key
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        entries.append({'path': key, 'size': len(data), 'sha256': digest(data)})
    manifest = {'schema': 1, 'purpose': 'Isolated FML script-connection development snapshot; excluded from application build',
                'sourceRevision': revision, 'sourceWorkingTreeDirty': bool(status.strip()),
                'seeds': list(SEEDS), 'files': entries}
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('Captured %d source/dependency files; source revision %s; dirty=%s' % (len(entries), revision[:12], manifest['sourceWorkingTreeDirty']))

if __name__ == '__main__':
    main()
