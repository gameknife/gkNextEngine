#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextDotNet/ManagedGameManifest.hpp"

#include <imgui.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Modules::NextDotNet
{
    class ManagedGameSession;

    /// Lifecycle states of an asynchronous C# project build/publish task.
    enum class EManagedBuildState : uint8_t
    {
        Idle,
        Building,
        Succeeded,
        Failed,
    };

    /// Final outcome summary of a build task.
    struct FManagedBuildResult
    {
        bool success = false;
        std::string gameId;
        std::string targetName;
        std::string error;
        double elapsedSeconds = 0.0;
    };

    /// Public asynchronous C# project build/publish task manager & progress UI.
    ///
    /// Manages the background compilation thread and delivers uniform, polished
    /// progress presentation across gkNextLauncher, gkNextEditor, and NewGameProjectDialog.
    class FManagedProjectBuilder
    {
    public:
        FManagedProjectBuilder();
        ~FManagedProjectBuilder();

        GK_NON_COPIABLE(FManagedProjectBuilder)

        /// Initiates an asynchronous publish/rebuild on a background thread.
        /// Returns true if the build was successfully dispatched, or false if already running.
        bool StartBuild(ManagedGameSession* session, const FManagedGameManifest& manifest,
                        std::string_view targetDisplayName = {});

        /// Resets the task back to Idle state. Waits for worker thread completion if active.
        void Reset();

        /// Status queries
        bool IsBusy() const;
        bool HasFinished() const;
        bool IsSuccess() const;
        EManagedBuildState GetState() const;
        const std::string& GetTargetName() const;
        const std::string& GetErrorMessage() const;
        double GetElapsedSeconds() const;

        /// Draws the progress view embedded inside an existing window/panel (e.g. NewGameProjectDialog).
        /// Returns true when the build finishes on this frame.
        bool DrawInlineProgress(ImVec2 size = ImVec2(0.0f, 0.0f));

        /// Draws a centered modal progress popup (e.g. for standalone Rebuild C# in Launcher or Editor).
        /// Returns true when the build is finished and the modal is dismissed.
        bool DrawModalProgress(const char* popupId = "##ManagedBuildModal");

    private:
        void DrawProgressCore(float cardWidth, bool inlineMode);

        std::thread workerThread_;
        mutable std::mutex mutex_;

        std::atomic<EManagedBuildState> state_{EManagedBuildState::Idle};
        std::chrono::steady_clock::time_point startTime_{};
        double finalElapsedSeconds_ = 0.0;

        FManagedGameManifest manifest_;
        std::string targetName_;
        std::string errorMessage_;

        bool modalOpenRequested_ = false;
        float successDismissTimer_ = -1.0f;
    };
}
