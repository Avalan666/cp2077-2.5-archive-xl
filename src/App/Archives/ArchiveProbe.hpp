#pragma once

// Cyberpunk 2077 2.5 archive injection experiment. Keep this on a separate
// branch until the new loader's ownership, lifetime and priority are verified.
// Game RVA 0x022D7BD0 (VA 0x1422D7BD0, image base 0x140000000).
#include <atomic>
#include <cctype>
#include "Core/Foundation/Feature.hpp"
#include "Core/Hooking/HookingAgent.hpp"
#include "Core/Logging/LoggingAgent.hpp"
#include "Core/Raw.hpp"

namespace Raw::ArchiveProbe
{
inline constexpr auto LoadArchives25 = Core::RawFunc<
    static_cast<uintptr_t>(0x022D7BD0),
    bool (*)(uintptr_t, uintptr_t, const Red::DynArray<Red::CString>*,
             uintptr_t, uint32_t, uint8_t)>{};
}

namespace App
{
class ArchiveProbe final
    : public Core::Feature
    , public Core::HookingAgent
    , public Core::LoggingAgent
{
public:
    explicit ArchiveProbe(std::filesystem::path aBundleDir)
        : m_bundleDir(std::move(aBundleDir))
    {
    }

protected:
    void OnBootstrap() override
    {
        LogInfo("[CP25 Inject] Installing archive loader wrapper at RVA 0x022D7BD0.");
        HookWrap<Raw::ArchiveProbe::LoadArchives25>(&ArchiveProbe::OnLoadArchives).OrThrow();
        LogInfo("[CP25 Inject] Wrapper installed. Original engine loader will be called.");
    }

    void OnShutdown() override
    {
        Unhook<Raw::ArchiveProbe::LoadArchives25>();
    }

private:
    using Original = decltype(Raw::ArchiveProbe::LoadArchives25)::Callable;

    // The read-only probe confirmed that the native mod group has flags=0,
    // a nonzero context, mode=0, and paths under archive/pc/mod. DLC and
    // base-game calls have flags=2. Check actual paths too, not flags alone.
    static bool IsNativeModGroup(uintptr_t aContext, uint32_t aMode,
                                 uint8_t aFlags,
                                 const Red::DynArray<Red::CString>* aPaths)
    {
        if (!aContext || aMode != 0 || aFlags != 0 || !aPaths)
            return false;

        const auto count = aPaths->Size();
        if (count == 0 || count > 2048 || !aPaths->Data())
            return false;

        const auto limit = (std::min)(count, uint32_t{16});
        for (uint32_t i = 0; i < limit; ++i)
        {
            const auto& s = (*aPaths)[i];
            if (s.Length() > 4096)
                return false;

            std::string path(s.c_str(), s.Length());
            for (char& ch : path)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (path.find("\\archive\\pc\\mod\\") != std::string::npos)
                return true;
        }
        return false;
    }

    bool OnLoadArchives(Original aOriginal, uintptr_t aContext,
                        uintptr_t aDestination,
                        const Red::DynArray<Red::CString>* aPaths,
                        uintptr_t aOther, uint32_t aMode, uint8_t aFlags)
    {
        const auto invocation = ++m_invocations;
        if (invocation <= 24)
        {
            LogInfo("[CP25 Inject] call={} context={:#x} destination={:#x} mode={} flags={} paths={}",
                    invocation, aContext, aDestination, aMode,
                    static_cast<uint32_t>(aFlags), aPaths ? aPaths->Size() : 0);
        }

        if (!IsNativeModGroup(aContext, aMode, aFlags, aPaths) || m_injected.load())
        {
            return aOriginal(aContext, aDestination, aPaths, aOther, aMode, aFlags);
        }

        std::vector<std::filesystem::path> extras;
        std::error_code error;

        if (!std::filesystem::is_directory(m_bundleDir, error))
        {
            LogWarning("[CP25 Inject] No bundle directory at '{}' ({}).",
                       m_bundleDir.string(), error.message());
            return aOriginal(aContext, aDestination, aPaths, aOther, aMode, aFlags);
        }

        for (const auto& entry : std::filesystem::directory_iterator(m_bundleDir, error))
        {
            if (error)
                break;

            std::error_code fileError;
            if (entry.is_regular_file(fileError) && entry.path().extension() == L".archive")
                extras.push_back(entry.path());
        }

        if (error)
        {
            LogWarning("[CP25 Inject] Could not scan '{}': {}",
                       m_bundleDir.string(), error.message());
        }

        if (extras.empty())
        {
            LogInfo("[CP25 Inject] Native mod group confirmed, but Bundle contains no .archive files.");
            return aOriginal(aContext, aDestination, aPaths, aOther, aMode, aFlags);
        }

        // Stage a separate array. Never modify the game's input array or any
        // ResourceDepot internals. The game sees the extra files during this
        // one synchronous loader call; no saved reference to the array is kept.
        Red::DynArray<Red::CString> combined;
        uint32_t added = 0;
        try
        {
            for (const auto& path : *aPaths)
                combined.PushBack(path);

            for (const auto& extra : extras)
            {
                const std::string nativePath = extra.string();
                bool duplicate = false;

                for (const auto& existing : combined)
                {
                    if (existing == nativePath.c_str())
                    {
                        duplicate = true;
                        break;
                    }
                }

                if (duplicate)
                    continue;

                combined.PushBack(nativePath);
                ++added;
                LogInfo("[CP25 Inject] Added archive '{}'", nativePath);
            }
        }
        catch (const std::exception& ex)
        {
            LogError("[CP25 Inject] Could not build combined archive list: {}", ex.what());
            return aOriginal(aContext, aDestination, aPaths, aOther, aMode, aFlags);
        }

        if (added == 0)
        {
            LogInfo("[CP25 Inject] All Bundle archives already present.");
            return aOriginal(aContext, aDestination, aPaths, aOther, aMode, aFlags);
        }

        // In CP2.5 the destination argument points directly to the archive
        // DynArray, not to ResourceDepot or ArchiveGroup. The verified append
        // routine 0x140682794 uses 0x50-byte elements, matching Red::Archive.
        static_assert(sizeof(Red::Archive) == 0x50);
        auto* destination = reinterpret_cast<const Red::DynArray<Red::Archive>*>(aDestination);
        const uint32_t countBefore = destination ? destination->Size() : 0;

        m_injected.store(true);
        LogInfo("[CP25 Inject] Loading native mod group with {} original + {} bundled archives.",
                aPaths->Size(), added);

        const bool result = aOriginal(aContext, aDestination, &combined, aOther, aMode, aFlags);
        const uint32_t countAfter = destination ? destination->Size() : 0;
        // On this engine path AL=0 is normal loop completion; AL=1 is an
        // early-exit path. Archive count and final path provide a stronger test.
        LogInfo("[CP25 Inject] Loader early-exit={} destination archives before={} after={} delta={}.",
                result, countBefore, countAfter,
                static_cast<int64_t>(countAfter) - countBefore);
        if (destination && countAfter > countBefore && countAfter <= 4096 && destination->Data())
        {
            const auto& lastArchive = destination->Back();
            if (lastArchive.path.Length() <= 4096)
            {
                LogInfo("[CP25 Inject] Last registered archive='{}'.",
                        lastArchive.path.c_str());
            }
            else
            {
                LogWarning("[CP25 Inject] Last archive path has implausible length={}.",
                           lastArchive.path.Length());
            }
        }
        return result;
    }

    std::filesystem::path m_bundleDir;
    std::atomic<uint32_t> m_invocations{0};
    std::atomic<bool> m_injected{false};
};
}
