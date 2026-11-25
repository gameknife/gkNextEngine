#include "Engine/Options.hpp"
#include "Engine/Utilities/Exception.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace Runtime::Config
{

namespace
{
    enum class EOptKind : uint8_t
    {
        Bool,
        UInt32,
        Float,
        String,
        StringVec,
        Help,
        HelpAll,
    };

    struct FOptSpec
    {
        const char* group;
        const char* name;
        char shortName;
        const char* description;
        EOptKind kind;
        void* target;
        bool* wasSet = nullptr;
    };

    bool EqualsIgnoreCase(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
            {
                return false;
            }
        }
        return true;
    }

    bool TryParseBoolLiteral(std::string_view text, bool& outValue)
    {
        if (text == "1" || EqualsIgnoreCase(text, "true") || EqualsIgnoreCase(text, "yes") || EqualsIgnoreCase(text, "on"))
        {
            outValue = true;
            return true;
        }
        if (text == "0" || EqualsIgnoreCase(text, "false") || EqualsIgnoreCase(text, "no") || EqualsIgnoreCase(text, "off"))
        {
            outValue = false;
            return true;
        }
        return false;
    }

    bool TryParseUInt32(std::string_view text, uint32_t& outValue)
    {
        if (text.empty())
        {
            return false;
        }
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), outValue);
        return ec == std::errc{} && ptr == text.data() + text.size();
    }

    bool TryParseFloat(std::string_view text, float& outValue)
    {
        if (text.empty())
        {
            return false;
        }
        std::string buf(text);
        char* endPtr = nullptr;
        outValue = std::strtof(buf.c_str(), &endPtr);
        return endPtr != buf.c_str() && *endPtr == '\0';
    }

    [[noreturn]] void FailCli(std::string_view message)
    {
        std::cerr << message << "\n\nRun with --help for the available options." << std::endl;
        std::exit(2);
    }

    void PrintHelp(const FOptSpec* specs, size_t count, bool includeAutomation)
    {
        static constexpr const char* kGroups[] = {
            "", "Display", "Rendering", "Remote Play", "Terminal", "Diagnostics", "Automation"
        };
        std::cout << "A cross-platform Vulkan renderer and engine.\nUsage:\n  gkNextRenderer [OPTION...]\n";
        for (const char* group : kGroups)
        {
            if (!includeAutomation && std::string_view(group) == "Automation")
            {
                continue;
            }
            bool headerPrinted = false;
            for (size_t i = 0; i < count; ++i)
            {
                const FOptSpec& spec = specs[i];
                if (std::string_view(spec.group) != group)
                {
                    continue;
                }
                if (!headerPrinted)
                {
                    std::cout << "\n";
                    if (group[0] != '\0')
                    {
                        std::cout << " " << group << " options:\n";
                    }
                    headerPrinted = true;
                }
                std::string flagLabel = spec.shortName != '\0'
                    ? (std::string("  -") + spec.shortName + ", --" + spec.name)
                    : (std::string("      --") + spec.name);
                if (spec.kind != EOptKind::Bool && spec.kind != EOptKind::Help && spec.kind != EOptKind::HelpAll)
                {
                    flagLabel += " arg";
                }
                if (flagLabel.size() < 34)
                {
                    flagLabel.append(34 - flagLabel.size(), ' ');
                }
                else
                {
                    flagLabel += "  ";
                }
                std::cout << flagLabel << spec.description << "\n";
            }
        }
        std::cout << std::flush;
    }
}

