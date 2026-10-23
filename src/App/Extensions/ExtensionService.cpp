#include "ExtensionService.hpp"
#include "App/Extensions/Animation/Extension.hpp"
#include "App/Extensions/Attachment/Extension.hpp"
#include "App/Extensions/Customization/Extension.hpp"
#include "App/Extensions/FactoryIndex/Extension.hpp"
#include "App/Extensions/Garment/Extension.hpp"
#include "App/Extensions/InkSpawner/Extension.hpp"
#include "App/Extensions/Journal/Extension.hpp"
#include "App/Extensions/Localization/Extension.hpp"
#include "App/Extensions/Mesh/Extension.hpp"
#include "App/Extensions/PuppetState/Extension.hpp"
#include "App/Extensions/QuestPhase/Extension.hpp"
#include "App/Extensions/ResourceLink/Extension.hpp"
#include "App/Extensions/ResourceMeta/Extension.hpp"
#include "App/Extensions/ResourcePatch/Extension.hpp"
#include "App/Extensions/Transmog/Extension.hpp"
#include "App/Extensions/WorldStreaming/Extension.hpp"
#include "Red/GameApplication.hpp"
#include "Red/GameEngine.hpp"
#include "Red/ResourceLoader.hpp"
#include "Red/TweakDB.hpp"

App::ExtensionService::ExtensionService(std::filesystem::path aBundleDir)
    : m_bundleDir(std::move(aBundleDir))
{
}

void App::ExtensionService::OnBootstrap()
{
    m_loader = Core::MakeUnique<ExtensionLoader>(m_bundleDir, L".xl");

    // Stage 1: these modules use address IDs present in the 2.5 library.
    // Other modules are disabled until their missing hooks and ABI are ported.
    m_loader->Add<ResourceMetaExtension>();
    m_loader->Add<LocalizationExtension>();
    m_loader->Add<TransmogExtension>();
    LogInfo("[CP25 Stage1] Enabled ResourceMeta, Localization and Transmog only.");

    HookOnceAfter<Raw::GameApplication::InitResourceDepot>([&]() {
        m_loader->Configure();
        m_loader->Load();
    });

    HookOnceAfter<Raw::CBaseEngine::LoadGatheredResources>([&]() {
        m_loader->OnDepotReady();
    });

    // TweakDB_Load is absent from the 2.5 address library. Do not attach.
    LogWarning("[CP25 Stage1] TweakDB-ready callback disabled pending a verified 2.5 hook.");
}

void App::ExtensionService::OnShutdown()
{
    m_loader->Unload();
    m_loader = nullptr;
}

void App::ExtensionService::Configure()
{
    std::unique_lock _(m_reloadMutex);

    if (!IsHooked<Raw::ResourceLoader::OnUpdate>())
    {
        HookAfter<Raw::ResourceLoader::OnUpdate>([&]() {
            std::unique_lock _(m_reloadMutex);

            m_loader->Configure();
            m_loader->Reload();

            Unhook<Raw::ResourceLoader::OnUpdate>();
        });
    }
}
