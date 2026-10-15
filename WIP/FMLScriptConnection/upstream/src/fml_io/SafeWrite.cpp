#include "SafeWrite.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace fml {
namespace {

// Escapado minimo para JSON. Estas cadenas son rutas y etiquetas cortas, no
// texto libre, asi que no hace falta arrastrar una libreria entera.
std::string esc(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        if      (c == '\\') out += "\\\\";
        else if (c == '"')  out += "\\\"";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

std::string unesc(const std::string& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            ++i;
            out += (v[i] == 'n') ? '\n' : v[i];
        } else {
            out += v[i];
        }
    }
    return out;
}

std::string field(const std::string& line, const std::string& key) {
    const std::string pat = "\"" + key + "\":\"";
    const size_t a = line.find(pat);
    if (a == std::string::npos) return {};
    size_t i = a + pat.size();
    std::string out;
    while (i < line.size() && line[i] != '"') {
        if (line[i] == '\\' && i + 1 < line.size()) { out += line[i]; ++i; }
        out += line[i];
        ++i;
    }
    return unesc(out);
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool readAllChecked(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

std::string uniqueToken() {
    static std::atomic<unsigned long long> sequence{0};
    const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << std::hex << ticks << '-' << sequence.fetch_add(1, std::memory_order_relaxed);
    return out.str();
}

bool relativeInside(const fs::path& root, const fs::path& candidate,
                    fs::path& absoluteCandidate, fs::path& relative,
                    std::error_code& ec) {
    ec.clear();
    fs::path absoluteRoot = fs::absolute(root, ec);
    if (ec) return false;
    absoluteRoot = fs::weakly_canonical(absoluteRoot, ec);
    if (ec) return false;

    absoluteCandidate = candidate.is_absolute() ? candidate : absoluteRoot / candidate;
    absoluteCandidate = fs::absolute(absoluteCandidate, ec).lexically_normal();
    if (ec) return false;

    // La STL incluida con VS2019 puede hacer fallar weakly_canonical() cuando
    // el archivo final (o varias carpetas) aun no existe. Resolver primero el
    // ancestro existente conserva la defensa contra junctions/symlinks y luego
    // permite anexar lexicalmente la cola nueva. Esto habilita crear overrides
    // nuevos sin confundirlos con una salida de la raiz editable.
    fs::path existing = absoluteCandidate;
    std::vector<fs::path> missingTail;
    while (!fs::exists(existing, ec)) {
        if (ec || !existing.has_parent_path() || existing.parent_path() == existing)
            return false;
        missingTail.push_back(existing.filename());
        existing = existing.parent_path();
    }
    existing = fs::weakly_canonical(existing, ec);
    if (ec) return false;
    absoluteCandidate = existing;
    for (auto it = missingTail.rbegin(); it != missingTail.rend(); ++it)
        absoluteCandidate /= *it;
    absoluteCandidate = absoluteCandidate.lexically_normal();

    // std::filesystem::relative de MSVC puede devolver error para un archivo
    // final que aun no existe, aunque weakly_canonical haya resuelto bien su
    // padre. Comparar componentes canonicos evita ese falso rechazo y mantiene
    // la defensa contra `..` y junctions que escapan de la raiz.
    auto rootIt = absoluteRoot.begin();
    auto candidateIt = absoluteCandidate.begin();
    for (; rootIt != absoluteRoot.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == absoluteCandidate.end()) return false;
        std::string rootPart = rootIt->generic_u8string();
        std::string candidatePart = candidateIt->generic_u8string();
#ifdef _WIN32
        std::transform(rootPart.begin(), rootPart.end(), rootPart.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::transform(candidatePart.begin(), candidatePart.end(), candidatePart.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        if (rootPart != candidatePart) return false;
    }
    if (candidateIt == absoluteCandidate.end()) return false;  // no escribir la raiz misma
    relative.clear();
    for (; candidateIt != absoluteCandidate.end(); ++candidateIt)
        relative /= *candidateIt;
    return !relative.empty() && !relative.is_absolute();
}

std::string pathKey(const fs::path& path) {
    std::string key = path.lexically_normal().generic_u8string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    return key;
}

bool metadataInside(const fs::path& root,const fs::path& backup,DiagnosticSink& sink) {
    std::error_code ec;const auto absoluteRoot=fs::absolute(root,ec);if(ec)return false;
    const auto absoluteBackup=fs::absolute(backup,ec);if(ec)return false;
    for(const auto& candidate:{absoluteRoot/".fml"/"history.jsonl",absoluteRoot/".fml"/"transactions"/".guard",absoluteBackup/".guard"}) {
        fs::path resolved,relative;
        if(!relativeInside(absoluteRoot,candidate,resolved,relative,ec)) {
            sink.error("FML-SEC-504","backup or history path resolves outside the editable root",candidate.u8string());return false;
        }
    }
    return true;
}

bool relativeLexicallyInside(const fs::path& root, const fs::path& candidate,
                             fs::path& absoluteCandidate, fs::path& relative,
                             std::error_code& ec) {
    ec.clear();
    fs::path absoluteRoot = fs::absolute(root, ec).lexically_normal();
    if (ec) return false;
    absoluteCandidate = fs::absolute(candidate, ec).lexically_normal();
    if (ec) return false;

    auto rootIt = absoluteRoot.begin();
    auto candidateIt = absoluteCandidate.begin();
    for (; rootIt != absoluteRoot.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == absoluteCandidate.end()) return false;
        std::string rootPart = rootIt->generic_u8string();
        std::string candidatePart = candidateIt->generic_u8string();
#ifdef _WIN32
        std::transform(rootPart.begin(), rootPart.end(), rootPart.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::transform(candidatePart.begin(), candidatePart.end(), candidatePart.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        if (rootPart != candidatePart) return false;
    }
    if (candidateIt == absoluteCandidate.end()) return false;
    relative.clear();
    for (; candidateIt != absoluteCandidate.end(); ++candidateIt) relative /= *candidateIt;
    return !relative.empty() && !relative.is_absolute();
}

bool writeFile(const fs::path& path, const std::string& content,
               DiagnosticSink& sink, const char* openCode, const char* writeCode) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        sink.error(openCode, "no se pudo abrir el archivo temporal", path.u8string());
        return false;
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.flush();
    if (!file) {
        sink.error(writeCode, "fallo al escribir el archivo temporal", path.u8string());
        file.close();
        std::error_code ignored;
        fs::remove(path, ignored);
        return false;
    }
    file.close();
    return true;
}

// El reemplazo nunca elimina primero el destino. ReplaceFileW conserva el
// original si falla; rename tiene esa misma garantia en POSIX.
bool atomicReplace(const fs::path& temporary, const fs::path& target,
                   std::error_code& ec) {
    ec.clear();
#ifdef _WIN32
    std::error_code existsError;
    const bool exists = fs::exists(target, existsError);
    if (existsError) { ec = existsError; return false; }
    if (exists) {
        if (::ReplaceFileW(target.c_str(), temporary.c_str(), nullptr,
                           REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
            return true;
    } else if (::MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
    return false;
#else
    fs::rename(temporary, target, ec);
    return !ec;
#endif
}

void appendHistoryLine(std::ofstream& f, const fs::path& rel,
                       const fs::path& relBackup, const std::string& detail) {
    f << "{\"stamp\":\""    << esc(SafeWriter::timestamp())
      << "\",\"file\":\""   << esc(rel.generic_u8string())
      << "\",\"backup\":\"" << esc(relBackup.generic_u8string())
      << "\",\"detail\":\"" << esc(detail) << "\"}" << "\n";
}

void appendHistoryLine(const fs::path& root, const fs::path& rel,
                       const fs::path& relBackup, const std::string& detail) {
    std::error_code ec;
    const fs::path dir = root / ".fml";
    fs::create_directories(dir, ec);
    std::ofstream f(dir / "history.jsonl", std::ios::app | std::ios::binary);
    if (f) appendHistoryLine(f, rel, relBackup, detail);
}

// Una raiz editable ya creada y canonizada no cambia durante la sesion. Cada
// escritura segura volvia a crearla y a canonizarla -una consulta al disco por
// componente de la ruta-, y una importacion escribe cientos de archivos.
// Tambien se recuerda que su `.fml/` ya existe.
struct PreparedRoot {
    fs::path canonical;
    bool     historyDir = false;
};
std::mutex& preparedRootsMutex() {
    static std::mutex mutex;
    return mutex;
}
std::unordered_map<std::string, PreparedRoot>& preparedRoots() {
    static std::unordered_map<std::string, PreparedRoot> roots;
    return roots;
}

unsigned long long fnv1a64(const std::string& value) {
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

void appendRecoveryField(std::string& out, const std::string& value) {
    out += std::to_string(value.size());
    out += '\n';
    out.append(value.data(), value.size());
    out += '\n';
}

}  // namespace

std::string SafeWriter::timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d-%02d%02d%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

SafeWriter::SafeWriter(fs::path projectRoot, std::string sessionStamp)
    : m_root(std::move(projectRoot)), m_sessionStamp(std::move(sessionStamp)) {
    m_backupDir = m_root / ".fml" / "backups" / fs::u8path(m_sessionStamp);
}

bool SafeWriter::write(const fs::path& target, const std::string& content,
                       DiagnosticSink& sink) {
    SafeWriteTransaction transaction(m_root, m_sessionStamp);
    transaction.add(target, content, m_detail);
    if (!transaction.commit(sink)) return false;
    m_written += transaction.filesWritten();
    return true;
}

SafeWriteTransaction::SafeWriteTransaction(fs::path projectRoot,
                                           std::string sessionStamp)
    : m_root(std::move(projectRoot)), m_sessionStamp(std::move(sessionStamp)) {}

void SafeWriteTransaction::add(fs::path target, std::string content,
                               std::string detail) {
    m_pending.push_back({std::move(target), std::move(content), std::move(detail), {}});
}

void SafeWriteTransaction::addCopy(fs::path target,fs::path source,std::string detail) {
    m_pending.push_back({std::move(target), {}, std::move(detail), std::move(source)});
}
void SafeWriteTransaction::expectUnchanged(fs::path target,std::optional<std::string> previous) {
    m_conditions.emplace_back(std::move(target),std::move(previous));
}
void SafeWriteTransaction::expectSourceUnchanged(fs::path source,std::uintmax_t size,fs::file_time_type modified) {
    m_sourceConditions.push_back({std::move(source),size,modified});
}

bool SafeWriteTransaction::commit(DiagnosticSink& sink) {
    if (m_pending.empty()) return true;

    struct PreparedWrite {
        PendingWrite* pending = nullptr;
        fs::path target;
        fs::path relative;
        fs::path temporary;
        fs::path rollback;
        fs::path permanentBackup;
        bool existed = false;
    };

    std::error_code ec;
    fs::path absoluteRoot = fs::absolute(m_root, ec);
    if (ec) {
        sink.error("FML-SAVE-001", "no se pudo resolver la raiz editable: " + ec.message(),
                   m_root.u8string());
        return false;
    }
    const std::string rootKey = absoluteRoot.lexically_normal().u8string();
    bool rootKnown = false, historyDirReady = false;
    {
        std::lock_guard<std::mutex> lock(preparedRootsMutex());
        const auto known = preparedRoots().find(rootKey);
        if (known != preparedRoots().end()) {
            absoluteRoot = known->second.canonical;
            historyDirReady = known->second.historyDir;
            rootKnown = true;
        }
    }
    if (!rootKnown) {
        fs::create_directories(absoluteRoot, ec);
        if (ec) {
            sink.error("FML-SAVE-002", "no se pudo preparar la raiz editable: " + ec.message(),
                       absoluteRoot.u8string());
            return false;
        }
        absoluteRoot = fs::weakly_canonical(absoluteRoot, ec);
        if (ec) {
            sink.error("FML-SAVE-003", "no se pudo canonizar la raiz editable: " + ec.message(),
                       absoluteRoot.u8string());
            return false;
        }
        std::lock_guard<std::mutex> lock(preparedRootsMutex());
        preparedRoots()[rootKey].canonical = absoluteRoot;
    }

    const std::string token = uniqueToken();
    const fs::path transactionDir = absoluteRoot / ".fml" / "transactions" /
                                    fs::u8path(token);
    const fs::path backupDir = absoluteRoot / ".fml" / "backups" /
                               fs::u8path(m_sessionStamp);
    if(!metadataInside(absoluteRoot,backupDir,sink))return false;
    std::vector<PreparedWrite> prepared;
    prepared.reserve(m_pending.size());
    std::unordered_set<std::string> destinations;
    std::vector<fs::path> createdDirectories;

    auto cleanup = [&] {
        std::error_code ignored;
        for (const PreparedWrite& item : prepared) fs::remove(item.temporary, ignored);
        fs::remove_all(transactionDir, ignored);
        for (auto at=createdDirectories.rbegin();at!=createdDirectories.rend();++at) fs::remove(*at,ignored);
    };

    auto unchanged = [&] {
        for(const auto& condition:m_sourceConditions) {
            std::error_code checkError;const auto& source=condition.source;
            const auto size=fs::file_size(source,checkError);if(checkError || size!=condition.size)return false;
            const auto modified=fs::last_write_time(source,checkError);if(checkError || modified!=condition.modified)return false;
        }
        for(const auto& condition:m_conditions) {
            fs::path target,relative;std::error_code checkError;
            if(!relativeInside(absoluteRoot,condition.first,target,relative,checkError))return false;
            bool exists=fs::exists(target,checkError);if(checkError)return false;
            if(exists!=condition.second.has_value())return false;
            if(exists){std::string current;if(!readAllChecked(target,current) || current!=*condition.second)return false;}
        }
        return true;
    };
    if(!unchanged()){sink.error("FML-SAVE-018","destination changed; prepare the export again",absoluteRoot.u8string());cleanup();return false;}
    // Preflight: rutas, duplicados, temporales y copias de rollback. No se
    // reemplaza ningun destino antes de completar esta fase.
    for (size_t i = 0; i < m_pending.size(); ++i) {
        PendingWrite& pending = m_pending[i];
        PreparedWrite item;
        item.pending = &pending;
        if (!relativeInside(absoluteRoot, pending.target, item.target, item.relative, ec)) {
            sink.error("FML-SAVE-004", "destino fuera de la raiz editable",
                       pending.target.u8string());
            cleanup();
            return false;
        }
        if (!destinations.insert(pathKey(item.target)).second) {
            sink.error("FML-SAVE-005", "el mismo archivo aparece dos veces en la transaccion",
                       item.target.u8string());
            cleanup();
            return false;
        }

        std::vector<fs::path> missing;
        for (auto dir=item.target.parent_path();dir!=absoluteRoot && !fs::exists(dir,ec);dir=dir.parent_path()) {
            if(ec || dir==dir.parent_path()) break;
            missing.push_back(dir);
        }
        for(auto at=missing.rbegin();at!=missing.rend();++at)createdDirectories.push_back(*at);
        fs::create_directories(item.target.parent_path(), ec);
        if (ec) {
            sink.error("FML-SAVE-006", "no se pudo crear la carpeta destino: " + ec.message(),
                       item.target.parent_path().u8string());
            cleanup();
            return false;
        }
        item.existed = fs::exists(item.target, ec);
        if (ec || (item.existed && !fs::is_regular_file(item.target, ec))) {
            sink.error("FML-SAVE-007", "el destino no es un archivo regular",
                       item.target.u8string());
            cleanup();
            return false;
        }

        item.temporary = item.target;
        item.temporary += fs::u8path(".fml-tmp-" + token + "-" + std::to_string(i));
        bool staged = false;
        if (!pending.source.empty()) {
            fs::copy_file(pending.source,item.temporary,fs::copy_options::none,ec);
            staged = !ec;
            if(!staged)sink.error("FML-SAVE-008","could not stage source: "+ec.message(),pending.source.u8string());
        } else staged = writeFile(item.temporary, pending.content, sink,"FML-SAVE-008", "FML-SAVE-009");
        if (!staged) {
            fs::remove(item.temporary,ec);
            cleanup();
            return false;
        }

        if (item.existed) {
            item.rollback = transactionDir / item.relative;
            fs::create_directories(item.rollback.parent_path(), ec);
            if (ec) {
                sink.error("FML-SAVE-010", "no se pudo preparar el rollback: " + ec.message(),
                           item.rollback.parent_path().u8string());
                prepared.push_back(std::move(item));
                cleanup();
                return false;
            }
            fs::copy_file(item.target, item.rollback, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                sink.error("FML-SAVE-011", "no se pudo copiar el estado previo: " + ec.message(),
                           item.target.u8string());
                prepared.push_back(std::move(item));
                cleanup();
                return false;
            }

            item.permanentBackup = backupDir / item.relative;
            if (!fs::exists(item.permanentBackup, ec)) {
                fs::create_directories(item.permanentBackup.parent_path(), ec);
                if (!ec)
                    fs::copy_file(item.rollback, item.permanentBackup,
                                  fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    sink.error("FML-SAVE-012", "no se pudo crear la copia permanente: " +
                               ec.message(), item.permanentBackup.u8string());
                    prepared.push_back(std::move(item));
                    cleanup();
                    return false;
                }
            }
        }
        prepared.push_back(std::move(item));
    }

    if(!unchanged() || !metadataInside(absoluteRoot,backupDir,sink)){sink.error("FML-SAVE-018","destination changed while preparing; export cancelled",absoluteRoot.u8string());cleanup();return false;}
    size_t committed = 0;
    for (; committed < prepared.size(); ++committed) {
        PreparedWrite& item = prepared[committed];
        if (atomicReplace(item.temporary, item.target, ec)) continue;

        sink.error("FML-SAVE-013", "fallo el commit; se inicia rollback: " + ec.message(),
                   item.target.u8string());
        bool rollbackOk = true;
        for (size_t r = committed; r > 0; --r) {
            PreparedWrite& previous = prepared[r - 1];
            if (!previous.existed) {
                std::error_code removeError;
                fs::remove(previous.target, removeError);
                if (removeError) {
                    rollbackOk = false;
                    sink.error("FML-SAVE-014", "no se pudo retirar un archivo nuevo durante rollback: " +
                               removeError.message(), previous.target.u8string());
                }
                continue;
            }

            fs::path restoreTemp = previous.target;
            restoreTemp += fs::u8path(".fml-rollback-" + token);
            std::error_code restoreError;
            fs::copy_file(previous.rollback, restoreTemp,
                          fs::copy_options::overwrite_existing, restoreError);
            if (!restoreError) atomicReplace(restoreTemp, previous.target, restoreError);
            if (restoreError) {
                rollbackOk = false;
                sink.error("FML-SAVE-015", "no se pudo restaurar durante rollback: " +
                           restoreError.message(), previous.target.u8string());
                fs::remove(restoreTemp, restoreError);
            }
        }
        cleanup();
        if (rollbackOk)
            sink.info("FML-SAVE-016", "rollback completado; el proyecto conserva su estado anterior",
                      absoluteRoot.u8string());
        return false;
    }

    {
        // Un historial abierto una vez por transaccion, no una vez por archivo.
        const fs::path historyDir = absoluteRoot / ".fml";
        auto openHistory = [&] {
            return std::ofstream(historyDir / "history.jsonl", std::ios::app | std::ios::binary);
        };
        if (!historyDirReady) fs::create_directories(historyDir, ec);
        ec.clear();
        std::ofstream history = openHistory();
        if (!history && historyDirReady) {
            // Recordado como creado, pero alguien lo borro durante la sesion.
            fs::create_directories(historyDir, ec);
            ec.clear();
            history = openHistory();
        }
        if (history && !historyDirReady) {
            std::lock_guard<std::mutex> lock(preparedRootsMutex());
            preparedRoots()[rootKey].historyDir = true;
        }
        if (history) for (const PreparedWrite& item : prepared) {
            fs::path relativeBackup;
            if (item.existed) {
                relativeBackup = fs::relative(item.permanentBackup, absoluteRoot, ec);
                if (ec) { ec.clear(); relativeBackup = item.permanentBackup; }
            }
            appendHistoryLine(history, item.relative, relativeBackup,
                              item.pending->detail);
        }
    }
    m_written += prepared.size();
    cleanup();
    sink.info("FML-SAVE-017", "transaccion de guardado completada (" +
              std::to_string(prepared.size()) + " archivos)", absoluteRoot.u8string());
    return true;
}

bool SafeWriter::removeFile(const fs::path& target, DiagnosticSink& sink) {
    if(!metadataInside(m_root,m_backupDir,sink))return false;
    std::error_code ec;
    fs::path absoluteTarget, rel;
    if (!relativeInside(m_root, target, absoluteTarget, rel, ec)) {
        sink.error("FML-SEC-501", "se rechazo borrar fuera de la raiz editable",
                   target.u8string());
        return false;
    }
    if (!fs::exists(absoluteTarget, ec)) return true;   // ya no estaba: nada que hacer

    const fs::path backup = m_backupDir / rel;

    if (!fs::exists(backup, ec)) {
        fs::create_directories(backup.parent_path(), ec);
        if (ec) {
            sink.error("FML-5020", "no se pudo crear la carpeta de copias: " + ec.message(),
                       backup.parent_path().u8string());
            return false;
        }
        fs::copy_file(absoluteTarget, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            sink.error("FML-5021", "no se pudo respaldar antes de borrar: " + ec.message(),
                       absoluteTarget.u8string());
            return false;   // sin copia NO se borra
        }
    }

    fs::remove(absoluteTarget, ec);
    if (ec) {
        sink.error("FML-5022", "no se pudo borrar: " + ec.message(), absoluteTarget.u8string());
        return false;
    }

    fs::path relBackup = fs::relative(backup, m_root, ec);
    if (ec) { ec.clear(); relBackup = backup; }
    appendHistory(rel, relBackup);
    sink.info("FML-5023", "borrado con copia de seguridad", backup.u8string());
    return true;
}

void SafeWriter::appendHistory(const fs::path& rel, const fs::path& relBackup) {
    appendHistoryLine(m_root, rel, relBackup, m_detail);
}

std::vector<SafeWriter::HistoryEntry> SafeWriter::readHistory(const fs::path& root,
                                                              size_t maxEntries) {
    std::vector<HistoryEntry> out;
    std::ifstream f(root / ".fml" / "history.jsonl", std::ios::binary);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("\"file\"") == std::string::npos) continue;
        HistoryEntry e;
        e.stamp  = field(line, "stamp");
        e.file   = field(line, "file");
        e.backup = field(line, "backup");
        e.detail = field(line, "detail");
        if (!e.file.empty()) out.push_back(std::move(e));
    }
    std::reverse(out.begin(), out.end());   // mas recientes primero
    if (out.size() > maxEntries) out.resize(maxEntries);
    return out;
}

bool SafeWriter::restore(const fs::path& root, const HistoryEntry& e, DiagnosticSink& sink) {
    std::error_code ec;
    fs::path target, targetRelative;
    if (!relativeInside(root, root / fs::u8path(e.file), target, targetRelative, ec)) {
        sink.error("FML-SEC-502", "el historial apunta fuera de la raiz editable", e.file);
        return false;
    }

    // Una entrada sin backup representa un archivo que no existia antes del
    // guardado. Restaurarla significa retirarlo de forma reversible.
    if (e.backup.empty()) {
        SafeWriter writer(root, timestamp() + "-restore-created");
        writer.setDetail("restaurar creacion de " + e.stamp);
        return writer.removeFile(target, sink);
    }

    fs::path backup, backupRelative;
    if (!relativeInside(root, root / fs::u8path(e.backup), backup, backupRelative, ec)) {
        sink.error("FML-SEC-503", "la copia del historial apunta fuera de la raiz editable",
                   e.backup);
        return false;
    }

    if (!fs::exists(backup, ec)) {
        sink.error("FML-5010", "la copia de seguridad ya no existe", backup.u8string());
        return false;
    }

    // Antes de restaurar se guarda el estado ACTUAL: revertir tambien es
    // reversible. Nunca se pierde trabajo por deshacer de mas.
    std::string current;
    if (fs::exists(target, ec) && readAllChecked(target, current)) {
        SafeWriter pre(root, timestamp() + "-pre-restore");
        pre.setDetail("estado previo a restaurar");
        pre.write(target, current, sink);
    }

    std::string content;
    if (!readAllChecked(backup, content)) {
        sink.error("FML-5011", "no se pudo leer la copia", backup.u8string());
        return false;
    }

    SafeWriter w(root, timestamp() + "-restore");
    w.setDetail("restaurado desde " + e.stamp);
    return w.write(target, content, sink);
}

bool writeRecoverySnapshot(const fs::path& recoveryRoot,
                           const std::string& sessionStamp,
                           const std::string& logicalKey,
                           const std::string& projectRoot,
                           const std::vector<RecoveryDocument>& documents,
                           DiagnosticSink& sink,
                           fs::path* writtenPath) {
    if (documents.empty()) {
        sink.error("FML-RECOVERY-001", "no hay documentos para recuperar");
        return false;
    }

    std::error_code ec;
    fs::create_directories(recoveryRoot, ec);
    if (ec) {
        sink.error("FML-RECOVERY-002", "no se pudo crear la carpeta de recuperacion: " +
                   ec.message(), recoveryRoot.u8string());
        return false;
    }

    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << fnv1a64(logicalKey);
    const fs::path sessionDir = recoveryRoot / fs::u8path(sessionStamp);
    fs::create_directories(sessionDir, ec);
    if (ec) {
        sink.error("FML-RECOVERY-003", "no se pudo preparar la sesion de recuperacion: " +
                   ec.message(), sessionDir.u8string());
        return false;
    }
    const fs::path target = sessionDir / fs::u8path(name.str() + ".fml-recovery");
    fs::path absoluteTarget, relative;
    // recoveryRoot lo crea la propia aplicacion fuera del mod. Se usa una
    // comprobacion lexical porque algunos entornos restringidos de Windows
    // niegan weakly_canonical sobre LOCALAPPDATA aunque permitan escribir ahi.
    if (!relativeLexicallyInside(recoveryRoot, target, absoluteTarget, relative, ec)) {
        sink.error("FML-SEC-504", "se rechazo un snapshot fuera de recoveryRoot; root=" +
                   recoveryRoot.u8string() + ", resolved=" + absoluteTarget.u8string() +
                   (ec ? ", error=" + ec.message() : std::string()),
                   target.u8string());
        return false;
    }

    std::string archive = "FML-RECOVERY-V1\n";
    appendRecoveryField(archive, sessionStamp);
    appendRecoveryField(archive, logicalKey);
    appendRecoveryField(archive, projectRoot);
    archive += std::to_string(documents.size()) + "\n";
    for (const RecoveryDocument& document : documents) {
        appendRecoveryField(archive, document.virtualPath);
        appendRecoveryField(archive, document.content);
    }

    fs::path temporary = absoluteTarget;
    temporary += fs::u8path(".tmp-" + uniqueToken());
    if (!writeFile(temporary, archive, sink, "FML-RECOVERY-004", "FML-RECOVERY-005"))
        return false;
    if (!atomicReplace(temporary, absoluteTarget, ec)) {
        sink.error("FML-RECOVERY-006", "no se pudo publicar el snapshot: " + ec.message(),
                   absoluteTarget.u8string());
        fs::remove(temporary, ec);
        return false;
    }
    if (writtenPath) *writtenPath = absoluteTarget;
    sink.info("FML-RECOVERY-007", "snapshot de recuperacion actualizado",
              absoluteTarget.u8string());
    return true;
}

bool removeRecoverySnapshot(const fs::path& recoveryRoot, const fs::path& snapshot,
                            DiagnosticSink& sink) {
    if (snapshot.empty()) return true;
    std::error_code ec;
    fs::path absoluteSnapshot, relative;
    if (!relativeLexicallyInside(recoveryRoot, snapshot, absoluteSnapshot, relative, ec) ||
        absoluteSnapshot.extension() != ".fml-recovery") {
        sink.error("FML-SEC-505", "se rechazo borrar un snapshot fuera de recoveryRoot",
                   snapshot.u8string());
        return false;
    }
    if (!fs::exists(absoluteSnapshot, ec)) return true;
    fs::remove(absoluteSnapshot, ec);
    if (ec) {
        sink.warn("FML-RECOVERY-008", "no se pudo retirar el snapshot ya guardado: " +
                  ec.message(), absoluteSnapshot.u8string());
        return false;
    }
    return true;
}

}  // namespace fml
