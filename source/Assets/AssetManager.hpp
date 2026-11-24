#pragma once

#include "Assets/AssetHandle.hpp"
#include "Core/Logger.hpp"
#include "Core/UUID.hpp"
#include "Platform/FileSystem/VFS.hpp"
#include "Platform/FileSystem/FileWatcher.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RL
{
    struct IAssetImporter
    {
        virtual ~IAssetImporter() = default;

        virtual std::shared_ptr<AssetBase> Import(const std::string& virtualPath,
                                                   const std::filesystem::path& realPath) = 0;

        virtual bool Handles(std::string_view extension) const = 0;
    };

    class AssetManager
    {
    public:
        using HotReloadCallback = std::function<void(std::string_view virtualPath)>;

        static void RegisterDefaultImporters();

        static void RegisterHotReloadCallback(HotReloadCallback callback)
        {
            std::lock_guard lock(s_reloadMutex);
            s_reloadCallbacks.push_back(std::move(callback));
        }

        static void ClearHotReloadCallbacks()
        {
            std::lock_guard lock(s_reloadMutex);
            s_reloadCallbacks.clear();
        }

        static void Mount(std::string_view prefix, const std::filesystem::path& realDir)
        {
            VFS::Mount(prefix, realDir);

            std::string normalizedPrefix(prefix);
            std::replace(normalizedPrefix.begin(), normalizedPrefix.end(), '\\', '/');
            if (!normalizedPrefix.ends_with('/'))
            {
                normalizedPrefix += '/';
            }

            std::error_code ec;
            const std::filesystem::path absDir = std::filesystem::weakly_canonical(std::filesystem::absolute(realDir), ec);

            bool needRestart = false;
            {
                std::lock_guard lock(s_mutex);
                bool updated = false;
                for (auto& m : s_mounts)
                {
                    if (m.first == normalizedPrefix)
                    {
                        m.second = absDir;
                        updated = true;
                        break;
                    }
                }
                if (!updated)
                {
                    s_mounts.emplace_back(normalizedPrefix, absDir);
                }
                needRestart = s_hotReloadEnabled;
            }

            if (needRestart)
            {
                EnableHotReload(true);
            }
        }

        static void RegisterImporter(std::unique_ptr<IAssetImporter> importer)
        {
            std::lock_guard lock(s_mutex);
            s_importers.push_back(std::move(importer));
        }

        static void EnableHotReload(bool enable = true)
        {
            if (!enable)
            {
                {
                    std::lock_guard lock(s_mutex);
                    s_hotReloadEnabled = false;
                }
                if (s_watcher.IsRunning())
                {
                    s_watcher.Stop();
                }
                return;
            }

            std::vector<std::filesystem::path> dirsToWatch;
            {
                std::lock_guard lock(s_mutex);
                s_hotReloadEnabled = true;

                auto tryQueueRoot = [&](std::string_view rootPrefix) {
                    if (const auto resolved = VFS::Resolve(rootPrefix))
                    {
                        std::error_code ec;
                        if (std::filesystem::exists(*resolved, ec) && std::filesystem::is_directory(*resolved, ec))
                        {
                            const auto norm = std::filesystem::weakly_canonical(*resolved, ec);
                            if (std::find(s_watchedDirs.begin(), s_watchedDirs.end(), norm) == s_watchedDirs.end())
                            {
                                dirsToWatch.push_back(norm);
                            }
                        }
                    }
                };

                tryQueueRoot("assets://");
                tryQueueRoot("scripts://");

                for (const auto& [prefix, absDir] : s_mounts)
                {
                    (void)prefix;
                    std::error_code ec;
                    if (std::filesystem::exists(absDir, ec) && std::filesystem::is_directory(absDir, ec))
                    {
                        const auto norm = std::filesystem::weakly_canonical(absDir, ec);
                        if (std::find(s_watchedDirs.begin(), s_watchedDirs.end(), norm) == s_watchedDirs.end() &&
                            std::find(dirsToWatch.begin(), dirsToWatch.end(), norm) == dirsToWatch.end())
                        {
                            dirsToWatch.push_back(norm);
                        }
                    }
                }
            }

            if (!dirsToWatch.empty())
            {
                if (s_watcher.IsRunning())
                {
                    s_watcher.Stop();
                }

                for (const auto& dir : dirsToWatch)
                {
                    s_watcher.Watch(dir, [](const FileChangeEvent& evt) {
                        std::lock_guard lock(s_pendingMutex);
                        s_pendingEvents.push_back(evt);
                    }, true);

                    std::lock_guard lock(s_mutex);
                    s_watchedDirs.push_back(dir);
                }
            }

            bool hasWatchedDirs = false;
            {
                std::lock_guard lock(s_mutex);
                hasWatchedDirs = !s_watchedDirs.empty();
            }

            if (hasWatchedDirs && !s_watcher.IsRunning())
            {
                s_watcher.Start();
            }
        }

        static void PollHotReload()
        {
            std::vector<FileChangeEvent> events;
            {
                std::lock_guard lock(s_pendingMutex);
                if (s_pendingEvents.empty())
                {
                    return;
                }
                events.swap(s_pendingEvents);
            }

            std::vector<std::string> invalidatedPaths;
            {
                std::lock_guard lock(s_mutex);

                auto normalizePathString = [](const std::filesystem::path& p) -> std::string {
                    std::error_code ec;
                    const std::filesystem::path canon = std::filesystem::weakly_canonical(p, ec);
                    std::string s = (ec ? p : canon).lexically_normal().string();
                    std::replace(s.begin(), s.end(), '\\', '/');
                    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    return s;
                };

                std::vector<std::pair<std::string, std::filesystem::path>> mountRoots = s_mounts;
                auto addVfsRoot = [&](std::string_view prefix) {
                    if (const auto resolved = VFS::Resolve(prefix))
                    {
                        for (const auto& m : mountRoots)
                        {
                            if (m.first == prefix) return;
                        }
                        mountRoots.emplace_back(std::string(prefix), *resolved);
                    }
                };
                addVfsRoot("assets://");
                addVfsRoot("scripts://");

                for (const auto& evt : events)
                {
                    if (evt.type != FileChangeType::Modified &&
                        evt.type != FileChangeType::Created &&
                        evt.type != FileChangeType::Deleted &&
                        evt.type != FileChangeType::Renamed)
                    {
                        continue;
                    }

                    const std::string changedNorm = normalizePathString(evt.path);
                    bool matchedCache = false;

                    for (auto it = s_cache.begin(); it != s_cache.end();)
                    {
                        const std::string cachedNorm = normalizePathString(std::filesystem::path(it->second->absolutePath));
                        if (cachedNorm == changedNorm)
                        {
                            invalidatedPaths.push_back(it->first);
                            s_uuidIndex.erase(it->second->uuid);
                            it = s_cache.erase(it);
                            matchedCache = true;
                        }
                        else
                        {
                            ++it;
                        }
                    }

                    if (!matchedCache)
                    {
                        std::error_code ec;
                        const std::filesystem::path changedCanon = std::filesystem::weakly_canonical(evt.path, ec);
                        const std::filesystem::path& target = ec ? evt.path : changedCanon;
                        for (const auto& [prefix, rootDir] : mountRoots)
                        {
                            const std::filesystem::path rootCanon = std::filesystem::weakly_canonical(rootDir, ec);
                            const std::filesystem::path& base = ec ? rootDir : rootCanon;
                            const auto rel = target.lexically_relative(base);
                            if (!rel.empty() && !rel.is_absolute())
                            {
                                std::string relStr = rel.string();
                                std::replace(relStr.begin(), relStr.end(), '\\', '/');
                                if (!relStr.starts_with(".."))
                                {
                                    invalidatedPaths.push_back(prefix + relStr);
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            std::sort(invalidatedPaths.begin(), invalidatedPaths.end());
            invalidatedPaths.erase(std::unique(invalidatedPaths.begin(), invalidatedPaths.end()), invalidatedPaths.end());

            if (invalidatedPaths.empty())
            {
                return;
            }

            std::vector<HotReloadCallback> callbacks;
            {
                std::lock_guard lock(s_reloadMutex);
                callbacks = s_reloadCallbacks;
            }

            for (const auto& vp : invalidatedPaths)
            {
                RL_LOG_INFO("AssetManager: hot-reload invalidated '{}'", vp);
                for (const auto& cb : callbacks)
                {
                    if (cb)
                    {
                        cb(vp);
                    }
                }
            }
        }

        template <typename AssetT>
        [[nodiscard]] static AssetHandle<AssetT> Load(std::string_view virtualPath)
        {
            {
                bool needsDefaults = false;
                {
                    std::lock_guard lock(s_mutex);
                    needsDefaults = !s_defaultsRegistered && s_importers.empty();
                }
                if (needsDefaults)
                {
                    RegisterDefaultImporters();
                }
            }

            const std::string key(virtualPath);

            {
                std::lock_guard lock(s_mutex);
                if (const auto it = s_cache.find(key); it != s_cache.end())
                {
                    auto typed = std::dynamic_pointer_cast<AssetT>(it->second);
                    if (typed) return AssetHandle<AssetT>(typed);
                }
            }

            const auto realPath = VFS::Resolve(virtualPath);
            if (!realPath)
            {
                RL_LOG_ERROR("AssetManager::Load: cannot resolve '{}'", virtualPath);
                return {};
            }

            std::shared_ptr<AssetBase> base;
            {
                std::lock_guard lock(s_mutex);
                IAssetImporter* importer = nullptr;
                for (auto& imp : s_importers)
                {
                    if (imp->Handles(realPath->extension().string()))
                    {
                        importer = imp.get();
                        break;
                    }
                }
                if (!importer)
                {
                    RL_LOG_ERROR("AssetManager::Load: no importer for '{}'", realPath->extension().string());
                    return {};
                }
                base = importer->Import(key, *realPath);
            }
            if (!base)
            {
                RL_LOG_ERROR("AssetManager::Load: import failed for '{}'", key);
                return {};
            }

            base->uuid         = UUID::Generate();
            base->virtualPath  = key;
            base->absolutePath = realPath->string();
            base->isLoaded     = true;

            auto typed = std::dynamic_pointer_cast<AssetT>(base);
            if (!typed)
            {
                RL_LOG_ERROR("AssetManager::Load: type mismatch for '{}'", key);
                return {};
            }

            {
                std::lock_guard lock(s_mutex);
                s_cache[key]            = base;
                s_uuidIndex[base->uuid] = base;
            }

            RL_LOG_DEBUG("AssetManager: loaded '{}' [{}]", key, base->uuid.ToString());
            return AssetHandle<AssetT>(typed);
        }

        static void Reload(std::string_view virtualPath)
        {
            {
                std::lock_guard lock(s_mutex);
                const auto it = s_cache.find(std::string(virtualPath));
                if (it != s_cache.end())
                {
                    s_uuidIndex.erase(it->second->uuid);
                    s_cache.erase(it);
                }
            }
            RL_LOG_INFO("AssetManager: hot-reloaded '{}'", virtualPath);
        }

        static void Unload(std::string_view virtualPath)
        {
            std::lock_guard lock(s_mutex);
            const auto it = s_cache.find(std::string(virtualPath));
            if (it != s_cache.end())
            {
                s_uuidIndex.erase(it->second->uuid);
                s_cache.erase(it);
                RL_LOG_DEBUG("AssetManager: unloaded '{}'", virtualPath);
            }
        }

        static void Clear()
        {
            std::lock_guard lock(s_mutex);
            s_cache.clear();
            s_uuidIndex.clear();
            RL_LOG_INFO("AssetManager: all assets cleared");
        }

        static void Shutdown()
        {
            {
                std::lock_guard lock(s_mutex);
                s_hotReloadEnabled = false;
            }
            s_watcher.Stop();
            {
                std::lock_guard lock(s_pendingMutex);
                s_pendingEvents.clear();
            }
            {
                std::lock_guard lock(s_reloadMutex);
                s_reloadCallbacks.clear();
            }
            Clear();
        }

        [[nodiscard]] static bool IsLoaded(std::string_view virtualPath)
        {
            std::lock_guard lock(s_mutex);
            return s_cache.contains(std::string(virtualPath));
        }

        [[nodiscard]] static usize GetCacheSize()
        {
            std::lock_guard lock(s_mutex);
            return s_cache.size();
        }

    private:
        [[nodiscard]] static IAssetImporter* FindImporter(std::string_view ext)
        {
            std::lock_guard lock(s_mutex);
            for (auto& imp : s_importers)
            {
                if (imp->Handles(ext)) return imp.get();
            }
            return nullptr;
        }

        inline static std::unordered_map<std::string, std::shared_ptr<AssetBase>> s_cache;
        inline static std::unordered_map<UUID, std::shared_ptr<AssetBase>>        s_uuidIndex;
        inline static std::vector<std::unique_ptr<IAssetImporter>>                s_importers;
        inline static std::vector<std::pair<std::string, std::filesystem::path>>  s_mounts;
        inline static std::vector<std::filesystem::path>                          s_watchedDirs;
        inline static bool                                                        s_defaultsRegistered = false;
        inline static bool                                                        s_hotReloadEnabled = false;
        inline static std::mutex                                                  s_mutex;
        inline static FileWatcher                                                 s_watcher;

        inline static std::vector<FileChangeEvent>                                s_pendingEvents;
        inline static std::mutex                                                  s_pendingMutex;
        inline static std::vector<HotReloadCallback>                              s_reloadCallbacks;
        inline static std::mutex                                                  s_reloadMutex;
    };
}

#include "Assets/AssetImporters.hpp"
