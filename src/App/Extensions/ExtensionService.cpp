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

    // CP2.5: restore modules with available engine hooks; the resource
    // serializer post-processing stage uses OnResourceReady.
    m_loader->Add<ResourceMetaExtension>();
    m_loader->Add<ResourceLinkExtension>();
    m_loader->Add<ResourcePatchExtension>();
    m_loader->Add<MeshExtension>();
    m_loader->Add<LocalizationExtension>();
    m_loader->Add<TransmogExtension>();
    m_loader->Add<AnimationExtension>();
    m_loader->Add<AttachmentExtension>();
    m_loader->Add<GarmentExtension>();
    m_loader->Add<PuppetStateExtension>();
    m_loader->Add<CustomizationExtension>();
    m_loader->Add<JournalExtension>();
    m_loader->Add<QuestPhaseExtension>();
    m_loader->Add<WorldStreamingExtension>();
    m_loader->Add<InkSpawnerExtension>();
    LogInfo("[CP25 Extended] Added Customization, Journal and synchronous InkSpawner alongside appearance, ResourcePatch and WorldStreaming systems.");

    HookOnceAfter<Raw::GameApplication::InitResourceDepot>([&]() {
        m_loader->Configure();
        m_loader->Load();
    });

    HookOnceAfter<Raw::CBaseEngine::LoadGatheredResources>([&]() {
        m_loader->OnDepotReady();
    });

    // TweakDB_Load is absent from the 2.5 address library. PuppetState remains
    // partial until its OnTweakDBReady suffix registration can be scheduled.
    LogWarning("[CP25 Appearance] TweakDB-ready callback unavailable; PuppetState suffix records not yet registered.");
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
