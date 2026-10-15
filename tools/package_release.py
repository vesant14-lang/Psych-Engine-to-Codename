"""Arma los dos paquetes de una versión de Psych Importer.

    python tools/package_release.py <carpeta de salida>

PsychImporter-<v>-Windows-x64.zip  el programa listo para usar
PsychImporter-<v>-Developer.zip    lo necesario para compilarlo y probarlo

Junto a ellos deja SHA256SUMS.txt y manifest.json (cada archivo con su tamaño
y su hash). La carpeta de salida no puede tener ya los ZIP: nunca sobrescribe.

El Developer lleva los archivos del importador que están en git (sin
BITACORA.md ni design/) y, del resto del árbol de FML, solo lo que abre la
compilación: las unidades que nombra build.bat y las cabeceras que MSVC dice
que incluye (cl /Zs /showIncludes, con las mismas opciones). Más SDL3.lib y
SDL3.dll para enlazar y ejecutar, setup_msvc.bat y las licencias. Hace falta
MSVC instalado, como para compilar.
"""
import hashlib, json, os, re, subprocess, sys, tempfile, time, zipfile

APP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(APP)
SDL = os.path.join(ROOT, "third_party", "SDL3-3.4.14")
IMGUI = os.path.join(ROOT, "third_party", "imgui")
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(__file__))
import publication
LEFT_OUT = publication.INTERNAL

