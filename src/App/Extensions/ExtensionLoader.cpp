#include "ExtensionLoader.hpp"
#include "App/Utils/Str.hpp"
#include "App/Environment.hpp"

App::ExtensionLoader::ExtensionLoader(std::filesystem::path aBundleDir, std::wstring aConfigExt)
    : m_bundleDir(std::move(aBundleDir))
    , m_customConfigExt(std::move(aConfigExt))
    , m_loaded(false)
{
}

void App::ExtensionLoader::Configure()
{
    if (m_configurables.empty())
        return;

    try
    {
        LogInfo("Discovering archive extensions...");

        for (const auto& module : m_configurables)
            module->ResetConfigs();

        // CP2.5 no longer exposes the old ResourceDepot::groups layout.
        // Discover .xl files from ordinary game mod directories instead.
        // The bundle directory is searched first to retain its precedence.
        Core::Vector<std::filesystem::path> configDirs;
        Core::Vector<std::filesystem::path> configFiles;

        if (!m_bundleDir.empty())
            configDirs.emplace_back(m_bundleDir);

        configDirs.emplace_back(Env::GameDir() / L"archive" / L"pc" / L"mod");
        configDirs.emplace_back(Env::GameDir() / L"mods");

        LogInfo("[CP25 Stage1] Scanning Bundle, archive/pc/mod and mods for .xl files; old ResourceDepot::groups is not accessed.");

        bool foundAny = false;
        bool successAll = true;
        std::error_code error;

        for (const auto& configDir : configDirs)
        {
            if (!std::filesystem::exists(configDir, error))
                continue;

            auto configDirIt = std::filesystem::recursive_directory_iterator(
                configDir, std::filesystem::directory_options::follow_directory_symlink);

            for (const auto& entry : configDirIt)
            {
                if (entry.is_regular_file() && entry.path().extension() == m_customConfigExt)
                {
                    successAll &= AddConfig(entry.path(), configDir, configDir == m_bundleDir);
                    foundAny = true;
                }
            }
        }

        for (const auto& configFile : configFiles)
        {
            if (!std::filesystem::exists(configFile, error))
                continue;

            successAll &= AddConfig(configFile, configFile.parent_path());
            foundAny = true;
        }

        if (foundAny)
        {
            if (successAll)
                LogInfo("All archive extensions loaded.");
            else
                LogWarning("Archive extensions are loaded with issues.");
        }
        else
        {
            LogInfo("No archive extensions found.");
        }
    }
    catch (const std::exception& ex)
    {
        LogError(ex.what());
    }
    catch (...)
    {
        LogError("An unknown error occurred while discovering archive extensions.");
    }
}

bool App::ExtensionLoader::AddConfig(const std::filesystem::path& aPath, const std::filesystem::path& aDir, bool aSilent)
{
    bool success = true;

    try
    {
        if (!aSilent)
        {
            LogInfo("Loading \"{}\"...", aPath.filename().string());
        }

        std::ifstream file(aPath);

        if (!file)
            throw std::runtime_error("Can't read extension file.");

        const auto name = aPath.filename().string();
        const auto config = YAML::Load(file);

        for (const auto& module : m_configurables)
            success &= module->AddConfig(name, config);
    }
    catch (const std::exception& ex)
    {
        LogError("{}: {}", aPath.filename().string(), ex.what());
        success = false;
    }
    catch (...)
    {
        LogError("{}: An unknown error occurred.", aPath.filename().string());
        success = false;
    }

    return success;
}

void App::ExtensionLoader::Load()
{
    if (m_loaded)
        return;

    for (const auto& module : m_modules)
    {
        try
        {
            module->Load();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
            module->Unload();
        }
        catch (...)
        {
            LogError("[{}] Failed to load. An unknown error has occurred.", module->GetName());
            module->Unload();
        }
    }

    for (const auto& module : m_configurables)
    {
        try
        {
            module->Configure();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
        }
        catch (...)
        {
            LogError("[{}] Failed to configure. An unknown error has occurred.", module->GetName());
        }
    }

    for (const auto& module : m_configurables)
    {
        try
        {
            module->PostConfigure();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
        }
        catch (...)
        {
            LogError("[{}] Failed to configure. An unknown error has occurred.", module->GetName());
        }
    }

    m_loaded = true;
}

void App::ExtensionLoader::OnDepotReady()
{
    if (!m_loaded)
        return;

    for (const auto& module : m_modules)
    {
        try
        {
            module->OnDepotReady();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
            module->Unload();
        }
        catch (...)
        {
            LogError("[{}] Failed to apply post load effects. An unknown error has occurred.", module->GetName());
            module->Unload();
        }
    }
}

void App::ExtensionLoader::OnTweakDBReady()
{
    if (!m_loaded)
        return;

    for (const auto& module : m_modules)
    {
        try
        {
            module->OnTweakDBReady();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
            module->Unload();
        }
        catch (...)
        {
            LogError("[{}] Failed to apply TweakDB changes. An unknown error has occurred.", module->GetName());
            module->Unload();
        }
    }
}

void App::ExtensionLoader::Unload()
{
    if (!m_loaded)
        return;

    for (const auto& module : m_modules)
    {
        try
        {
            module->Unload();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
        }
        catch (...)
        {
            LogError("[{}] Failed to unload. An unknown error has occurred.", module->GetName());
        }
    }

    m_loaded = false;
}

void App::ExtensionLoader::Reload()
{
    if (!m_loaded)
        return;

    for (const auto& module : m_configurables)
    {
        try
        {
            module->Configure();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
        }
        catch (...)
        {
            LogError("[{}] Failed to configure. An unknown error has occurred.", module->GetName());
        }
    }

    for (const auto& module : m_configurables)
    {
        try
        {
            module->PostConfigure();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
        }
        catch (...)
        {
            LogError("[{}] Failed to configure. An unknown error has occurred.", module->GetName());
        }
    }

    for (const auto& module : m_configurables)
    {
        try
        {
            module->Reload();
        }
        catch (const std::exception& ex)
        {
            LogError("[{}] {}", module->GetName(), ex.what());
            module->Unload();
        }
        catch (...)
        {
            LogError("[{}] Failed to reload. An unknown error has occurred.", module->GetName());
            module->Unload();
        }
    }
}
