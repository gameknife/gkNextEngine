#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Interface/UiHost.hpp"
#include "Modules/NextUI/Hud.hpp"
#include "Modules/NextUI/NextUI.hpp"
#include "Modules/NextUI/UiRenderer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

TEST_CASE("UI metrics scale deterministically", "[Unit][UI]")
{
    using NextUI::FUiMetrics;
    const FUiMetrics one = FUiMetrics::FromScale(1.0f);
    const FUiMetrics oneAndHalf = FUiMetrics::FromScale(1.5f);
    const FUiMetrics two = FUiMetrics::FromScale(2.0f);

    CHECK(one.titleBarHeight == 48.0f);
    CHECK(oneAndHalf.titleBarHeight == 72.0f);
    CHECK(two.toolbarHeight == 76.0f);
    CHECK(two.spacing == 12.0f);
}

TEST_CASE("UI renderer texture handle encoding preserves slot and blend flags", "[Unit][UI]")
{
    using namespace NextUI;

    uint32_t textureIndex = 0;
    uint32_t flags = 0;
    CHECK_FALSE(FUiRenderer::DecodeTextureHandle(0, textureIndex, flags));

    const uint64_t straight = FUiRenderer::EncodeTextureHandle(42u, UI_BATCH_FLAG_STRAIGHT_ALPHA);
    CHECK(FUiRenderer::DecodeTextureHandle(straight, textureIndex, flags));
    CHECK(textureIndex == 42u);
    CHECK(flags == UI_BATCH_FLAG_STRAIGHT_ALPHA);

    const uint64_t premul = FUiRenderer::EncodeTextureHandle(1024u, UI_BATCH_FLAG_NONE);
    CHECK(FUiRenderer::DecodeTextureHandle(premul, textureIndex, flags));
    CHECK(textureIndex == 1024u);
    CHECK(flags == UI_BATCH_FLAG_NONE);

    const uint64_t rawHdr = FUiRenderer::EncodeTextureHandle(7u, UI_BATCH_FLAG_STRAIGHT_ALPHA | UI_BATCH_FLAG_RAW_OUTPUT);
    CHECK(FUiRenderer::DecodeTextureHandle(rawHdr, textureIndex, flags));
    CHECK(textureIndex == 7u);
    CHECK(flags == (UI_BATCH_FLAG_STRAIGHT_ALPHA | UI_BATCH_FLAG_RAW_OUTPUT));
}

TEST_CASE("UI scoped containers restore ImGui stacks", "[Unit][UI]")
{
    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    REQUIRE(fontPixels != nullptr);
    ImGui::NewFrame();

    const int initialColorStack = context->ColorStack.Size;
    const int initialStyleStack = context->StyleVarStack.Size;
    {
        NextUI::FOverlayPanelConfig config;
        config.WindowId = "ScopedOverlayTest";
        config.Position = ImVec2(10.0f, 10.0f);
        config.Size = ImVec2(240.0f, 120.0f);
        config.Padding = ImVec2(11.0f, 7.0f);
        config.BorderSize = 0.0f;
        NextUI::FScopedOverlayPanel panel(config);
        CHECK(context->StyleVarStack.Size == initialStyleStack + 4);
        CHECK(context->ColorStack.Size == initialColorStack + 2);
    }
    CHECK(context->StyleVarStack.Size == initialStyleStack);
    CHECK(context->ColorStack.Size == initialColorStack);

    {
        NextUI::ApplyTheme();
        CHECK(ImGui::GetStyle().ChildBorderSize == 0.0f);
        NextUI::FScopedWindow hostWindow("InsetHostWindow");
        NextUI::FScopedInsetPanel inset("ScopedInsetTest", ImVec2(120.0f, 60.0f), true);
        CHECK(ImGui::GetStyle().ChildBorderSize == 1.0f);
        CHECK(context->StyleVarStack.Size == initialStyleStack + 2);
        CHECK(context->ColorStack.Size == initialColorStack + 2);
    }
    CHECK(context->StyleVarStack.Size == initialStyleStack);
    CHECK(context->ColorStack.Size == initialColorStack);

    ImGui::EndFrame();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previousContext);
}

