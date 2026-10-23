#pragma once

// Cyberpunk 2077 2.5 diagnostic only. The RVA below was obtained from the
// 2.5 executable disassembly (VA 0x1422D7BD0, image base 0x140000000).
// This probe does NOT register archives or access ResourceDepot::groups.
#include <atomic>
#include "Core/Foundation/Feature.hpp"
#include "Core/Hooking/HookingAgent.hpp"
#include "Core/Logging/LoggingAgent.hpp"
#include "Core/Raw.hpp"

namespace Raw::ArchiveProbe
{
// The first, second and fourth arguments are deliberately opaque. The
// disassembly establishes R8 as DynArray<CString>* and shows a fifth DWORD
// and sixth BYTE argument. The function returns a bool in AL.
// Treat this as a hypothesis to confirm through runtime logging, not a
// general-purpose API for calling the engine.
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
protected:
    void OnBootstrap() override
    {
        LogInfo("[CP25 Probe] Attaching read-only archive loader observer at RVA 0x022D7BD0.");
        HookBefore<Raw::ArchiveProbe::LoadArchives25>(&ArchiveProbe::Observe).OrThrow();
        LogInfo("[CP25 Probe] Observer installed; original engine loader remains active.");
    }

    void OnShutdown() override
    {
        Unhook<Raw::ArchiveProbe::LoadArchives25>();
    }

private:
    void Observe(uintptr_t aContext, uintptr_t aDestination,
                 const Red::DynArray<Red::CString>* aPaths, uintptr_t aOther,
                 uint32_t aMode, uint8_t aFlags)
    {
        const auto invocation = ++m_invocations;
        if (invocation > 128)
            return;

        // No engine objects or path arrays are modified by this observer.
        // 2.5 disassembly confirms the game reads [R8] and [R8+0xC].
        try
        {
            if (!aPaths)
            {
                LogInfo("[CP25 Probe] call={} context={:#x} destination={:#x} other={:#x} mode={} flags={} paths=NULL",
                        invocation, aContext, aDestination, aOther, aMode, static_cast<uint32_t>(aFlags));
                return;
            }

            const auto count = aPaths->Size();
            LogInfo("[CP25 Probe] call={} context={:#x} destination={:#x} other={:#x} mode={} flags={} count={} data={:#x}",
                    invocation, aContext, aDestination, aOther, aMode, static_cast<uint32_t>(aFlags),
                    count, reinterpret_cast<uintptr_t>(aPaths->Data()));

            if (count == 0 || count > 2048 || !aPaths->Data())
                return;

            const auto limit = (std::min)(count, uint32_t{16});
            for (uint32_t i = 0; i < limit; ++i)
            {
                const auto& name = (*aPaths)[i];
                const auto length = name.Length();
                if (length > 4096)
                {
                    LogWarning("[CP25 Probe] call={} path[{}] suspicious length={}", invocation, i, length);
                    break;
                }
                LogInfo("[CP25 Probe] call={} path[{}]={}",
                        invocation, i,
                        std::string_view(name.c_str(), (std::min)(length, uint32_t{320})));
            }
            if (count > limit)
                LogInfo("[CP25 Probe] call={} another {} paths omitted", invocation, count - limit);
        }
        catch (...)
        {
            // An observer must never prevent the original game function.
        }
    }

    std::atomic<uint32_t> m_invocations{0};
};
}