MIT = """Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""


def version():
    text = open(os.path.join(APP, "src", "Version.hpp"), encoding="utf-8").read()
    return re.search(r'PSYCH_IMPORTER_VERSION\s+"([^"]+)"', text).group(1)


def read(path):
    return open(path, encoding="utf-8", errors="replace").read()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# --- Developer: what build.bat compiles and what the compiler opens ----------
def build_units(root=ROOT):
    app = os.path.join(root, "PsychImporterStandalone")
    imgui = os.path.join(root, "third_party", "imgui")
    units, loop = [], None
    for line in read(os.path.join(app, "build.bat")).splitlines():
        found = re.search(r"for %%U in \(([^)]*)\) do", line)
        if found:
            loop = found.group(1).split()
        elif line.strip() == ")":
            loop = None
        for path in re.findall(r'"(%(?:ROOT|APP|IMGUI)%[^"]+\.(?:cpp|c))"', line):
            path = (path.replace("%ROOT%", root + "\\").replace("%APP%", app + "\\")
                        .replace("%IMGUI%", imgui + "\\"))
            for name in (loop if "%%U" in path else [None]):
                units.append(os.path.normpath(path.replace("%%U", name) if name else path))
    return sorted(set(units))


def compiled_files(root=ROOT):
    """Every file the compiler opens to build the importer and its checks, as
    paths relative to `root`. Only parses (/Zs): nothing is written."""
    units = build_units(root)
    for unit in units:
        if not os.path.isfile(unit):
            sys.exit("build.bat nombra un archivo que no existe: " + unit)
    sdl_include = os.path.join(root, "third_party", "SDL3-3.4.14", "include")
    imgui = os.path.join(root, "third_party", "imgui")
    with tempfile.TemporaryDirectory(prefix="pi-includes-") as tmp:
        rsp = os.path.join(tmp, "units.rsp")
        # A Unicode response file: the paths may carry accents.
        with open(rsp, "w", encoding="utf-16") as f:
            f.write('/nologo /Zs /EHsc /std:c++17 /utf-8 /bigobj /W0 /showIncludes\n/I"%s"\n/I"%s"\n' % (sdl_include, imgui))
            f.writelines('"%s"\n' % u for u in units)
        command = 'call "%s" >nul && cl @"%s"' % (os.path.join(root, "setup_msvc.bat"), rsp)
        done = subprocess.run("cmd /c " + command, capture_output=True, cwd=tmp, env=dict(os.environ))
    output = (done.stdout + done.stderr).decode("mbcs", errors="replace")
    if done.returncode != 0:
        sys.exit("MSVC no pudo leer las unidades (salida %d):\n%s" % (done.returncode, output[-2000:]))
    # Paths come back as cl opened them; keep the part under `root`, which is
    # plain ASCII even when `root` itself has accents.
    marker = os.path.basename(os.path.normpath(root)).lower() + "\\"
    found = {os.path.relpath(u, root) for u in units}
    for line in output.splitlines():
        # The note's wording depends on Visual Studio's language: any line
        # naming an existing file under `root` counts.
        at = line.lower().find(marker)
        if at >= 0:
            rel = os.path.normpath(line[at + len(marker):].strip())
            if os.path.isfile(os.path.join(root, rel)):
                found.add(rel)
    if len(found) <= len(units):
        sys.exit("MSVC no listó ninguna cabecera (¿falta /showIncludes?):\n" + output[-2000:])
    # The case the files have on disk.
    disk = {}
    for top in ("src", "third_party", "PsychImporterStandalone"):
        for base, _, names in os.walk(os.path.join(root, top)):
            for n in names:
                rel = os.path.relpath(os.path.join(base, n), root)
                disk[os.path.normcase(rel)] = rel
    return sorted({disk.get(os.path.normcase(f), f) for f in found})


def git_files():
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "--cached", "--others", "--exclude-standard", "--", "PsychImporterStandalone"],
                         capture_output=True, text=True, encoding="utf-8", check=True).stdout
    files = []
    for rel in out.splitlines():
        inner = rel[len("PsychImporterStandalone/"):]
        # A tracked cache is still generated output, even if Git ignores new ones.
        if "__pycache__" in inner.split("/") or inner.lower().endswith((".pyc", ".pyo")):
            continue
        if any(inner == x or inner.startswith(x) for x in LEFT_OUT):
            continue
        if os.path.isfile(os.path.join(ROOT, rel)):
            if inner.lower().endswith(".md") and inner not in publication.PUBLIC_MARKDOWN:
                sys.exit("Classify this Markdown as English-public or internal before packaging: " + inner)
            files.append(os.path.normpath(os.path.join(ROOT, rel)))
    return files


def developer_files():
    files = set(git_files()) | {os.path.join(ROOT, f) for f in compiled_files()}
    files.add(os.path.join(ROOT, "setup_msvc.bat"))
    # SDL3.lib to link and SDL3.dll to run; not the 15 MB of symbols.
    for extra in (os.path.join(SDL, "lib", "x64", "SDL3.lib"), os.path.join(SDL, "lib", "x64", "SDL3.dll"),
                  os.path.join(SDL, "LICENSE.txt"), os.path.join(ROOT, "third_party", "miniz", "LICENSE")):
        files.add(extra)
    unique = {}
    for f in files:
        f = os.path.normpath(f)
        unique.setdefault(os.path.normcase(f), f)
    root = os.path.normcase(os.path.normpath(ROOT)) + os.sep
    out = []
    for key, f in sorted(unique.items()):
        if not key.startswith(root):
            sys.exit("fuera del árbol de FML: " + f)
        out.append(os.path.relpath(f, ROOT))
    return out


# --- Public: the program, README and licences ------------------------------
def tail_block(path, start):
    text = read(path)
    at = text.rfind(start)
    if at < 0:
        sys.exit("no encuentro la licencia en " + path)
    return text[at:]


def default_font_licences():
    source = read(os.path.join(IMGUI, "imgui_draw.cpp"))
    blocks = []
    for name in ("ProggyClean.ttf", "ProggyForever-Regular-minimal.ttf"):
        at = source.rfind("// [SECTION] Default font data (" + name + ")")
        match = re.search(r"// MIT [Ll]icense / (Copyright[^\n]+)", source[at:]) if at >= 0 else None
        if not match:
            sys.exit("Review the embedded default-font license before packaging: " + name)
        notices = "\n".join(re.split(r", (?=Copyright)", match.group(1).strip()))
        blocks.append(name + "\nThe MIT License (MIT)\n\n" + notices + "\n\n" + MIT)
    return "\n".join(blocks)


def licences():
    pugixml = tail_block(os.path.join(ROOT, "third_party", "pugixml.hpp"), "Copyright (c) 2006")
    pugixml = "\n".join(line.strip().lstrip("*").strip() for line in pugixml.split("*/")[0].splitlines()).strip() + "\n"
    stb = tail_block(os.path.join(ROOT, "third_party", "stb_image.h"), "This software is available under 2 licenses").split("*/")[0].strip() + "\n"
    miniaudio = tail_block(os.path.join(ROOT, "third_party", "miniaudio.h"), "This software is available as a choice of the following licenses").split("*/")[0].strip() + "\n"
    imgui_year = re.search(r"Copyright \(c\) (\d{4}-\d{4}) Omar Cornut", read(os.path.join(IMGUI, "imgui.cpp"))).group(1)
    json_year = re.search(r"SPDX-FileCopyrightText: (\d{4}-\d{4}) Niels Lohmann", read(os.path.join(ROOT, "third_party", "json.hpp"))).group(1)
    return {
        "SDL3.txt": read(os.path.join(SDL, "LICENSE.txt")),
        "Dear-ImGui.txt": "Dear ImGui\nThe MIT License (MIT)\n\nCopyright (c) %s Omar Cornut\n\n%s" % (imgui_year, MIT) + "\nEmbedded default fonts\n\n" + default_font_licences(),
        "pugixml.txt": "pugixml\n\n" + pugixml,
        "miniz.txt": read(os.path.join(ROOT, "third_party", "miniz", "LICENSE")),
        "nlohmann-json.txt": "JSON for Modern C++\nMIT License\n\nCopyright (c) %s Niels Lohmann\n\n%s" % (json_year, MIT),
        "stb.txt": "stb_image, stb_image_write, stb_vorbis, imstb_truetype, imstb_rectpack, imstb_textedit (Sean Barrett)\n\n" + stb,
        "miniaudio.txt": "miniaudio (David Reid)\n\n" + miniaudio,
        "CodenameEngine-LICENSE.txt": read(os.path.join(APP, "third_party", "CodenameEngine", "LICENSE")),
        "CodenameEngine-NOTICE.md": read(os.path.join(APP, "third_party", "CodenameEngine", "NOTICE.md")),
        "PsychEngine-LICENSE.txt": read(os.path.join(APP, "third_party", "PsychEngine", "LICENSE")),
        "PsychEngine-NOTICE.md": read(os.path.join(APP, "third_party", "PsychEngine", "NOTICE.md")),
    }


def write_zip(target, entries):
    """entries: [(name in zip, source path or bytes)]. Returns the manifest."""
    manifest = []
    with zipfile.ZipFile(target, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, source in sorted(entries, key=lambda e: e[0].lower()):
            data = source if isinstance(source, bytes) else open(source, "rb").read()
            info = zipfile.ZipInfo(name, date_time=time.localtime()[:6])
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
            manifest.append({"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    return manifest


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = os.path.abspath(sys.argv[1])
    v = version()
    public_zip = os.path.join(out, "PsychImporter-%s-Windows-x64.zip" % v)
    developer_zip = os.path.join(out, "PsychImporter-%s-Developer.zip" % v)
    if os.path.exists(public_zip) or os.path.exists(developer_zip):
        sys.exit("ya hay paquetes de la %s en %s; no se sobrescriben" % (v, out))
    exe = os.path.join(APP, "PsychImporter.exe")
    dll = os.path.join(APP, "SDL3.dll")
    for f in (exe, dll):
        if not os.path.isfile(f):
            sys.exit("falta " + f + ": compila antes con build.bat")
    doc_errors = publication.markdown_errors(APP) + publication.license_errors(APP)
    if doc_errors: sys.exit("\n".join(doc_errors))
    publication.verify_wip(APP)
    developer_paths = developer_files()
    code_paths = [os.path.join(ROOT, rel) for rel in developer_paths if os.path.splitext(rel)[1].lower() in (".c", ".cpp", ".h", ".hpp", ".inl") and not rel.replace(os.sep,"/").startswith("PsychImporterStandalone/WIP/")]
    newest = max(os.path.getmtime(path) for path in code_paths)
    if os.path.getmtime(exe) < newest:
        sys.exit("PsychImporter.exe es más viejo que sus fuentes o el núcleo compartido: vuelve a compilar")
    built = subprocess.run([exe, "--version"], capture_output=True, text=True, encoding="utf-8", timeout=60)
    if built.returncode or built.stdout.strip() != "Psych Importer " + v:
        sys.exit("el ejecutable no dice la versión de sus fuentes: vuelve a compilar")
    readme = read(os.path.join(APP, "README.md"))
    if "Version " + v not in readme:
        sys.exit("README.md does not declare Version %s" % v)
    # The output folder is one that exists: no tool creates folders on its own (DESIGN.md §0).
    if not os.path.isdir(out):
        sys.exit("la carpeta de salida no existe: " + out)

    top = "PsychImporter-%s/" % v
    public = [(top + "PsychImporter.exe", exe), (top + "SDL3.dll", dll)]
    public += [(top + name, os.path.join(APP, name)) for name in publication.PUBLIC_EDITION_DOCS]
    public += [(top + "licenses/" + name, text.replace("\r\n", "\n").encode("utf-8")) for name, text in licences().items()]
    public_manifest = write_zip(public_zip, public)

    top = "PsychImporter-%s-Developer/" % v
    developer = [(top + rel.replace(os.sep, "/"), os.path.join(ROOT, rel)) for rel in developer_paths]
    developer += [(top + "README.md", publication.repository_readme(readme).encode("utf-8")), (top + "LICENSE", os.path.join(APP,"LICENSE"))]
    developer += [(top + "licenses/" + name, text.replace("\r\n", "\n").encode("utf-8")) for name, text in licences().items()]
    developer_manifest = write_zip(developer_zip, developer)

    sums = "".join("%s  %s\n" % (sha256(z), os.path.basename(z)) for z in (public_zip, developer_zip))
    open(os.path.join(out, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="\n").write(sums)
    json.dump({"version": v, "created": time.strftime("%Y-%m-%d %H:%M:%S"), "exe_sha256": sha256(exe),
               "public": public_manifest, "developer": developer_manifest},
              open(os.path.join(out, "manifest.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("Psych Importer %s" % v)
    print("  %s  %d archivos  %.1f MB" % (os.path.basename(public_zip), len(public_manifest), os.path.getsize(public_zip) / 1e6))
    print("  %s  %d archivos  %.1f MB" % (os.path.basename(developer_zip), len(developer_manifest), os.path.getsize(developer_zip) / 1e6))


if __name__ == "__main__":
    main()
