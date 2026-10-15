# -*- coding: utf-8 -*-
"""Embebe src/fml_formats/psych-compat/fml-psych-compat.hx en
src/fml_formats/PsychCompatScript.cpp.

El .hx es la fuente de verdad (se lee, se prueba con el parser de hscript y
se edita como HScript); el .cpp es lo que compila. `fml_psychsourcecheck`
comprueba que no se hayan separado. Se parte en trozos de menos de 16 KB
porque MSVC no admite literales mas largos (C2026).

Uso: python tools/embed_compat.py   (desde la raiz del repositorio o desde tools/)
"""
import io, os, hashlib

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
source = os.path.join(root, 'src', 'fml_formats', 'psych-compat', 'fml-psych-compat.hx')
target = os.path.join(root, 'src', 'fml_formats', 'PsychCompatScript.cpp')

text = io.open(source, encoding='utf-8', newline='').read().replace('\r\n', '\n')
lines = text.split('\n')
parts, current = [], ''
for i, line in enumerate(lines):
    piece = line + ('\n' if i < len(lines) - 1 else '')
    if len(current.encode('utf-8')) + len(piece.encode('utf-8')) > 12000:
        parts.append(current)
        current = ''
    current += piece
if current:
    parts.append(current)

def literal(chunk):
    delimiter = 'fmlhx'
    while (')' + delimiter + '"') in chunk:
        delimiter += 'x'
    return 'R"' + delimiter + '(' + chunk + ')' + delimiter + '"'

out = []
out.append('// GENERADO por tools/embed_compat.py desde psych-compat/fml-psych-compat.hx.')
out.append('// No se edita a mano: se edita el .hx y se vuelve a generar.')
out.append('// sha256 del .hx: ' + hashlib.sha256(text.encode('utf-8')).hexdigest())
out.append('#include <cstddef>')
out.append('')
out.append('namespace fml {')
out.append('namespace psychcompat {')
out.append('')
out.append('namespace {')
out.append('const char* const kParts[] = {')
for chunk in parts:
    out.append(literal(chunk) + ',')
out.append('};')
out.append('}  // namespace')
out.append('')
out.append('const char* const* compatScriptParts(std::size_t* count) {')
out.append('    *count = sizeof(kParts) / sizeof(kParts[0]);')
out.append('    return kParts;')
out.append('}')
out.append('')
out.append('}  // namespace psychcompat')
out.append('}  // namespace fml')
out.append('')
io.open(target, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print('PsychCompatScript.cpp: %d trozos, %d bytes' % (len(parts), len(text.encode('utf-8'))))