TEST_CASE("UI frame layers are explicit and policy composable", "[Unit][UI]")
{
    using namespace Runtime;
    const FUiFrameResult editorResult{EUiDeveloperLayer::Console};
    CHECK(HasUiLayer(editorResult.requestedDeveloperLayers, EUiDeveloperLayer::Console));
    CHECK_FALSE(HasUiLayer(editorResult.requestedDeveloperLayers, EUiDeveloperLayer::Statistics));
    CHECK_FALSE(HasUiLayer(editorResult.requestedDeveloperLayers, EUiDeveloperLayer::Memory));

    const FUiFrameResult fullDeveloperUi{EUiDeveloperLayer::All};
    CHECK(HasUiLayer(fullDeveloperUi.requestedDeveloperLayers, EUiDeveloperLayer::Statistics));
    CHECK(HasUiLayer(fullDeveloperUi.requestedDeveloperLayers, EUiDeveloperLayer::Console));
    CHECK(HasUiLayer(fullDeveloperUi.requestedDeveloperLayers, EUiDeveloperLayer::Memory));

    const FUiFramePolicy remotePolicy{.allowApplicationUi = true,
                                      .allowedDeveloperLayers = EUiDeveloperLayer::None};
    CHECK((editorResult.requestedDeveloperLayers & remotePolicy.allowedDeveloperLayers) ==
          EUiDeveloperLayer::None);
    CHECK(FUiFrameResult::ForApplicationHandled().requestedDeveloperLayers == EUiDeveloperLayer::None);
}

TEST_CASE("Bottom bar stays attached to the main viewport", "[Unit][UI]")
{
    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    REQUIRE(fontPixels != nullptr);
    ImGui::NewFrame();

    bool contentDrawn = false;
    NextUI::FBottomBarConfig config;
    config.WindowId = "BottomBarViewportTest";
    config.Height = 30.0f;
    config.DrawLeftContent = [&contentDrawn]() { contentDrawn = true; };
    NextUI::DrawBottomBar(config);

    ImGuiWindow* window = ImGui::FindWindowByName(config.WindowId);
    REQUIRE(window != nullptr);
    CHECK(contentDrawn);
    CHECK(window->Viewport == ImGui::GetMainViewport());
    CHECK((window->Flags & ImGuiWindowFlags_NoDocking) != 0);
    CHECK((window->Flags & ImGuiWindowFlags_NoScrollbar) != 0);
    CHECK(window->Pos.y == 690.0f);
    CHECK(window->Size.y == 30.0f);

    ImGui::EndFrame();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previousContext);
}

TEST_CASE("UI HUD helpers and NotificationCenter work deterministically", "[Unit][UI]")
{
    using namespace NextUI;

    const ImVec4 highHp = Hud::GetHealthColor(0.8f);
    const ImVec4 midHp = Hud::GetHealthColor(0.4f);
    const ImVec4 lowHp = Hud::GetHealthColor(0.1f);
    const ImVec4 expectedHigh = Color(EColor::Success);
    const ImVec4 expectedMid = Color(EColor::Warning);
    const ImVec4 expectedLow = Color(EColor::Danger);

    CHECK(highHp.x == expectedHigh.x);
    CHECK(midHp.x == expectedMid.x);
    CHECK(lowHp.x == expectedLow.x);

    FNotificationCenter center;
    center.Push("Scene saved", ENotificationKind::Info, 1000.0f);
    CHECK(center.ActiveCount() == 1);
    center.Update(500.0f);
    CHECK(center.ActiveCount() == 1);
    center.Update(600.0f);
    CHECK(center.ActiveCount() == 0);
}
