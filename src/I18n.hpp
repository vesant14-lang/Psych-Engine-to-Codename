#pragma once
#include <filesystem>
#include <string>
#include <map>
#include <vector>

namespace pi {
enum class Language { Spanish, English };
Language language();
void setLanguage(Language value);
bool loadLanguage(const std::filesystem::path& path);
bool saveLanguage(const std::filesystem::path& path);
// Only interface captions and diagnostic prose go through this adapter.
// Resource names, editable values, paths and serialized charts stay untouched.
inline const char* ui(const char* es,const char* en) {
    return language()==Language::English?en:es;
}
inline std::string uiId(const char* es,const char* en,const char* id) {
    return std::string(ui(es,en))+"###"+id;
}
std::string message(const std::string& original);
std::string reportText(const std::string& original);
}