Options::Options(const int argc, const char* argv[])
{
    locale = "en";
    Width = 1920;
    Height = 1080;
    PresentMode = 2;
    RemoteFps = 60;
    TuiSsaa = 4;
    HardwareQuery = true;

    const bool disableStreamlineForApplication =
        argc > 0 && argv[0] != nullptr &&
        std::filesystem::path(argv[0]).stem().string() == "gkNextEditor";

    std::string remoteResolution;
    bool noShaderHotReload = false;
    bool noCppLiveCoding = false;

    const FOptSpec specs[] = {
        {"", "load-scene", '\0', "Scene to load. Absolute path, or relative to the application directory.", EOptKind::String, &SceneName},
        {"", "hdri", '\0', "HDR environment map to load.", EOptKind::String, &HDRIfile},
        {"", "locale", '\0', "User interface language: en or zhCN.", EOptKind::String, &locale},
        {"", "help", 'h', "Print the common options.", EOptKind::Help, nullptr},
        {"", "help-all", '\0', "Print every option, including automation and test hooks.", EOptKind::HelpAll, nullptr},

        {"Display", "width", '\0', "Framebuffer width in pixels.", EOptKind::UInt32, &Width, &WidthSpecified},
        {"Display", "height", '\0', "Framebuffer height in pixels.", EOptKind::UInt32, &Height, &HeightSpecified},
        {"Display", "fullscreen", '\0', "Start borderless fullscreen instead of windowed.", EOptKind::Bool, &Fullscreen},
#if GK_WITH_VITURE
        {"Display", "ar", '\0', "Enable VITURE AR head tracking.", EOptKind::Bool, &ArMode},
        {"Display", "ar-world-units-per-meter", '\0', "World-unit scale applied to tracked physical translation.", EOptKind::Float, &ArWorldUnitsPerMeter},
        {"Display", "ar-prediction-ms", '\0', "VITURE pose prediction horizon in milliseconds (default: 20.0).", EOptKind::Float, &ArPredictionMs},
        {"Display", "ar-smoothing-hz", '\0', "AR pose low-pass smoothing frequency in Hz (0 disables smoothing; default: 0).", EOptKind::Float, &ArSmoothingHz},
        {"Display", "ar-dof", '\0', "VITURE Carina tracking mode: 3 or 6 (default: 6).", EOptKind::UInt32, &ArDof},
#endif
        {"Display", "present-mode", '\0', "Presentation mode (0 = Immediate, 1 = MailBox, 2 = FIFO, 3 = FIFO relaxed).", EOptKind::UInt32, &PresentMode},
        {"Display", "forcesdr", '\0', "Force SDR output even when the display supports HDR.", EOptKind::Bool, &ForceSDR},
        {"Display", "system-dpi-scaling", '\0', "Use legacy Windows bitmap DPI scaling instead of engine-native DPI scaling.", EOptKind::Bool, &SystemDpiScaling},

        {"Rendering", "gpu", '\0', "Index of the GPU to render on. Use --hwquery to list capabilities.", EOptKind::UInt32, &GpuIdx},
        {"Rendering", "forcenort", '\0', "Disable hardware ray tracing and fall back to the software renderers.", EOptKind::Bool, &ForceNoRT},
        {"Rendering", "force-compatibility-renderer", '\0', "Pretend the GPU cannot back the full bindless arrays.", EOptKind::Bool, &ForceCompatibilityRenderer},
        {"Rendering", "forcesoftgen", '\0', "Force software ray tracing for ambient cube generation.", EOptKind::Bool, &ForceSoftGen},
        {"Rendering", "disable-streamline", '\0', "Disable NVIDIA Streamline (DLSS, Reflex, Frame Generation).", EOptKind::Bool, &DisableStreamline},
        {"Rendering", "disable-fidelityfx", '\0', "Disable AMD FidelityFX (FSR).", EOptKind::Bool, &DisableFidelityFX},
        {"Rendering", "cvar", '\0', "Apply a startup CVar override, e.g. --cvar \"r.upscaler.type 1\". Repeatable.", EOptKind::StringVec, &CVarOverrides},
        {"Rendering", "shader-hotreload", '\0', "Enable Slang shader hot reload.", EOptKind::Bool, &ShaderHotReload},
        {"Rendering", "no-shader-hotreload", '\0', "Disable Slang shader hot reload.", EOptKind::Bool, &noShaderHotReload},
        {"Rendering", "shader-hotreload-interval", '\0', "Shader hot reload poll interval in seconds.", EOptKind::Float, &ShaderHotReloadInterval},
        {"Rendering", "reference", '\0', "Reference renderer comparison mode.", EOptKind::Bool, &ReferenceMode},
        {"Rendering", "savefile", '\0', "Save a screenshot after each benchmark run.", EOptKind::Bool, &SaveFile},

        {"Remote Play", "remote", '\0', "Enable WebRTC Remote Play host mode.", EOptKind::Bool, &RemoteMode},
        {"Remote Play", "remote-show-window", '\0', "Keep the desktop window visible while --remote is active.", EOptKind::Bool, &RemoteShowWindow},
        {"Remote Play", "remote-multiview", '\0', "Give each browser client its own camera and input context. Requires --remote.", EOptKind::Bool, &RemoteMultiView},
        {"Remote Play", "remote-bind", '\0', "Bind address.", EOptKind::String, &RemoteBind},
        {"Remote Play", "remote-http-port", '\0', "HTTP port serving the browser client.", EOptKind::UInt32, &RemoteHttpPort},
        {"Remote Play", "remote-port", '\0', "Signaling WebSocket port.", EOptKind::UInt32, &RemotePort},
        {"Remote Play", "remote-bitrate", '\0', "Starting video bitrate in kbps. 0 = automatic.", EOptKind::UInt32, &RemoteBitrateKbps},
        {"Remote Play", "remote-fps", '\0', "Target stream frame rate.", EOptKind::UInt32, &RemoteFps},
        {"Remote Play", "remote-res", '\0', "Encode resolution, e.g. 1280x720. Empty means the source resolution.", EOptKind::String, &remoteResolution},
        {"Remote Play", "remote-max-clients", '\0', "Maximum simultaneous clients in --remote-multiview mode.", EOptKind::UInt32, &RemoteMaxClients},
        {"Remote Play", "remote-encoder", '\0', "Video encoder: auto or vulkan.", EOptKind::String, &RemoteEncoder},

        {"Terminal", "tui", '\0', "Render into the current terminal using truecolor block characters.", EOptKind::Bool, &Tui},
        {"Terminal", "tui-fps", '\0', "Maximum terminal refresh rate.", EOptKind::UInt32, &TuiFps},
        {"Terminal", "tui-max-cols", '\0', "Column cap (0 = auto).", EOptKind::UInt32, &TuiMaxCols},
        {"Terminal", "tui-max-rows", '\0', "Row cap (0 = auto).", EOptKind::UInt32, &TuiMaxRows},
        {"Terminal", "tui-ssaa", '\0', "Supersample factor for hidden rendering (1-16).", EOptKind::UInt32, &TuiSsaa},
        {"Terminal", "tui-cell-mode", '\0', "Character layout: half or quadrant.", EOptKind::String, &TuiCellMode},
        {"Terminal", "tui-no-input", '\0', "Do not capture stdin.", EOptKind::Bool, &TuiNoInput},

        {"Diagnostics", "hwquery", '\0', "Log the selected device's ray tracing and feature support at startup.", EOptKind::Bool, &HardwareQuery},
        {"Diagnostics", "validation", '\0', "Enable Vulkan validation layers.", EOptKind::Bool, &Validation},
        {"Diagnostics", "sync-validation", '\0', "Enable Vulkan synchronization validation. Implies --validation.", EOptKind::Bool, &SyncValidation},
        {"Diagnostics", "vulkan-driver", '\0', "Vulkan driver selection: native, lvp (Windows/Linux), or dozen (Windows).", EOptKind::String, &VulkanDriver},
        {"Diagnostics", "renderdoc", '\0', "Capture the first ready scene frame and open it in RenderDoc if available.", EOptKind::Bool, &RenderDoc},
        {"Diagnostics", "hidden-window", '\0', "Create the window hidden: no popup and no focus steal.", EOptKind::Bool, &HiddenWindow},
        {"Diagnostics", "dedicated", '\0', "Dedicated server: tick the game without drawing or presenting. Implies --hidden-window.", EOptKind::Bool, &Dedicated},
        {"Diagnostics", "dedicated-tick-rate", '\0', "Game ticks per second in --dedicated mode.", EOptKind::UInt32, &DedicatedTickRate},
        {"Diagnostics", "headless-surface", '\0', "Use VK_EXT_headless_surface and skip SDL window creation.", EOptKind::Bool, &HeadlessSurface},
        {"Diagnostics", "benchmark-config", '\0', "Load a gkNextMotionBenchmark orchestration JSON.", EOptKind::String, &BenchmarkConfig},

        {"Automation", "agent-validation", '\0', "Agent validation mode: hidden window, immediate present, deterministic pacing.", EOptKind::Bool, &AgentValidation},
        {"Automation", "agent-visible-window", '\0', "Keep the window visible during agent validation.", EOptKind::Bool, &AgentVisibleWindow},
        {"Automation", "agent-control", '\0', "Loopback endpoint for gnb runtime control (host:port).", EOptKind::String, &AgentControl},
        {"Automation", "agent-control-token", '\0', "One-time token for gnb runtime control.", EOptKind::String, &AgentControlToken},
        {"Automation", "exit-after-frames", '\0', "Request a clean exit after this many rendered frames (0 disables).", EOptKind::UInt32, &ExitAfterFrames},
        {"Automation", "asset-trace", '\0', "Append successfully resolved runtime asset paths to this manifest.", EOptKind::String, &AssetTrace},
        {"Automation", "dump-reflection", '\0', "Write the entt::meta reflection manifest to this path and exit.", EOptKind::String, &DumpReflection},
        {"Automation", "fastexit", '\0', "Skip task teardown on exit.", EOptKind::Bool, &FastExit},
        {"Automation", "update-baseline", '\0', "Update visual test baseline images from the current run.", EOptKind::Bool, &UpdateVisualTestBaseline},
        {"Automation", "flappy-replay", '\0', "Run the Flappy deterministic replay and write trace output.", EOptKind::Bool, &FlappyReplay},
        {"Automation", "cpp-live-coding", '\0', "Enable Live++ C++ live coding when compiled in.", EOptKind::Bool, &CppLiveCoding},
        {"Automation", "no-cpp-live-coding", '\0', "Disable Live++ C++ live coding.", EOptKind::Bool, &noCppLiveCoding},
        {"Automation", "test-gltf", '\0', "Run the glTF robustness test from the Khronos Sample Assets.", EOptKind::Bool, &TestGltfRobustness},
        {"Automation", "test-gltf-filter", '\0', "Filter for the glTF robustness test (partial name match).", EOptKind::String, &TestGltfFilter},
    };
    constexpr size_t specCount = sizeof(specs) / sizeof(specs[0]);

    auto findLong = [&](std::string_view name) -> const FOptSpec*
    {
        for (const FOptSpec& s : specs)
        {
            if (name == s.name)
            {
                return &s;
            }
        }
        return nullptr;
    };

    auto findShort = [&](char ch) -> const FOptSpec*
    {
        for (const FOptSpec& s : specs)
        {
            if (s.shortName != '\0' && s.shortName == ch)
            {
                return &s;
            }
        }
        return nullptr;
    };

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr)
        {
            continue;
        }
        const std::string_view rawArg(argv[i]);
        if (rawArg.empty())
        {
            continue;
        }

        const FOptSpec* spec = nullptr;
        std::string_view inlineValue;
        bool hasInlineValue = false;

        if (rawArg.starts_with("--"))
        {
            std::string_view body = rawArg.substr(2);
            const size_t eqPos = body.find('=');
            if (eqPos != std::string_view::npos)
            {
                inlineValue = body.substr(eqPos + 1);
                body = body.substr(0, eqPos);
                hasInlineValue = true;
            }
            spec = findLong(body);
            if (spec == nullptr)
            {
                FailCli(std::string("Option '") + std::string(body) + "' does not exist");
            }
        }
        else if (rawArg.starts_with('-') && rawArg.size() >= 2)
        {
            spec = findShort(rawArg[1]);
            if (spec == nullptr || rawArg.size() > 2)
            {
                FailCli(std::string("Option '") + std::string(rawArg) + "' does not exist");
            }
        }
        else
        {
            FailCli(std::string("Unexpected positional argument '") + std::string(rawArg) + "'");
        }

        if (spec->wasSet != nullptr)
        {
            *spec->wasSet = true;
        }

        if (spec->kind == EOptKind::HelpAll)
        {
            PrintHelp(specs, specCount, true);
            std::exit(0);
        }
        if (spec->kind == EOptKind::Help)
        {
            PrintHelp(specs, specCount, false);
            std::cout << "\nRun with --help-all to also list automation and test options." << std::endl;
            std::exit(0);
        }

        if (spec->kind == EOptKind::Bool)
        {
            bool parsedBool = true;
            if (hasInlineValue)
            {
                if (!TryParseBoolLiteral(inlineValue, parsedBool))
                {
                    FailCli(std::string("Argument '") + std::string(inlineValue) + "' failed to parse for --" + spec->name);
                }
            }
            else if (i + 1 < argc && argv[i + 1] != nullptr && TryParseBoolLiteral(argv[i + 1], parsedBool))
            {
                ++i;
            }
            *static_cast<bool*>(spec->target) = parsedBool;
            continue;
        }

        std::string_view valueText;
        if (hasInlineValue)
        {
            valueText = inlineValue;
        }
        else if (i + 1 < argc && argv[i + 1] != nullptr)
        {
            valueText = argv[++i];
        }
        else
        {
            FailCli(std::string("Option '--") + spec->name + "' is missing an argument");
        }

        switch (spec->kind)
        {
        case EOptKind::UInt32:
            if (!TryParseUInt32(valueText, *static_cast<uint32_t*>(spec->target)))
            {
                FailCli(std::string("Argument '") + std::string(valueText) + "' failed to parse for --" + spec->name);
            }
            break;
        case EOptKind::Float:
            if (!TryParseFloat(valueText, *static_cast<float*>(spec->target)))
            {
                FailCli(std::string("Argument '") + std::string(valueText) + "' failed to parse for --" + spec->name);
            }
            break;
        case EOptKind::String:
            *static_cast<std::string*>(spec->target) = std::string(valueText);
            break;
        case EOptKind::StringVec:
            static_cast<std::vector<std::string>*>(spec->target)->emplace_back(valueText);
            break;
        default:
            break;
        }
    }

    if (SyncValidation)
    {
        Validation = true;
    }
    if (Dedicated)
    {
        HiddenWindow = true;
        ForceNoRT = true;
        DedicatedTickRate = std::clamp<uint32_t>(DedicatedTickRate, 1, 240);
    }
    DisableStreamline = DisableStreamline || disableStreamlineForApplication || AgentValidation || Dedicated;
    DisableFidelityFX = DisableFidelityFX || AgentValidation || Dedicated;

    if (VulkanDriver != "native" && VulkanDriver != "lvp" && VulkanDriver != "dozen")
    {
        Throw(std::out_of_range("Invalid --vulkan-driver. Expected native, lvp, or dozen."));
    }
    if (VulkanDriver == "lvp" || VulkanDriver == "dozen")
    {
        DisableStreamline = true;
        DisableFidelityFX = true;
    }

    if (noShaderHotReload)
    {
        ShaderHotReload = false;
    }

    if (noCppLiveCoding || AgentValidation)
    {
        CppLiveCoding = false;
    }

    if (ShaderHotReloadInterval < 0.1f)
    {
        ShaderHotReloadInterval = 0.1f;
    }

    if (!remoteResolution.empty())
    {
        const size_t separator = remoteResolution.find('x');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= remoteResolution.size())
        {
            Throw(std::out_of_range("Invalid --remote-res. Expected WIDTHxHEIGHT."));
        }
        uint32_t rw = 0;
        uint32_t rh = 0;
        const std::string_view resView(remoteResolution);
        if (!TryParseUInt32(resView.substr(0, separator), rw) ||
            !TryParseUInt32(resView.substr(separator + 1), rh))
        {
            Throw(std::out_of_range("Invalid --remote-res. Expected WIDTHxHEIGHT."));
        }
        RemoteWidth = rw;
        RemoteHeight = rh;
    }

    if (RemoteMode)
    {
        ForceSDR = true;
        if (!RemoteShowWindow)
        {
            HiddenWindow = true;
        }
        if (RemoteFps == 0)
        {
            RemoteFps = 60;
        }
        if (RemoteHttpPort > 65535 || RemotePort > 65535)
        {
            Throw(std::out_of_range("Remote Play ports must be in range 0..65535."));
        }
        if (RemoteEncoder != "auto" && RemoteEncoder != "vulkan")
        {
            Throw(std::out_of_range("Invalid --remote-encoder. Expected auto or vulkan."));
        }
        if (RemoteMaxClients == 0)
        {
            Throw(std::out_of_range("Remote Play --remote-max-clients must be at least 1."));
        }
    }
    else if (RemoteMultiView)
    {
        Throw(std::out_of_range("--remote-multiview requires --remote."));
    }

    if (Tui)
    {
        ForceSDR = true;
        HiddenWindow = true;
        if (TuiFps == 0)
        {
            TuiFps = 30;
        }
        TuiSsaa = std::clamp(TuiSsaa, 1u, 16u);
        if (TuiCellMode == "half-block")
        {
            TuiCellMode = "half";
        }
        if (TuiCellMode != "half" && TuiCellMode != "quadrant")
        {
            Throw(std::out_of_range("Invalid --tui-cell-mode. Expected half or quadrant."));
        }
    }

    if (PresentMode > 3)
    {
        Throw(std::out_of_range("Invalid present mode."));
    }

#if GK_WITH_VITURE
    if (!std::isfinite(ArPredictionMs) || ArPredictionMs < 0.0f || ArPredictionMs > 100.0f)
    {
        Throw(std::out_of_range("Invalid --ar-prediction-ms. Expected a value from 0 to 100."));
    }
    if (!std::isfinite(ArSmoothingHz) || ArSmoothingHz < 0.0f || ArSmoothingHz > 240.0f)
    {
        Throw(std::out_of_range("Invalid --ar-smoothing-hz. Expected a value from 0 to 240."));
    }
    if (ArDof != 3 && ArDof != 6)
    {
        Throw(std::out_of_range("Invalid --ar-dof. Expected 3 or 6."));
    }
#endif
}

}
