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
    LogInfo("[CP25 JournalDepot] Journal resource requests moved to depot-ready phase; Customization and InkSpawner retained.");

    HookOnceAfter<Raw::GameApplication::InitResourceDepot>([&]() {
        m_loader->Configure();
        m_loader->Load();
    });

    HookOnceAfter<Raw::CBaseEngine::LoadGatheredResources>([&]() {
        m_loader->OnDepotReady();
    });

    // CP2.5 no longer maps TweakDB_Load. The engine-init completion hook
    // is mapped and also used by InkSpawner. The appearance-suffix list is a
    // concrete indication that TweakDB records have been loaded.
    HookOnceAfter<Raw::CBaseEngine::InitEngine>([&]() {
        if (!m_loader)
            return;

        const auto* db = Red::TweakDB::Get();
        if (!db)
        {
            LogWarning("[CP25 TweakDB] Engine initialized but TweakDB is unavailable; suffix records not installed.");
            return;
        }

        constexpr auto suffixList = Red::TweakDBID(
            "itemsFactoryAppearanceSuffix.ItemsFactoryAppearanceSuffixOrderDefault");
        if (!Red::RecordExists(suffixList))
        {
            LogWarning("[CP25 TweakDB] Appearance suffix ordering record absent at engine init; no suffix changes made.");
            return;
        }

        LogInfo("[CP25 TweakDB] Existing appearance-suffix list found; restoring registered TweakDB extension changes.");
        m_loader->OnTweakDBReady();
        LogInfo("[CP25 TweakDB] TweakDB extension changes applied.");
    });
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
