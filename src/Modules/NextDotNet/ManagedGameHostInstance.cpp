#include "Modules/NextDotNet/ManagedGameHostInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/NextDotNetModule.hpp"

namespace Modules::NextDotNet
{
    ManagedGameHostInstance::ManagedGameHostInstance(Vulkan::WindowConfig& config,
                                                     Runtime::Config::Options& options,
                                                     NextEngine* engine,
                                                     FManagedGameHostOptions hostOptions)
        : NextGameInstanceBase(config, options, engine)
        , hostOptions_(std::move(hostOptions))
        , session_(*engine)
    {
        FManagedGameManifest::FWindow window = hostOptions_.window;
        bool compileManagedSources = false;

        bootManifest_ = LoadBootManifest();
        if (bootManifest_)
        {
            window = bootManifest_->window;
            compileManagedSources = bootManifest_->compileManagedSources;
        }

        const std::string manifestIcon = bootManifest_ ? bootManifest_->icon : "";
        ConfigureWindow(config, options, window.title, window.width, window.height, window.forceSDR, manifestIcon);

        // The runtime starts idle on purpose. Which assembly is loaded, and whether it hot reloads,
        // is the session's decision — the same decision whether this host runs one game forever or
        // swaps between several.
        Install(*engine,
                {
                    .gameAssembly = "",
                    .compileManagedSources = compileManagedSources,
                    .enableHotReload = false,
                });

        session_.SetLinkedModules(hostOptions_.linkedModules);
    }

    ManagedGameHostInstance::~ManagedGameHostInstance() = default;

    std::optional<FManagedGameManifest> ManagedGameHostInstance::LoadBootManifest() const
    {
        if (!hostOptions_.gameId.empty())
        {
            const std::vector<FManagedGameManifest> manifests = ScanManagedGameManifests();
            const auto it = std::find_if(manifests.begin(), manifests.end(),
                                         [this](const FManagedGameManifest& manifest)
                                         {
                                             return manifest.id == hostOptions_.gameId;
                                         });
            if (it != manifests.end())
            {
                return *it;
            }

            SPDLOG_ERROR("[game] starting without a game: no manifest with id '{}' was found",
                         hostOptions_.gameId);
            return std::nullopt;
        }

        if (!hostOptions_.manifestPath.empty())
        {
            if (auto manifest = LoadManagedGameManifest(hostOptions_.manifestPath))
            {
                return manifest;
            }
            // Keep going with the fallback window: an engine that starts and says the manifest
            // is broken is more useful than one that dies before it can log anything.
            SPDLOG_ERROR("[game] starting without a game: {} could not be loaded",
                         hostOptions_.manifestPath);
        }
        return std::nullopt;
    }

    void ManagedGameHostInstance::OnInit()
    {
        session_.OnHostInit();

        if (bootManifest_)
        {
            session_.RequestLoad(*bootManifest_);
        }
    }

    void ManagedGameHostInstance::OnTick(double deltaSeconds)
    {
        session_.OnHostTick(deltaSeconds);
    }

    void ManagedGameHostInstance::OnDestroy()
    {
        session_.OnHostDestroy();
    }

    bool ManagedGameHostInstance::OnRenderUI()
    {
        const bool hostConsumed = OnHostRenderUI();
        const bool gameConsumed = session_.OnRenderUI();
        return hostConsumed || gameConsumed;
    }

    bool ManagedGameHostInstance::OnKey(SDL_Event& event)
    {
        return OnHostKey(event);
    }

    bool ManagedGameHostInstance::OnMouseButton(SDL_Event&)
    {
        return false;
    }

