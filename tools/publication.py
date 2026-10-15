"""Publication boundaries shared by the packager and release checks."""
from pathlib import Path
import hashlib
import json
import re

PUBLIC_MARKDOWN = {
    'README.md': '# Psych Importer',
    'DEVELOPMENT.md': '# Development',
    'ARCHITECTURE.md': '# Architecture',
    'CODENAME_COMPATIBILITY.md': '# Codename Engine compatibility',
    'CONTRIBUTING.md': '# Contributing',
    'CHANGELOG.md': '# Changelog',
    'SECURITY.md': '# Reporting stability and output-integrity issues',
    'THIRD_PARTY_NOTICES.md': '# Third-party notices',
    'WIP/README.md': '# FML script-connection work in progress',
    'WIP/FMLScriptConnection/README.md': '# FML script connection',
    'third_party/CodenameEngine/NOTICE.md': '# Codename Engine',
    'third_party/PsychEngine/NOTICE.md': '# Psych Engine',
}
PUBLIC_EDITION_DOCS = ('README.md','CODENAME_COMPATIBILITY.md','CHANGELOG.md','CONTRIBUTING.md','SECURITY.md','THIRD_PARTY_NOTICES.md','LICENSE')
INTERNAL = ('BITACORA.md','design/','ANALISIS_','AUDITORIA_','internal/')
SPANISH_PROSE = re.compile(r'\b(?:el programa|la carpeta|las pruebas|los archivos|esta versi[oó]n|se comprueba|c[oó]mo compilar|qu[eé] hay en el paquete)\b',re.I)


ENGINE_LICENSE_SHA256 = {'CodenameEngine': '7382b1cf711e7a7c7af9816e0e07b49b91e7149b7c6d347225f98800fcb1f1c2', 'PsychEngine': '50e6751797c50dedd75ef1b8a0d9e42f5f8472e9fbce91f34718e9f97b0c780a'}

def license_errors(app):
    errors=[]
    for engine,expected in ENGINE_LICENSE_SHA256.items():
        path=Path(app)/'third_party'/engine/'LICENSE'
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=expected:
            errors.append('Missing or changed original engine license: '+engine)
    return errors

def internal(relative):
    return any(relative == part or relative.startswith(part) for part in INTERNAL)

def markdown_errors(app):
    errors=[]
    for rel,heading in PUBLIC_MARKDOWN.items():
        path=Path(app)/rel
        if not path.is_file():
            errors.append('Missing English publication document: '+rel);continue
        text=path.read_text(encoding='utf-8')
        if text.splitlines()[0] != heading:
            errors.append('Unexpected publication heading: '+rel)
        prose=re.sub(r'```.*?```','',text,flags=re.S)
        prose=re.sub(r'`[^`]*`','',prose)
        if SPANISH_PROSE.search(prose):
            errors.append('Spanish draft prose in an English publication document: '+rel)
    return errors

def verify_wip(app):
    module=Path(app)/'WIP/FMLScriptConnection'
    snapshot=(module/'upstream').resolve()
    manifest=json.loads((module/'SOURCE_MANIFEST.json').read_text(encoding='utf-8'))
    if manifest.get('schema') != 1 or not manifest.get('sourceRevision') or not manifest.get('files'):
        raise ValueError('Incomplete WIP source provenance')
    listed=set()
    for entry in manifest['files']:
        rel=entry['path'];path=(snapshot/rel).resolve()
        if rel in listed or not path.is_relative_to(snapshot) or not path.is_file():
            raise ValueError('Invalid WIP source entry: '+rel)
        data=path.read_bytes()
        if len(data)!=entry['size'] or hashlib.sha256(data).hexdigest()!=entry['sha256']:
            raise ValueError('Changed WIP source snapshot: '+rel)
        listed.add(rel)
    actual={p.relative_to(snapshot).as_posix() for p in snapshot.rglob('*') if p.is_file()}
    if actual != listed:
        raise ValueError('WIP source inventory differs from its manifest')
    return len(listed)

def repository_readme(text):
    # The source ZIP has a real README at its root. Its document links point
    # into the application directory; the binary ZIP keeps local links.
    for name in ('LICENSE',*PUBLIC_MARKDOWN):
        text=text.replace(']('+name+')','](PsychImporterStandalone/'+name+')')
    return text
