#pragma once

#include "Core/Types.hpp"
#include "Core/Logger.hpp"
#include "Platform/FileSystem/VFS.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneSerializer.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace RL
{
    struct GameExportConfig
    {
        std::string GameTitle = "RLengine Game";
        std::string OutputDir = "ExportedGame";
        std::string EntrySceneName = "main.rlscene";
        u32 WindowWidth = 1280;
        u32 WindowHeight = 720;
        bool ForceDX11 = false;
        bool BinaryScene = true;
    };

    struct GameExportResult
    {
        bool Success = false;
        std::string ErrorMessage;
        std::filesystem::path ExecutablePath;
        std::filesystem::path ConfigPath;
        std::filesystem::path ScenePath;
        usize FilesCopied = 0;
    };

    class GameExporter
    {
    public:
        static GameExportResult ExportGame(const Scene& scene, const GameExportConfig& config)
        {
            GameExportResult result;

            if (config.OutputDir.empty())
            {
                result.ErrorMessage = "Output directory cannot be empty.";
                return result;
            }

            std::error_code ec;
            const std::filesystem::path outRoot = std::filesystem::weakly_canonical(
                std::filesystem::absolute(config.OutputDir, ec), ec);
            if (ec)
            {
                result.ErrorMessage = "Invalid output directory path: " + ec.message();
                return result;
            }

            const std::filesystem::path assetsOutDir = outRoot / "assets";
            const std::filesystem::path scenesOutDir = assetsOutDir / "scenes";
            const std::filesystem::path shadersOutDir = outRoot / "shaders";
            const std::filesystem::path scriptsOutDir = outRoot / "scripts";

            const std::array<std::filesystem::path, 5> requiredDirs = {
                outRoot,
                assetsOutDir,
                scenesOutDir,
                shadersOutDir,
                scriptsOutDir
            };

            for (const auto& dir : requiredDirs)
            {
                std::filesystem::create_directories(dir, ec);
                if (ec)
                {
                    result.ErrorMessage = "Failed to create directory '" + dir.string() + "': " + ec.message();
                    return result;
                }
            }

            wchar_t modulePathBuf[MAX_PATH]{};
            const DWORD pathLen = GetModuleFileNameW(nullptr, modulePathBuf, MAX_PATH);
            if (pathLen == 0 || pathLen >= MAX_PATH)
            {
                result.ErrorMessage = "Failed to locate current executable via GetModuleFileNameW.";
                return result;
            }

            const std::filesystem::path currentExePath(modulePathBuf);
            const std::filesystem::path currentExeDir = currentExePath.parent_path();

            std::string exeBaseName = SanitizeFileName(config.GameTitle);
            if (exeBaseName.empty())
            {
                exeBaseName = "RLengine7.2";
            }

            const std::filesystem::path destExePath = outRoot / (exeBaseName + ".exe");
            std::filesystem::copy_file(
                currentExePath,
                destExePath,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec)
            {
                result.ErrorMessage = "Failed to copy executable: " + ec.message();
                return result;
            }
            result.ExecutablePath = destExePath;
            ++result.FilesCopied;

            const std::array<const wchar_t*, 3> runtimeDlls = {
                L"dxcompiler.dll",
                L"dxil.dll",
                L"d3dcompiler_47.dll"
            };

            for (const wchar_t* dllName : runtimeDlls)
            {
                const std::filesystem::path srcDll = currentExeDir / dllName;
                if (std::filesystem::exists(srcDll, ec) && std::filesystem::is_regular_file(srcDll, ec))
                {
                    const std::filesystem::path dstDll = outRoot / dllName;
                    std::filesystem::copy_file(
                        srcDll,
                        dstDll,
                        std::filesystem::copy_options::overwrite_existing,
                        ec);
                    if (ec)
                    {
                        result.ErrorMessage = "Failed to copy runtime DLL '" + srcDll.filename().string() + "': " + ec.message();
                        return result;
                    }
                    ++result.FilesCopied;
                }
            }

            const auto copyMountedDir = [&](std::string_view vfsPrefix, const std::filesystem::path& fallbackRel, const std::filesystem::path& dstDir) -> bool
            {
                std::optional<std::filesystem::path> srcDir = VFS::Resolve(vfsPrefix);
                if (!srcDir.has_value())
                {
                    std::error_code localEc;
                    if (std::filesystem::exists(fallbackRel, localEc) && std::filesystem::is_directory(fallbackRel, localEc))
                    {
                        srcDir = std::filesystem::weakly_canonical(fallbackRel, localEc);
                    }
                }

                if (!srcDir.has_value() || !std::filesystem::exists(*srcDir, ec))
                {
                    return true;
                }

                if (VFS::IsInsideSandbox(outRoot, *srcDir))
                {
                    result.ErrorMessage = "Output directory cannot be inside '" + srcDir->string() + "'.";
                    return false;
                }

                std::filesystem::copy(
                    *srcDir,
                    dstDir,
                    std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive,
                    ec);
                if (ec)
                {
                    result.ErrorMessage = "Failed to copy directory '" + srcDir->string() + "': " + ec.message();
                    return false;
                }

                for (const auto& entry : std::filesystem::recursive_directory_iterator(*srcDir, ec))
                {
                    if (!ec && entry.is_regular_file(ec))
                    {
                        ++result.FilesCopied;
                    }
                }

                return true;
            };

            if (!copyMountedDir("shaders://", "shaders", shadersOutDir))
            {
                return result;
            }
            if (!copyMountedDir("scripts://", "scripts", scriptsOutDir))
            {
                return result;
            }
            if (!copyMountedDir("assets://", "assets", assetsOutDir))
            {
                return result;
            }

            std::string entrySceneName = config.EntrySceneName.empty() ? "main.rlscene" : config.EntrySceneName;
            const std::filesystem::path sceneOutPath = scenesOutDir / entrySceneName;
            if (!SceneSerializer::Save(scene, sceneOutPath.string()))
            {
                result.ErrorMessage = "Failed to save active scene to '" + sceneOutPath.string() + "'.";
                return result;
            }
            result.ScenePath = sceneOutPath;
            ++result.FilesCopied;

            const std::filesystem::path configOutPath = outRoot / "game.rlconfig";
            std::ofstream configFile(configOutPath, std::ios::binary | std::ios::trunc);
            if (!configFile.is_open())
            {
                result.ErrorMessage = "Failed to open config file for writing: '" + configOutPath.string() + "'.";
                return result;
            }

            const std::string entrySceneRel = "assets/scenes/" + entrySceneName;
            std::ostringstream jsonStream;
            jsonStream << "{\n"
                       << "  \"title\": \"" << EscapeJsonString(config.GameTitle) << "\",\n"
                       << "  \"entry_scene\": \"" << EscapeJsonString(entrySceneRel) << "\",\n"
                       << "  \"width\": " << config.WindowWidth << ",\n"
                       << "  \"height\": " << config.WindowHeight << ",\n"
                       << "  \"dx11\": " << (config.ForceDX11 ? "true" : "false") << "\n"
                       << "}\n";

            const std::string jsonContent = jsonStream.str();
            configFile.write(jsonContent.data(), static_cast<std::streamsize>(jsonContent.size()));
            configFile.close();
            if (!configFile)
            {
                result.ErrorMessage = "Failed to write config file: '" + configOutPath.string() + "'.";
                return result;
            }
            result.ConfigPath = configOutPath;
            ++result.FilesCopied;

            result.Success = true;
            RL_LOG_INFO("GameExporter: Exported '{}' to '{}' ({} files)", config.GameTitle, outRoot.string(), result.FilesCopied);
            return result;
        }

    private:
        [[nodiscard]] static std::string SanitizeFileName(std::string_view input)
        {
            std::string out;
            out.reserve(input.size());
            for (const unsigned char ch : input)
            {
                if (ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
                    ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*')
                {
                    out.push_back('_');
                }
                else
                {
                    out.push_back(static_cast<char>(ch));
                }
            }
            while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
            {
                out.pop_back();
            }
            return out;
        }

        [[nodiscard]] static std::string EscapeJsonString(std::string_view input)
        {
            std::string out;
            out.reserve(input.size());
            for (const unsigned char ch : input)
            {
                switch (ch)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (ch >= 32)
                    {
                        out.push_back(static_cast<char>(ch));
                    }
                    break;
                }
            }
            return out;
        }
    };
}
