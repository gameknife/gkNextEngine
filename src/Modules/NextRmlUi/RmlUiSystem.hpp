#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/UiHost.hpp"

#include <functional>
#include <string>

union SDL_Event;

namespace Rml
{
    class Element;
    class ElementDocument;
}

class NextEngine;

namespace NextUI
{
    class RmlUiSystem final : public Runtime::IUiLayer
    {
    public:
        static constexpr std::string_view LayerId = "rmlui";

        GK_NON_COPIABLE(RmlUiSystem)

        explicit RmlUiSystem(NextEngine& engine);
        ~RmlUiSystem() override;

        // Runtime::IUiLayer
        std::string_view GetLayerId() const override { return LayerId; }
        int32_t GetOrder() const override { return static_cast<int32_t>(Runtime::EUiLayerOrder::Document); }
        bool HandleEvent(const SDL_Event& event) override;
        void BeginFrame(glm::vec2 logicalSize, float dpiScale) override;
        void RenderLayer(NextEngine& engine) override;
        bool WantsToCaptureMouse() const override;
        bool WantsToCaptureKeyboard() const override;

        bool IsAvailable() const;
        void Shutdown();

        Rml::ElementDocument* EnsureDocument(const std::string& documentId, const std::string& rmlSource);
        Rml::ElementDocument* ReplaceDocument(const std::string& documentId, const std::string& rmlSource);
        Rml::Element* GetElementById(const std::string& documentId, const std::string& elementId);
        void SetDocumentVisible(const std::string& documentId, bool visible);
        void ClearEventListeners();
        bool ListenClick(const std::string& documentId, const std::string& elementId, std::function<void()> callback);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
