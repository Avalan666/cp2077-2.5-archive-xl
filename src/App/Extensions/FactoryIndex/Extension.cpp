#include "Extension.hpp"
#include "App/Configuration.hpp"
#include <atomic>
#include "Core/Facades/Container.hpp"
#include "Red/FactoryIndex.hpp"

namespace
{
constexpr auto ExtensionName = "FactoryIndex";
constexpr auto LastFactory = Red::ResourcePath(R"(base\gameplay\factories\vehicles\vehicles.csv)");
}

std::string_view App::FactoryIndexExtension::GetName()
{
    return ExtensionName;
}

bool App::FactoryIndexExtension::Load()
{
    if (Config::Batch1("factory"))
    {
        // This mapped method is also used by Transmog. Observe its context
        // rather than hooking the unavailable LoadFactoryAsync address.
        HookAfter<Raw::FactoryIndex::ResolveResource>(&OnResolveResource).OrThrow();
        LogInfo("[CP25 Batch1 D] ResolveResource probe installed.");
    }
    else
        LogInfo("[CP25 Batch1 D] Disabled.");

    s_resourcePathRegistry = Core::Resolve<ResourcePathRegistry>();

    return true;
}

bool App::FactoryIndexExtension::Unload()
{
    if (IsHooked<Raw::FactoryIndex::ResolveResource>())
        Unhook<Raw::FactoryIndex::ResolveResource>();

    return true;
}

void App::FactoryIndexExtension::Configure()
{
    s_factories.clear();
    if (!Config::Batch1("factory"))
        return;

    auto depot = Red::ResourceDepot::Get();

    Core::Set<Red::ResourcePath> invalidPaths;

    for (const auto& unit : m_configs)
    {
        for (const auto& factoryPathStr : unit.factories)
        {
            auto factoryPath = Red::ResourcePath(factoryPathStr.c_str());

            if (!depot->ResourceExists(factoryPath))
            {
                if (!invalidPaths.contains(factoryPath))
                {
                    LogError("[{}] Factory \"{}\" doesn't exist. Skipped.", ExtensionName, factoryPathStr);
                    invalidPaths.insert(factoryPath);
                }
                continue;
            }

            s_factories.insert_or_assign(factoryPath, factoryPathStr);
        }
    }

    for (const auto& [knownPath, knownPathStr] : s_factories)
    {
        s_resourcePathRegistry->RegisterPath(knownPath, knownPathStr);
    }

    LogInfo("[CP25 Batch1 D] {} valid mod factories configured; async injection is not restored yet.",
            s_factories.size());
    m_configs.clear();
}

void App::FactoryIndexExtension::OnResolveResource(uintptr_t, Red::ResourcePath& aPath, Red::CName aName)
{
    static std::atomic<uint32_t> observed{0};
    auto event = ++observed;
    if (event <= 12)
        LogInfo("[CP25 Batch1 D] ResolveResource event={} name={:#x} resolvedHash={:#x} configuredFactories={}.",
                event, aName.hash, aPath.hash, s_factories.size());
}

#if 0 // The 2.5 address for LoadFactoryAsync is not mapped; retain original implementation for later porting.
void App::FactoryIndexExtension::OnLoadFactoryAsync(uintptr_t aIndex, Red::ResourcePath aPath, uintptr_t aContext)
{
    if (aPath != LastFactory)
        return;

    LogInfo("[{}] Initializing factory index...", ExtensionName);

    if (!s_factories.empty())
    {
        for (const auto& [factoryPath, factoryPathStr] : s_factories)
        {
            LogInfo("[{}] Loading factory \"{}\"...", ExtensionName, factoryPathStr);

            Raw::FactoryIndex::LoadFactoryAsync(aIndex, factoryPath, aContext);
        }

        LogInfo("[{}] All factories loaded.", ExtensionName);
    }
    else
    {
        LogInfo("[{}] No factories to load.", ExtensionName);
    }
}

#endif
