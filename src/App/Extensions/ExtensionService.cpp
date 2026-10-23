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

    // InitEngine is not sufficient evidence that TweakDB data has loaded.
    // Probe here and retry at an existing late gameplay lifecycle callback.
    HookOnceAfter<Raw::CBaseEngine::InitEngine>([&]() {
        TryRestoreTweakDB("InitEngine");
    });

}

void App::ExtensionService::TryRestoreTweakDB(const char* aStage)
{
    // Protect the dispatch against simultaneous engine/gameplay callbacks.
    std::unique_lock lock(m_tweakDBMutex);
    if (m_tweakDBDispatched || !m_loader)
        return;

    auto* db = Red::TweakDB::Get();
    if (!db)
    {
        LogWarning("[CP25 TweakDBProbe] {}: TweakDB instance unavailable.", aStage);
        return;
    }

    // Distinguish a generally empty/unloaded database from a missing
    // appearance-suffix registry. These probes do not modify any records.
    constexpr auto baseItem = Red::TweakDBID("Items.GenericHeadClothing");
    constexpr auto genderSuffix = Red::TweakDBID("itemsFactoryAppearanceSuffix.Gender");
    constexpr auto suffixOrder = Red::TweakDBID(
        "itemsFactoryAppearanceSuffix.ItemsFactoryAppearanceSuffixOrderDefault");
    const bool basePresent = Red::RecordExists(baseItem);
    const bool genderPresent = Red::RecordExists(genderSuffix);
    const bool orderPresent = Red::RecordExists(suffixOrder);
    bool orderFlatPresent = false;
    {
        std::shared_lock dbLock(db->mutex00);
        orderFlatPresent = db->GetFlatValue({suffixOrder, ".appearanceSuffixes"}) != nullptr;
    }

    LogInfo("[CP25 TweakDBProbe] {}: baseItem={} genderSuffix={} suffixOrder={} suffixOrderFlat={}.",
            aStage, basePresent, genderPresent, orderPresent, orderFlatPresent);

    if (!orderPresent || !orderFlatPresent)
    {
        LogWarning("[CP25 TweakDBProbe] {}: suffix registry not ready/present; no TweakDB writes performed.", aStage);
        return;
    }

    m_tweakDBDispatched = true;
    m_loader->OnTweakDBReady();

    // Positive registration evidence is not proof of runtime suffix selection.
    const bool bodyType = Red::RecordExists(Red::TweakDBID("itemsFactoryAppearanceSuffix.BodyType"));
    const bool armsState = Red::RecordExists(Red::TweakDBID("itemsFactoryAppearanceSuffix.ArmsState"));
    const bool legsState = Red::RecordExists(Red::TweakDBID("itemsFactoryAppearanceSuffix.LegsState"));
    const bool feetState = Red::RecordExists(Red::TweakDBID("itemsFactoryAppearanceSuffix.FeetState"));
    LogInfo("[CP25 TweakDBProbe] {}: callbacks dispatched; custom records body={} arms={} legs={} feet={}.",
            aStage, bodyType, armsState, legsState, feetState);
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
