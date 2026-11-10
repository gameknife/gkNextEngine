#pragma once

// ============================================================================
// PlayerShip.hpp - The player's ship: intent in, clamped position and a banked
// node transform out. P0 scope is movement only; weapons, shield, options and
// bombs land in P1 on top of the same Step/Apply split.
//
// The ship is the one kit object that faces *away* from the camera's forward:
// everything in kit_raiden points at SCAD -Y (engine +Z, screen down) so that
// enemies need no rotation, which leaves the player at yaw = pi.
// ============================================================================

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/NextRaidenConfig.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

namespace NextRaiden
{
    class FBulletPool;

    struct FPlayerInput
    {
        /// Normalised intent: +X right, +Z towards the bottom of the screen.
        glm::vec2 Move{0.0f};
        bool Slow = false;
        bool Fire = false;
        bool BombPressed = false;
    };

    enum class EPlayerState : uint8_t
    {
        Flying,
        Dead,     // waiting out the respawn delay
        GameOver,
    };

    class FPlayerShip
    {
    public:
        void Configure(const FPlayerConfig& player, const FPlayfieldConfig& field, const FWeaponConfig& weapon,
                       const FSurvivalConfig& survival);
        void Bind(std::shared_ptr<Assets::Node> node);
        /// Wingman nodes the player earns with option pickups. They are bound once and simply
        /// stay hidden until the corresponding option is collected.
        void BindOptions(std::vector<std::shared_ptr<Assets::Node>> nodes);
        void Unbind();

        /// Full reset: position, lives, shield, bombs, power. A new stage, or a new run.
        void Reset(float centreZ);
        /// Back into the fight after a death: position and invulnerability only.
        void Respawn(float centreZ);

        void Step(const FPlayerInput& input, float centreZ, float deltaSeconds, FBulletPool& bullets);
        /// Writes the node transform. Separate from Step so a frame of several fixed steps
        /// only touches the scene graph once.
        void Apply();

        const glm::vec3& Position() const { return position_; }
        const glm::vec3& Velocity() const { return velocity_; }
        float BankDegrees() const { return bank_; }
        bool HasVisual() const { return node_ != nullptr; }

        EPlayerState State() const { return state_; }
        bool IsVulnerable() const { return state_ == EPlayerState::Flying && invulnerable_ <= 0.0f && !godMode_; }
        /// Takes one hit. Returns true when that emptied the shield and cost a life.
        bool TakeHit();
        /// True when the bomb actually went off (stock and state permitting).
        bool ConsumeBomb();

        int Lives() const { return lives_; }
        int Shield() const { return shield_; }
        int Bombs() const { return bombs_; }
        int Power() const { return power_; }
        int Options() const { return options_; }
        int MaxOptions() const { return static_cast<int>(optionNodes_.size()); }
        /// True when the option was taken; false means the player is already at the cap and
        /// the pickup should convert to score instead.
        bool AddOption();
        void SetPower(int power);
        void AddPower(int delta) { SetPower(power_ + delta); }
        void AddBomb(int delta);
        void AddShield(int delta);
        void AddLife(int delta) { lives_ += delta; }
        void SetGodMode(bool enabled) { godMode_ = enabled; }
        float InvulnerableSeconds() const { return invulnerable_; }
        float RespawnCountdown() const { return respawnTimer_; }

    private:
        void FireWeapon(FBulletPool& bullets);
        void StepOptions(const FPlayerInput& input, float deltaSeconds, FBulletPool& bullets);

        FPlayerConfig config_{};
        FPlayfieldConfig field_{};
        FWeaponConfig weapon_{};
        FSurvivalConfig survival_{};
        std::shared_ptr<Assets::Node> node_;
        std::vector<std::shared_ptr<Assets::Node>> optionNodes_;
        /// Where each wingman currently is. They lag the player rather than being welded to
        /// it, which is what makes them read as escorts instead of as parts of the sprite.
        std::vector<glm::vec3> optionPositions_;
        glm::vec3 position_{0.0f};
        glm::vec3 velocity_{0.0f};
        float bank_ = 0.0f;
        float fireTimer_ = 0.0f;
        float invulnerable_ = 0.0f;
        float respawnTimer_ = 0.0f;
        float blinkTimer_ = 0.0f;
        EPlayerState state_ = EPlayerState::Flying;
        int lives_ = 3;
        int shield_ = 3;
        int bombs_ = 3;
        int power_ = 1;
        int options_ = 0;
        float optionFireTimer_ = 0.0f;
        bool godMode_ = false;
    };
}
