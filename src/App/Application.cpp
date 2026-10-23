#include "Application.hpp"
#include "App/Archives/ArchiveProbe.hpp"
#include "App/Configuration.hpp"
#include "App/Environment.hpp"
#include "App/Extensions/ExtensionService.hpp"
#include "App/Migration.hpp"
#include "App/Patches/EntitySpawnerPatch.hpp"
#include "App/Patches/WorldWidgetLimitPatch.hpp"
#include "App/Project.hpp"
#include "App/Shared/ResourcePathRegistry.hpp"
#include "Core/Foundation/RuntimeProvider.hpp"
#include "Support/MinHook/MinHookProvider.hpp"
#include "Support/RED4ext/RED4extProvider.hpp"
#include "Support/RedLib/RedLibProvider.hpp"
#include "Support/Spdlog/SpdlogProvider.hpp"

App::Application::Application(HMODULE aHandle, const RED4ext::v1::Sdk* aSdk)
{
    Register<Core::RuntimeProvider>(aHandle)
        ->SetBaseImagePathDepth(2);

    Register<Support::MinHookProvider>();
    Register<Support::SpdlogProvider>()
        ->AppendTimestampToLogName()
        ->CreateRecentLogSymlink()
        ->SetMaxLogFiles(5)
        ->ToggleDebugOutput(Config::DebugLogging());
    Register<Support::RED4extProvider>(aHandle, aSdk)
        ->EnableAddressLibrary()
        ->RegisterScripts(Env::ScriptsDir());
    // CP2.5 loader probe: deliberately do not bootstrap the legacy archive
    // loader, extension hooks, or patches with unresolved address hashes.
    Register<App::ArchiveProbe>();
}

void App::Application::OnStarting()
{
    LogInfo("{} {} is starting...", Project::Name, Project::Version.to_string());

    LogInfo("[CP25 Probe] Diagnostic build: no migration cleanup, archive loading, or extensions.");
}
