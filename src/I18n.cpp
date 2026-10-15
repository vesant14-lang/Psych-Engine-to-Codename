#include "I18n.hpp"
#include "../../src/fml_app/UiText.hpp"

namespace pi {
Language language() { return fml::uiLanguage()==fml::UiLanguage::English?Language::English:Language::Spanish; }
void setLanguage(Language value) { fml::setUiLanguage(value==Language::English?fml::UiLanguage::English:fml::UiLanguage::Spanish); }
bool loadLanguage(const std::filesystem::path& path) { return fml::loadUiLanguage(path); }
bool saveLanguage(const std::filesystem::path& path) { return fml::saveUiLanguage(path); }

namespace {
// Explicit message templates; captured resource data is copied byte for byte.
const std::vector<std::pair<std::string,std::string>> entries={
    {"Abre un mod o archivos JSON sueltos para empezar.","Open a mod or loose JSON files to begin."},
    {"Buscando JSON y recursos en varias rutas...","Searching several folders for definitions and resources..."},
    {"No se encontraron personajes ni charts Psych. Revisa el informe.","No Psych resources were found. Check the report."},
    {"%s personajes · %s canciones · %s escenarios · %s archivos revisados","%s characters · %s songs · %s stages · %s files scanned"},
    {"Escribiendo importación...","Writing import..."},
    {"Sin icono","No icon"},
    {"Imagen no encontrada o no legible","Image not found or unreadable"},
    {"Imagen de escenario sin resolver: %s · %s","Unresolved stage image: %s · %s"},
    {"Imagen no legible o demasiado grande","Image is unreadable or too large"},
    {"PNG inválido o excede el límite","Invalid PNG or size limit exceeded"},
    {"PNG inválido o excede el límite: %s","Invalid PNG or size limit exceeded: %s"},
    {"Atlas o PNG inválido: %s","Invalid atlas or PNG: %s"},
    {"Datos de icono inválidos: %s","Invalid icon data: %s"},
    {"Frame fuera de la imagen: %s","Frame outside the image: %s"},
    {"Importación cancelada antes de escribir: %s","Import cancelled before writing: %s"},
    {"Importación cancelada: la fuente cambió desde la revisión del plan: %s","Import cancelled: the source changed after the plan was reviewed: %s"},
    {"Se omite (imagen dañada o demasiado grande): %s","Left out (damaged or oversized image): %s"},
    {"Importación cancelada: %s archivos.","Import cancelled: %s files."},
    {"La imagen excede el límite de la GPU","Image exceeds the GPU size limit"},
    {"Límite de imágenes alcanzado; vuelve a cargar el origen","Image limit reached; reload the source"},
    {"No se pudo decodificar la imagen","Could not decode image"},
    {"Elige una carpeta de destino existente.","Choose an existing destination folder."},
    {"El destino debe estar separado de las carpetas de origen.","Destination must be separate from source folders."},
    {"Ruta destino inválida: %s","Invalid destination path: %s"},
    {"Dos recursos escribirían la misma ruta: %s","Two resources would write the same path: %s"},
    {"Ya existe; usa otro ID: %s","Already exists; use another ID: %s"},
    {"Recurso ausente, vacío o demasiado grande: %s","Resource is missing, empty or too large: %s"},
    {"ID inválido: %s. Usa letras, números, guion o guion bajo.","Invalid ID: %s. Use letters, numbers, hyphens or underscores."},
    {"%s: esta primera versión importa Sparrow de una sola hoja.","%s: multi-sheet characters are not supported yet."},
    {"%s: elige la imagen y el atlas XML.","%s: choose the image and XML atlas."},
    {"%s: atlas inválido.","%s: invalid atlas."},
    {"%s: animación sin fotogramas: %s","%s: animation has no frames: %s"},
    {"%s: selecciona al menos una animación válida.","%s: select at least one valid animation."},
    {"%s: no hay icono de origen; requiere face en el destino.","%s: source icon is missing; destination needs face."},
    {"%s: Lua conservado sin activar.","%s: Lua archived without activation."},
    {"ID de canción inválido.","Invalid song ID."},
    {"Ya existe esa canción. Usa otro ID.","That song already exists. Use another ID."},
    {"Selecciona al menos una dificultad.","Select at least one difficulty."},
    {"%s: falta el escenario %s en destino/base Codename. Elige uno existente.","%s: stage %s is missing in the destination/Codename base. Choose an existing one."},
    {"Personaje con ruta inválida: %s","Character has an invalid path: %s"},
    {"Personaje requiere un ID válido: %s","Character needs a valid ID: %s"},
    {"Falta personaje %s. Carga su JSON suelto o instálalo en el destino; múltiples coincidencias requieren selección manual.","Missing character %s. Load its loose JSON or install it in the destination; multiple matches need manual selection."},
    {"Dificultad con nombre inválido: %s","Invalid difficulty name: %s"},
    {"%s: se requiere OGG Vorbis válido: %s","%s: valid OGG Vorbis is required: %s"},
    {"%s: instrumental no encontrado. Elígelo manualmente.","%s: instrumental not found. Choose it manually."},
    {"%s: los tipos de nota custom requieren revisar su comportamiento.","%s: custom note types need their behavior reviewed."},
    {"Scripts locales archivados sin activar. Esta importación no convierte modcharts, cinemáticas ni mecánicas.","Local scripts archived without activation. This import does not convert modcharts, cutscenes or mechanics."},
    {"Importación básica completada: %s archivos.","Basic import completed: %s files."},
    {"Importación parcial: %s archivos.","Partial import: %s files."},
    {"No se pudo guardar el manifiesto.","Could not save the manifest."},
    {"No se reconoce carpeta: %s","Unrecognized folder: %s"},
    {"No se pudo resolver carpeta: %s","Could not resolve folder: %s"},
    {"No se pudo revisar una carpeta: %s","Could not scan a folder: %s"},
    {"Límite de 100.000 archivos alcanzado; divide la carpeta.","100,000 file limit reached; split the folder."},
    {"Imagen no encontrada. Puedes elegirla manualmente.","Image not found. You can choose it manually."},
    {"Varias imágenes coinciden. Elige el archivo correcto.","Several images match. Choose the correct file."},
    {"Atlas XML no encontrado. Puedes elegirlo manualmente.","XML atlas not found. You can choose it manually."},
    {"Varios atlas coinciden. Elige el archivo correcto.","Several atlases match. Choose the correct file."},
    {"Audio ambiguo: %s.ogg; selección manual requerida.","Ambiguous audio: %s.ogg; manual selection required."},
    {"%s: chart inválido","%s: invalid chart"},
    {"%s: Animate no válido: %s","%s: invalid Animate atlas: %s"},
    {"%s: Animate contiene efectos no convertibles: %s","%s: Animate contains unsupported effects: %s"},
    {"%s: escenario vacío; no se puede importar como completo.","%s: empty stage; cannot import it as a complete stage."},
    {"%s: recurso de escenario sin resolver: %s","%s: unresolved stage resource: %s"},
    {"%s: escenario ambiguo o ausente: %s","%s: stage is ambiguous or missing: %s"},
    {"%s: no se importará el escenario %s. Tendrás que modificar posteriormente la declaración stage del chart a un escenario existente en el destino.","%s: stage %s will not be imported. You must later change the chart's stage declaration to a stage available in the destination."},
    {"%s: lógica dinámica pendiente; se importa el decorado declarativo.","%s: dynamic logic remains to be ported; importing the declarative scenery."},
    {"%s: vista previa aproximada; revisa el informe de FML.","%s: approximate preview; check the FML report."},
    {"atlas sin frames","atlas has no frames"},
    {"no se encontro <TextureAtlas>","<TextureAtlas> was not found"},
    {"atlas XML invalido: %s","invalid atlas XML: %s"},
    {"frame con tamano invalido: %s","invalid frame size: %s"},
    {"el personaje no declara `image`: sin atlas no hay nada que dibujar","character does not declare `image`: nothing to draw without an atlas"},
    {"campos que FML no conoce y CONSERVA tal cual: %s. Si son de una version mas nueva de Psych, no se pierden","fields unknown to FML and PRESERVED unchanged: %s. Fields from newer Psych versions are not lost"},
    {"la animacion `%s` no declara prefijo de atlas (`name`): en el juego cargaria sin fotogramas","animation `%s` has no atlas prefix (`name`): it would load without frames"},
    {"`image` declara %s atlas separados por comas; deben resolverse todas sus páginas","`image` declares %s comma-separated atlases; all pages must be resolved"},
    {"el personaje no declara ninguna animacion: en el juego saldria quieto y sin `idle`","character has no animations: it would appear static without `idle`"},
    {"events.json was imported as global events","events.json se importó como eventos globales"},
    {"%s malformed Psych events were skipped","Se omitieron %s eventos Psych malformados"},
    {"No se pudo guardar el idioma; el cambio se mantiene en esta sesión.","Could not save the language; this session keeps the change."},
    {"Elige canción, dificultad y función del audio.","Choose a song, difficulty and audio role."},
    {"Elige personaje, objeto de escenario o icono.","Choose a character, stage object or icon."},
    {"Elige a qué personaje u objeto pertenece el atlas.","Choose which character or object owns this atlas."},
    {"Página de Animate; necesita su Animation.json.","Animate page; requires its Animation.json."},
    {"Asociación por declaración o ruta resuelta.","Associated through a declaration or resolved path."},
    {"Formato no reconocido; conservado en la lista para revisión.","Unrecognized format; kept in the review list."},
    {"Solo se pueden unir grupos con el mismo ID de canción.","Only groups with the same song ID can be merged."},
    {"Hay dificultades con el mismo nombre; conserva los grupos separados.","Duplicate difficulty names; keep these groups separate."},
    {"Selecciona al menos un recurso.","Select at least one resource."},
    {"Solo definición: copia sus recursos y revisa las rutas antes de usarla.","Definition only: copy its assets and check paths before use."},
    {"Solo charts: configura audio, personajes y la declaración stage posteriormente.","Charts only: configure audio, characters and the stage declaration later."},
    {"Chart con dependencias seleccionadas: configura el audio en destino.","Chart with selected dependencies: configure destination audio."},
    {"Elige el recurso de escenario para la clave: %s","Choose the stage asset for key: %s"},
    {"Elige el recurso de personaje para la clave: %s","Choose the character asset for key: %s"},
    {"Una misma clave de escenario tiene recursos diferentes: %s","One stage key has conflicting assets: %s"},
    {"Una misma clave de personaje tiene recursos diferentes: %s","One character key has conflicting assets: %s"},
    {"Solo audio: este grupo no contiene chart ni metadatos de canción; tendrás que asociarlo posteriormente.","Audio only: this group has no chart or song metadata; associate it later."},
    {"No se pudo crear el ZIP.","Could not create the ZIP."},
    {"ZIP creado: %s","ZIP created: %s"},
    {"%s: no se importará el escenario %s. Tendrás que modificar posteriormente la declaración stage del chart a un escenario existente en el destino.","%s: stage %s will not be imported. Edit the chart's stage declaration later to use an existing destination stage."},
    {"%s: evento sin equivalente: %s (%s)","%s: event without an equivalent: %s (%s)"},
    {"Animation.json / spritemap inválido o incompleto","Invalid or incomplete Animation.json / spritemap"},
    {"Animate supera las 8 páginas que admite el lector actual de FML.","Animate exceeds the 8 pages supported by the current FML reader."},
    {"Página Animate ausente o fuera de la carpeta: %s","Animate page is missing or outside its folder: %s"},
    {"Animate no tiene páginas PNG","Animate has no PNG pages"},
    {"no hay evento equivalente: `HScript Call`","no equivalent event: `HScript Call`"},
    {"Se omite (ya existe en el destino y no se sobrescribe): %s","Left out (already in the destination, not overwritten): %s"},
    {"Se omite (otro recurso ya escribe esa ruta; se conserva el primero): %s","Left out (another resource writes that path; the first one is kept): %s"},
    {"Se omite (ruta destino inválida): %s","Left out (invalid destination path): %s"},
    {"Se omite (recurso ausente, vacío o demasiado grande): %s","Left out (resource missing, empty or too large): %s"},
    {"Se omite el personaje %s porque le falta una parte; Codename usará su personaje de reserva (bf) donde aparezca.","Character %s is left out because a part is missing; Codename uses its fallback character (bf) wherever it appears."},
    {"Se omite el escenario %s porque le falta una parte; las canciones que lo usan se jugarán sin decorado.","Stage %s is left out because a part is missing; songs using it play without scenery."},
    {"Se omite la canción %s porque le falta su instrumental, un chart o sus metadatos.","Song %s is left out because its instrumental, a chart or its metadata is missing."},
    {"No se exporta: %s","Not exported: %s"},
    {"Se exporta con este problema: %s","Exported with this problem: %s"},
    {"No queda nada que exportar sin lo bloqueado.","Nothing is left to export without the blocked parts."},
    {"%s: %s ajustes del script no tienen equivalente en el XML y se han dejado fuera","%s: %s stage script settings have no XML equivalent and were left out"},
    {"La exportación supera el límite de %s archivos; divídela en varias.","The export exceeds the %s file limit; split it."},
    {"La exportación supera el límite de 16 GB; divídela en varias.","The export exceeds the 16 GB limit; split it."},
    {"No hay espacio suficiente en el disco de destino: hacen falta %s MB y quedan %s MB.","Not enough space on the destination disk: %s MB needed, %s MB free."},
    {"El ZIP superaría 3,5 GB; exporta a carpeta o divide la selección.","The ZIP would exceed 3.5 GB; export to a folder or split the selection."},
    {"... y %s avisos más (lista completa en el manifiesto).","... and %s more warnings (full list in the manifest)."},
    {"... y %s archivos más.","... and %s more files."},
    {"Bloqueos (%s): impiden generar este paquete.","Blocked (%s): these issues prevent creating this package."},
    {"hay más problemas; revisa los recursos por separado.","there are more issues; review the resources separately."},
    {"Puede afectar al juego (%s): cambios o problemas que revisar antes de exportar.","May affect gameplay (%s): changes or problems to review before exporting."},
    {"Informativo (%s): archivos de referencia y conversiones realizadas.","Informational (%s): reference files and completed conversions."},
    {"Scripts originales archivados como referencia.","Original scripts archived for reference."},
    {"%s: pueden quedar acciones de scripts sin convertir; revisa el comportamiento en Codename.","%s: some script actions may remain unconverted; check the behavior in Codename."},
    {"%s: el comportamiento Lua del personaje no se conecta; requiere porteo.","%s: the character's Lua behavior is not connected; it needs porting."},
    {"Se omite el recurso %s: su tipo no se puede marcar en Recursos.","Resource %s is left out: its type cannot be selected in Resources."},
    {"Para revisar (%s): lo que Codename no tendrá o hará distinto.","To review (%s): what Codename will lack or do differently."},
    {"Informativo (%s): notas que no cambian lo que se ve ni lo que suena.","Informational (%s): notes that do not change what is seen or heard."},
    {"Informativo: %s notas ocultas.","Informational: %s notes hidden."},
    {"... y %s notas más.","... and %s more notes."},
    {"Se omite el personaje %s: %s","Character %s is left out: %s"},
    {"Se omite el escenario %s: %s","Stage %s is left out: %s"},
    {"%s: el personaje %s no está en el juego; el chart usa %s.","%s: character %s is not in the game; the chart uses %s."},
    {"%s/%s: el escenario %s no está en el juego; el chart usa stage.","%s/%s: stage %s is not in the game; the chart uses stage."},
    {"%s: cantan por tipo de nota %s; aparecen al lado de su línea, sin la coreografía del script original.","%s: by note type sing %s; they stand beside their line, without the original script's choreography."},
    {"Los mods por separado no van directamente en la carpeta mods de Codename: expórtalos a otra carpeta y copia a mods los que quieras.","Separate bundles cannot go straight into Codename's mods folder: export them somewhere else and copy the ones you want into mods."},
    {"Se usa el escenario %s de Codename: el de origen va compilado en su motor y solo se reconstruiría sin su disposición.","Codename's own %s stage is used: the source one is compiled into its engine and could only be rebuilt without its layout."},
    {"Se omite la canción %s: %s","Song %s is left out: %s"},
    {"Se omite el icono %s: %s","Icon %s is left out: %s"},
    {"Se omite el chart %s: %s","Chart %s is left out: %s"},
    {"Se omite el recurso %s: %s","Resource %s is left out: %s"},
    {"no está en images/, music/ ni sounds/ del mod.","it is not under the mod's images/, music/ or sounds/."},
    {"Codename necesita OGG Vorbis.","Codename needs OGG Vorbis."},
    {"ya no está en el disco.","it is no longer on disk."},
    {"Hay más de una canción %s: esta se exporta como %s.","There is more than one song %s: this one is exported as %s."},
    {"Hay más de un %s %s: este se exporta como %s.","There is more than one %s %s: this one is exported as %s."},
    {"Archivo de origen vacío, no se archiva: %s","Empty source file, not archived: %s"},
    {"imagen ausente en el mod, se omite: %s","image missing from the mod, left out: %s"},
    {"atlas ausente en el mod, se omite: %s","atlas missing from the mod, left out: %s"},
    {"la imagen que declara (%s) no está en el mod; tampoco carga en Psych. Elígela a mano si existe con otro nombre.","the image it declares (%s) is not in the mod; it does not load in Psych either. Pick it by hand if it exists under another name."},
    {"este stage no trae `objects`: sus %s sprites salen de su script, con su orden y su scroll","this stage has no `objects`: its %s sprites come from its script, with their order and scroll"},
    {"vista estatica de onCreate/onCreatePost; callbacks, modcharts y expresiones dinamicas requieren porteo, no se ejecutan","static view of onCreate/onCreatePost; callbacks, modcharts and dynamic expressions need porting and are not run"},
    {"STAGE PARCIAL: condiciones sin contexto o bucles dinamicos omitidos. No se mezclaron ramas excluyentes. El original se conserva para portearlo.","PARTIAL STAGE: conditions without context or dynamic loops left out. Exclusive branches were not mixed. The original is kept for porting."},
    {"Animate compatible: poses anidadas resueltas, original conservado","Animate adapted: nested poses resolved, original kept"},
    {"%s ajustes del script no tienen equivalente en el XML y se han dejado fuera","%s stage script settings have no XML equivalent and were left out"},
    {"se omite la dificultad %s: el mod no trae su instrumental.","difficulty %s is left out: the mod has no instrumental for it."},
    {"ninguna dificultad exportable (sin instrumental o con nombre inválido).","no exportable difficulty (no instrumental or invalid name)."},
    {"evento sin equivalente: %s (%s)","event without an equivalent: %s (%s)"},
    {"los tipos de nota custom requieren revisar su comportamiento.","custom note types need their behavior reviewed."},
    {"Lua conservado sin activar.","Lua archived without activation."},
    {"escenario vacío; no se puede importar como completo.","empty stage; cannot import it as a complete stage."},
    {"lógica dinámica pendiente; se importa el decorado declarativo.","dynamic logic remains to be ported; importing the declarative scenery."},
    {"Imagen de escenario sin resolver: %s · %s","Unresolved stage image: %s · %s"}
};
bool match(const std::string& value,const std::string& pattern,std::vector<std::string>& fields) {
    size_t at=0,pos=0;
    while(true) {
        auto token=pattern.find("%s",pos);
        auto literal=pattern.substr(pos,token==std::string::npos?token:token-pos);
        if(value.compare(at,literal.size(),literal)!=0) return false;
        at+=literal.size(); if(token==std::string::npos) return at==value.size();
        pos=token+2; auto next=pattern.find("%s",pos); auto delimiter=pattern.substr(pos,next==std::string::npos?next:next-pos);
        if(delimiter.empty()) { if(next!=std::string::npos) return false; fields.push_back(value.substr(at)); return true; }
        auto end=value.find(delimiter,at); if(end==std::string::npos) return false;
        fields.push_back(value.substr(at,end-at)); at=end;
    }
}
std::string fill(const std::string& pattern,const std::vector<std::string>& fields) {
    std::string out; size_t at=0,index=0;
    while(true) { auto token=pattern.find("%s",at); out+=pattern.substr(at,token==std::string::npos?token:token-at); if(token==std::string::npos) return out; if(index<fields.size()) out+=fields[index++]; at=token+2; }
}
}
std::string message(const std::string& original) {
    // Match complete known prose, never substitute words inside captured data.
    // A captured field may itself be a known message ("Se omite el personaje
    // x: <reason>"): it is translated too; ids and paths match nothing.
    for(const auto& entry:entries) {
        std::vector<std::string> fields;
        bool hit=match(original,entry.first,fields);
        if(!hit) { fields.clear(); hit=match(original,entry.second,fields); }
        if(hit) { for(auto& field:fields) field=message(field); return fill(language()==Language::English?entry.second:entry.first,fields); }
    }
    // "<id>: <message>": notices name their song, character or stage first.
    auto colon=original.find(": ");
    if(colon!=std::string::npos && colon>0 && colon<80) {
        auto rest=message(original.substr(colon+2));
        if(rest!=original.substr(colon+2)) return original.substr(0,colon+2)+rest;
    }
    return original;
}
std::string reportText(const std::string& original) {
    std::string result; size_t at=0;
    while(at<original.size()) {
        auto end=original.find('\n',at); auto line=original.substr(at,end==std::string::npos?end:end-at);
        if(line.rfind("BLOQUEADO: ",0)==0) line=std::string(ui("BLOQUEADO: ","BLOCKED: "))+message(line.substr(11));
        else if(line.rfind("Aviso: ",0)==0) line=std::string(ui("Aviso: ","Warning: "))+message(line.substr(7));
        else if(line.rfind("Nota: ",0)==0) line=std::string(ui("Nota: ","Note: "))+message(line.substr(6));
        else if(line.rfind("Destino: ",0)==0) line=std::string(ui("Destino: ","Destination: "))+line.substr(9);
        else if(line.rfind("Archivos (",0)==0) line=std::string(ui("Archivos (","Files ("))+line.substr(10);
        else if(line.rfind("Eventos: ",0)==0) line=std::string(ui("Eventos: ","Events: "))+line.substr(9);
        else if(line.rfind("Convertido: ",0)==0) line=std::string(ui("Convertido: ","Converted: "))+line.substr(12);
        else if(line.rfind("Sin equivalente: ",0)==0) line=std::string(ui("Sin equivalente: ","No equivalent: "))+line.substr(17);
        else line=message(line);
        result+=line; if(end==std::string::npos) break; result+='\n'; at=end+1;
    } return result;
}
}