    bool ManagedGameHostInstance::OnTouch(SDL_Event& event)
    {
#if IOS || ANDROID
        if (!UsesDualStickTouch())
        {
            return false;
        }

        const SDL_TouchFingerEvent& touch = event.tfinger;
        if (event.type == SDL_EVENT_FINGER_DOWN)
        {
            uint64_t& activeFinger = touch.x < 0.5f ? moveFinger_ : lookFinger_;
            if (activeFinger == 0)
            {
                activeFinger = touch.fingerID;
                if (touch.x < 0.5f)
                {
                    moveCenter_ = {touch.x, touch.y};
                    touchLeftX_ = 0;
                    touchLeftY_ = 0;
                }
                else
                {
                    lookCenter_ = {touch.x, touch.y};
                    touchRightX_ = 0;
                    touchRightY_ = 0;
                }
                PublishGamepadInput();
            }
            return true;
        }

        if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
        {
            if (moveFinger_ == touch.fingerID)
            {
                moveFinger_ = 0;
                touchLeftX_ = 0;
                touchLeftY_ = 0;
            }
            if (lookFinger_ == touch.fingerID)
            {
                lookFinger_ = 0;
                touchRightX_ = 0;
                touchRightY_ = 0;
            }
            PublishGamepadInput();
            return true;
        }

        if (event.type != SDL_EVENT_FINGER_MOTION)
        {
            return true;
        }

        const VkExtent2D windowSize = GetEngine().GetWindow().WindowSize();
        const float width = static_cast<float>(std::max(1u, windowSize.width));
        const float height = static_cast<float>(std::max(1u, windowSize.height));
        if (moveFinger_ == touch.fingerID)
        {
            SetTouchStick((touch.x - static_cast<float>(moveCenter_.x)) * width,
                          (touch.y - static_cast<float>(moveCenter_.y)) * height,
                          touchLeftX_, touchLeftY_);
            PublishGamepadInput();
        }
        else if (lookFinger_ == touch.fingerID)
        {
            SetTouchStick((touch.x - static_cast<float>(lookCenter_.x)) * width,
                          (touch.y - static_cast<float>(lookCenter_.y)) * height,
                          touchRightX_, touchRightY_);
            PublishGamepadInput();
        }
        return true;
#else
        (void)event;
        return false;
#endif
    }

    bool ManagedGameHostInstance::OnGamepadInput(int16_t leftStickX,
                                                 int16_t leftStickY,
                                                 int16_t rightStickX,
                                                 int16_t rightStickY,
                                                 int16_t leftTrigger,
                                                 int16_t rightTrigger)
    {
        if (!UsesDualStickTouch())
        {
            session_.SetGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY,
                                     leftTrigger, rightTrigger);
            return false;
        }

        physicalLeftX_ = leftStickX;
        physicalLeftY_ = leftStickY;
        physicalRightX_ = rightStickX;
        physicalRightY_ = rightStickY;
        physicalLeftTrigger_ = leftTrigger;
        physicalRightTrigger_ = rightTrigger;
        PublishGamepadInput();
        return false;
    }

    int16_t ManagedGameHostInstance::ToGamepadAxis(float value)
    {
        constexpr float axisMax = 32767.0f;
        return static_cast<int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * axisMax));
    }

    int16_t ManagedGameHostInstance::CombineGamepadAxes(int16_t physical, int16_t touch)
    {
        return static_cast<int16_t>(std::clamp(static_cast<int32_t>(physical) + touch, -32767, 32767));
    }

    bool ManagedGameHostInstance::UsesDualStickTouch() const
    {
        return bootManifest_ && bootManifest_->mobileControls == EMobileControls::DualStick;
    }

    void ManagedGameHostInstance::SetTouchStick(float deltaX, float deltaY,
                                                int16_t& outX, int16_t& outY) const
    {
        constexpr float joystickRadiusFraction = 0.20f;
        const VkExtent2D windowSize = GetEngine().GetWindow().WindowSize();
        const float radius = std::min(static_cast<float>(std::max(1u, windowSize.width)),
                                      static_cast<float>(std::max(1u, windowSize.height))) *
                             joystickRadiusFraction;
        const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        if (distance <= 0.0001f || radius <= 0.0f)
        {
            outX = 0;
            outY = 0;
            return;
        }

        const float magnitude = std::min(distance / radius, 1.0f);
        outX = ToGamepadAxis((deltaX / distance) * magnitude);
        // SDL gamepad Y points down; MoveAxis.Poll() converts it to forward-positive.
        outY = ToGamepadAxis((deltaY / distance) * magnitude);
    }

    void ManagedGameHostInstance::PublishGamepadInput()
    {
        session_.SetGamepadInput(CombineGamepadAxes(physicalLeftX_, touchLeftX_),
                                 CombineGamepadAxes(physicalLeftY_, touchLeftY_),
                                 CombineGamepadAxes(physicalRightX_, touchRightX_),
                                 CombineGamepadAxes(physicalRightY_, touchRightY_),
                                 physicalLeftTrigger_, physicalRightTrigger_);
    }

    void ManagedGameHostInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                      std::vector<Assets::Model>& models,
                                                      std::vector<Assets::FMaterial>& materials,
                                                      std::vector<Assets::LightObject>& lights,
                                                      std::vector<Assets::AnimationTrack>& tracks)
    {
        session_.OnBeforeSceneRebuild(nodes, models, materials, lights, tracks);
    }

    void ManagedGameHostInstance::OnSceneLoaded()
    {
        session_.OnSceneLoaded();
    }

    bool ManagedGameHostInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
    {
        return session_.TryGetOverrideCamera(outRenderCamera);
    }
}
