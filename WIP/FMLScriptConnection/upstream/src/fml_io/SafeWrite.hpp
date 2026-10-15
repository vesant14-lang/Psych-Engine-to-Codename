// fml_io — Escritura segura a disco.
//
// DESIGN §5: Funkin Mod Lab nunca debe destruir el mod original.
//
//   1. Copia de seguridad ANTES de la primera modificacion de cada archivo,
//      en .fml/backups/<timestamp>/<ruta relativa>.
//   2. Escritura atomica: se escribe a .tmp y se renombra. Un corte de luz a
//      mitad deja el original intacto, nunca un archivo a medias.
#pragma once

#include "../fml_core/Diagnostics.hpp"

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace fml {

class SafeWriter {
public:
    // `projectRoot` es donde vive la carpeta .fml/. `sessionStamp` agrupa todas
    // las copias de una misma sesion en la misma carpeta.
    SafeWriter(std::filesystem::path projectRoot, std::string sessionStamp);

    // Crea la copia de seguridad si es la primera vez que se toca el archivo,
    // y escribe de forma atomica. Devuelve false y anota el motivo si falla.
    bool write(const std::filesystem::path& target, const std::string& content,
               DiagnosticSink& sink);

    // Borra un archivo DESPUES de respaldarlo, y lo apunta en el historial.
    // Asi «borrar» sigue siendo reversible con `restore()`: la copia se vuelve a
    // escribir en la ruta original y el archivo reaparece. Sin copia no se
    // borra, igual que sin copia no se escribe (DESIGN §5).
    bool removeFile(const std::filesystem::path& target, DiagnosticSink& sink);

    const std::filesystem::path& backupDir() const { return m_backupDir; }
    size_t filesWritten() const { return m_written; }

    static std::string timestamp();

    // Historial persistente en .fml/history.jsonl: una linea por escritura, con
    // la ruta de la copia previa. Es lo que permite revertir ENTRE SESIONES, no
    // solo con Ctrl+Z dentro de la que esta abierta.
    struct HistoryEntry {
        std::string stamp, file, backup, detail;
    };
    static std::vector<HistoryEntry> readHistory(const std::filesystem::path& root,
                                                 size_t maxEntries = 50);
    static bool restore(const std::filesystem::path& root, const HistoryEntry& e,
                        DiagnosticSink& sink);

    void setDetail(std::string d) { m_detail = std::move(d); }

private:
    std::filesystem::path m_root;
    std::filesystem::path m_backupDir;
    std::string           m_sessionStamp;
    size_t                m_written = 0;
    std::string           m_detail;

    void appendHistory(const std::filesystem::path& target,
                       const std::filesystem::path& backup);
};

// Agrupa varios documentos en un solo commit. Todos los temporales y copias de
// rollback se preparan antes de reemplazar el primer destino. Si cualquier
// reemplazo falla, se restauran los que ya se habian aplicado.
class SafeWriteTransaction {
public:
    SafeWriteTransaction(std::filesystem::path projectRoot,
                         std::string sessionStamp);

    void add(std::filesystem::path target, std::string content,
             std::string detail = {});
    // Stream source files while preparing, without buffering an entire bundle.
    void addCopy(std::filesystem::path target,std::filesystem::path source,
                 std::string detail = {});
    void expectUnchanged(std::filesystem::path target,std::optional<std::string> previous);
    void expectSourceUnchanged(std::filesystem::path source,std::uintmax_t size,
                               std::filesystem::file_time_type modified);
    bool commit(DiagnosticSink& sink);

    bool empty() const { return m_pending.empty(); }
    size_t filesWritten() const { return m_written; }

private:
    struct PendingWrite {
        std::filesystem::path target;
        std::string content;
        std::string detail;
        std::filesystem::path source;
    };

    std::filesystem::path m_root;
    std::string m_sessionStamp;
    std::vector<PendingWrite> m_pending;
    std::vector<std::pair<std::filesystem::path,std::optional<std::string>>> m_conditions;
    struct SourceCondition {std::filesystem::path source;std::uintmax_t size;std::filesystem::file_time_type modified;};
    std::vector<SourceCondition> m_sourceConditions;
    size_t m_written = 0;
};

// El autosave no escribe el proyecto. Empaqueta todos los documentos dirty en
// un unico archivo atomico fuera del mod, para poder recuperarlos tras un crash.
struct RecoveryDocument {
    std::string virtualPath;
    std::string content;
};

bool writeRecoverySnapshot(const std::filesystem::path& recoveryRoot,
                           const std::string& sessionStamp,
                           const std::string& logicalKey,
                           const std::string& projectRoot,
                           const std::vector<RecoveryDocument>& documents,
                           DiagnosticSink& sink,
                           std::filesystem::path* writtenPath = nullptr);

bool removeRecoverySnapshot(const std::filesystem::path& recoveryRoot,
                            const std::filesystem::path& snapshot,
                            DiagnosticSink& sink);

}  // namespace fml
