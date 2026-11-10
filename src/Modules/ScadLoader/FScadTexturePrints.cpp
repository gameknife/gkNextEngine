#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/ScadLoader/FScadTexturePrints.h"

#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace Assets::Scad
{
    namespace TexturePrintsDetail
    {
        namespace fs = std::filesystem;

        struct FLibrary
        {
            bool loaded = false;
            std::string loadError;
            fs::file_time_type stamp{};
            std::chrono::steady_clock::time_point checked{};
            std::unordered_map<std::string, StmtPtr> functions;
            std::vector<std::string> names;
            std::map<std::string, std::shared_ptr<const FTextureGraph>, std::less<>> graphs;
        };

        std::mutex& LibraryMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        FLibrary& Library()
        {
            static FLibrary library;
            return library;
        }

        fs::path LoosePath()
        {
            return Utilities::FileHelper::GetRuntimeFilePath(kTexturePrintLibrary);
        }

        bool ReadLibrary(std::string& text)
        {
            std::vector<uint8_t> data;
            if (auto* package = Utilities::Package::FPackageFileSystem::TryGetInstance())
            {
                if (!package->LoadFile(kTexturePrintLibrary, data)) return false;
                text.assign(reinterpret_cast<const char*>(data.data()), data.size());
                return true;
            }
            std::ifstream file(LoosePath(), std::ios::binary);
            if (!file.is_open()) return false;
            text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }

        // (Re)loads the library when it is first needed or its file changed;
        // the file is stat'ed at most once a second.
        void Refresh(FLibrary& library)
        {
            const auto now = std::chrono::steady_clock::now();
            if (library.loaded && now - library.checked < std::chrono::seconds(1)) return;
            library.checked = now;
            std::error_code ec;
            const bool packaged = Utilities::Package::FPackageFileSystem::TryGetInstance() != nullptr;
            const fs::file_time_type stamp = packaged ? fs::file_time_type{} : fs::last_write_time(LoosePath(), ec);
            if (library.loaded && stamp == library.stamp) return;

            library = FLibrary{};
            library.loaded = true;
            library.checked = now;
            library.stamp = stamp;
            std::string text;
            if (!ReadLibrary(text))
            {
                library.loadError = fmt::format("cannot read {}", kTexturePrintLibrary);
                return;
            }
            std::vector<Token> tokens;
            Scope scope;
            std::string error;
            if (!ScadLexer::Tokenize(text, tokens, error) || !ScadParser::Parse(tokens, scope, error))
            {
                library.loadError = fmt::format("{}: {}", kTexturePrintLibrary, error);
                return;
            }
            for (const StmtPtr& statement : scope)
            {
                if (statement->kind != StmtKind::FunctionDef) continue;
                library.functions[statement->name] = statement;
                if (statement->name.rfind("tex_", 0) == 0) library.names.push_back(statement->name.substr(4));
            }
            std::sort(library.names.begin(), library.names.end());
        }

        bool IsPrintName(std::string_view name)
        {
            return !name.empty() && std::all_of(name.begin(), name.end(), [](char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            });
        }
    }

    using namespace TexturePrintsDetail;

    FTexExpr ToTexExpr(const Value& value)
    {
        FTexExpr e;
        switch (value.type)
        {
        case Value::Type::Number:
            e.kind = FTexExpr::EKind::Number;
            e.number = value.num;
            break;
        case Value::Type::Bool:
            e.kind = FTexExpr::EKind::Number;
            e.number = value.boolean ? 1.0 : 0.0;
            break;
        case Value::Type::Str:
            e.kind = FTexExpr::EKind::String;
            e.text = value.str;
            break;
        case Value::Type::Vec:
            e.kind = FTexExpr::EKind::List;
            e.items.reserve(value.vec.size());
            for (const Value& item : value.vec) e.items.push_back(ToTexExpr(item));
            break;
        default:
            break;
        }
        return e;
    }

    std::shared_ptr<const FTextureGraph> ResolveTexturePrint(std::string_view name, std::string& error)
    {
        std::scoped_lock lock(LibraryMutex());
        FLibrary& library = Library();
        Refresh(library);
        if (const auto found = library.graphs.find(name); found != library.graphs.end()) return found->second;
        if (!library.loadError.empty())
        {
            error = library.loadError;
            return nullptr;
        }
        const std::string function = "tex_" + std::string(name);
        if (!IsPrintName(name) || !library.functions.contains(function))
        {
            std::string known;
            for (const std::string& n : library.names) known += (known.empty() ? "" : ", ") + n;
            error = fmt::format("unknown texture print '{}' (known: {}; add function {}() to {})", name, known, function,
                                kTexturePrintLibrary);
            return nullptr;
        }

        // Evaluate `print = tex_<name>();` against the library's functions and
        // read the value back from the top-level bindings.
        const std::string probe = "__gk_texture_print = " + function + "();";
        std::vector<Token> tokens;
        Scope top;
        std::string parseError;
        if (!ScadLexer::Tokenize(probe, tokens, parseError) || !ScadParser::Parse(tokens, top, parseError))
        {
            error = parseError;
            return nullptr;
        }
        SceneEvalResult result;
        std::string evalError;
        ScadEvaluator::EvaluateScene(top, {}, library.functions, ScadLoadOptions{}, result, evalError);
        const auto value = result.topLevelVariables.find("__gk_texture_print");
        if (value == result.topLevelVariables.end())
        {
            error = fmt::format("{}() did not evaluate: {}", function, evalError);
            return nullptr;
        }
        std::string graphError;
        auto graph = FTextureGraph::Compile(ToTexExpr(value->second), graphError);
        if (!graph)
        {
            error = fmt::format("{}(): {}", function, graphError);
            return nullptr;
        }
        library.graphs.emplace(std::string(name), graph);
        return graph;
    }

    std::vector<std::string> TexturePrintNames()
    {
        std::scoped_lock lock(LibraryMutex());
        FLibrary& library = Library();
        Refresh(library);
        return library.names;
    }

    std::shared_ptr<FProceduralTexture> MakeTexturePrint(std::string_view name, std::string& error)
    {
        auto graph = ResolveTexturePrint(name, error);
        if (!graph) return nullptr;
        auto texture = std::make_shared<FProceduralTexture>();
        texture->pattern = std::string(name);
        texture->graph = std::move(graph);
        return texture;
    }
}
