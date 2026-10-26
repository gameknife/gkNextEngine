#include "Application/Game/NextRaiden/Player/PlayerShip.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Application/Game/NextRaiden/Combat/BulletPool.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

namespace NextRaiden
{
    namespace
    {
        constexpr float kBlinkPeriod = 0.12f;
    }

    void FPlayerShip::Configure(const FPlayerConfig& player, const FPlayfieldConfig& field,
                                const FWeaponConfig& weapon, const FSurvivalConfig& survival)
    {
        config_ = player;
        field_ = field;
        weapon_ = weapon;
        survival_ = survival;
    }

    void FPlayerShip::Bind(std::shared_ptr<Assets::Node> node)
    {
        node_ = std::move(node);
        if (node_)
        {
            Assets::NodeUtils::SetVisibleRecursive(node_, true);
        }
    }

    void FPlayerShip::BindOptions(std::vector<std::shared_ptr<Assets::Node>> nodes)
    {
        optionNodes_ = std::move(nodes);
        optionPositions_.assign(optionNodes_.size(), position_);
        for (const std::shared_ptr<Assets::Node>& node : optionNodes_)
        {
            Assets::NodeUtils::SetVisibleRecursive(node, false);
        }
    }

    void FPlayerShip::Unbind()
    {
        node_.reset();
        optionNodes_.clear();
        optionPositions_.clear();
    }

    bool FPlayerShip::AddOption()
    {
        if (options_ >= static_cast<int>(optionNodes_.size()))
        {
            return false;
        }
        ++options_;
        // Placed on top of the player so it flies out to its station rather than appearing
        // there, which is the whole reason the wingmen lag in the first place.
        if (static_cast<size_t>(options_ - 1) < optionPositions_.size())
        {
            optionPositions_[static_cast<size_t>(options_ - 1)] = position_;
        }
        return true;
    }

    void FPlayerShip::Reset(float centreZ)
    {
        lives_ = survival_.Lives;
        bombs_ = survival_.Bombs;
        power_ = 1;
        options_ = 0;
        state_ = EPlayerState::Flying;
        Respawn(centreZ);
    }

    void FPlayerShip::Respawn(float centreZ)
    {
        position_ = glm::vec3(0.0f, field_.FlightAltitude, centreZ + config_.SpawnBehind);
        velocity_ = glm::vec3(0.0f);
        bank_ = 0.0f;
        fireTimer_ = 0.0f;
        optionFireTimer_ = 0.0f;
        optionPositions_.assign(optionNodes_.size(), position_);
        shield_ = survival_.Shield;
        invulnerable_ = survival_.InvulnerableSeconds;
        respawnTimer_ = 0.0f;
        blinkTimer_ = 0.0f;
        state_ = EPlayerState::Flying;
        Apply();
    }

    void FPlayerShip::SetPower(int power)
    {
        power_ = std::clamp(power, 1, static_cast<int>(weapon_.Levels.size()));
    }

    void FPlayerShip::AddBomb(int delta)
    {
        bombs_ = std::clamp(bombs_ + delta, 0, 5);
    }

    void FPlayerShip::AddShield(int delta)
    {
        shield_ = std::clamp(shield_ + delta, 0, survival_.Shield);
    }

    bool FPlayerShip::TakeHit()
    {
        if (!IsVulnerable())
        {
            return false;
        }
        --shield_;
        if (shield_ > 0)
        {
            invulnerable_ = survival_.InvulnerableSeconds;
            return false;
        }

        --lives_;
        SetPower(power_ - survival_.PowerLossOnDeath);
        state_ = lives_ > 0 ? EPlayerState::Dead : EPlayerState::GameOver;
        respawnTimer_ = survival_.RespawnSeconds;
        return true;
    }

    bool FPlayerShip::ConsumeBomb()
    {
        if (state_ != EPlayerState::Flying || bombs_ <= 0)
        {
            return false;
        }
        --bombs_;
        invulnerable_ = std::max(invulnerable_, survival_.BombInvulnerableSeconds);
        return true;
    }

    void FPlayerShip::FireWeapon(FBulletPool& bullets)
    {
        const FWeaponLevel& level = weapon_.Levels[static_cast<size_t>(std::clamp(power_, 1, 4) - 1)];
        const int shots = std::max(1, level.Shots);
        // Forward for the player is -Z, which is the one place in this game where the kit's
        // "everything points at +Z" convention does not apply.
        const glm::vec3 muzzle = position_ + glm::vec3(0.0f, 0.0f, -weapon_.MuzzleForward);

        for (int i = 0; i < shots; ++i)
        {
            const float centred = static_cast<float>(i) - static_cast<float>(shots - 1) * 0.5f;
            const float angle = shots > 1 ? centred * (level.ArcDegrees / static_cast<float>(shots - 1)) : 0.0f;
            const float radians = glm::radians(angle);

            FBulletSeed seed;
            seed.Position = muzzle + glm::vec3(centred * weapon_.SpreadOffset, 0.0f, 0.0f);
            // Negated because the shot travels up the screen: the pattern helpers measure
            // angles from +Z, and the player is the one shooting the other way.
            seed.Velocity = glm::vec3(std::sin(radians), 0.0f, -std::cos(radians)) * weapon_.BulletSpeed;
            seed.Visual = EBulletVisual::Bolt;
            seed.Damage = level.Damage;
            bullets.Emit(seed, false, weapon_.BulletRadius);
        }
    }

    void FPlayerShip::Step(const FPlayerInput& input, float centreZ, float deltaSeconds, FBulletPool& bullets)
    {
        invulnerable_ = std::max(0.0f, invulnerable_ - deltaSeconds);
        blinkTimer_ += deltaSeconds;

        if (state_ != EPlayerState::Flying)
        {
            respawnTimer_ = std::max(0.0f, respawnTimer_ - deltaSeconds);
            velocity_ = glm::vec3(0.0f);
            // Keep drifting with the camera so the wreck does not get left behind the screen
            // edge while the respawn timer runs.
            position_.z = std::clamp(position_.z, centreZ - field_.AheadLimit, centreZ + field_.BehindLimit);
            return;
        }

        glm::vec2 move = input.Move;
        const float length = glm::length(move);
        if (length > 1.0f)
        {
            move /= length;
        }

        const float speed = config_.Speed * (input.Slow ? config_.SlowScale : 1.0f);
        const glm::vec3 previous = position_;
        position_.x += move.x * speed * deltaSeconds;
        position_.z += move.y * speed * deltaSeconds;
        position_.y = field_.FlightAltitude;

        position_.x = std::clamp(position_.x, -field_.HalfWidth, field_.HalfWidth);
        // Ahead is -Z (up the screen); behind is +Z. The bounds travel with the camera, so
        // standing still means being pushed off the bottom edge.
        position_.z = std::clamp(position_.z, centreZ - field_.AheadLimit, centreZ + field_.BehindLimit);
        velocity_ = deltaSeconds > 0.0f ? (position_ - previous) / deltaSeconds : glm::vec3(0.0f);

        // Bank into the turn: moving towards screen-right dips the ship's right-hand side.
        // Purely cosmetic, but it is what makes a shmup ship read as flying rather than
        // sliding. The sign is the screen's, not the airframe's - the ship carries yaw = pi,
        // so its own left/right are the mirror of the player's.
        const float targetBank = move.x * config_.BankDegrees;
        const float step = config_.BankRateDegrees * deltaSeconds;
        bank_ = std::clamp(targetBank, bank_ - step, bank_ + step);

        fireTimer_ = std::max(0.0f, fireTimer_ - deltaSeconds);
        if (input.Fire && fireTimer_ <= 0.0f)
        {
            FireWeapon(bullets);
            fireTimer_ = weapon_.IntervalSeconds;
        }

        StepOptions(input, deltaSeconds, bullets);
    }

    void FPlayerShip::StepOptions(const FPlayerInput& input, float deltaSeconds, FBulletPool& bullets)
    {
        optionFireTimer_ = std::max(0.0f, optionFireTimer_ - deltaSeconds);
        const bool fire = input.Fire && optionFireTimer_ <= 0.0f;
        if (fire)
        {
            optionFireTimer_ = weapon_.OptionIntervalSeconds;
        }

        for (int i = 0; i < static_cast<int>(optionPositions_.size()); ++i)
        {
            if (i >= options_)
            {
                continue;
            }
            // Left of the player, then right; the offset is behind the wing so the wingmen do
            // not cover the ship the player is trying to aim with.
            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
            const float row = static_cast<float>(i / 2);
            const glm::vec3 station = position_ + glm::vec3(side * weapon_.OptionOffsetX, 0.0f,
                                                            weapon_.OptionOffsetZ + row * 2.2f);
            // Exponential follow: frame-rate independent because the step is fixed, and the
            // lag is what sells them as separate aircraft.
            const float blend = std::clamp(weapon_.OptionFollowRate * deltaSeconds, 0.0f, 1.0f);
            optionPositions_[static_cast<size_t>(i)] += (station - optionPositions_[static_cast<size_t>(i)]) * blend;

            if (fire)
            {
                FBulletSeed seed;
                seed.Position = optionPositions_[static_cast<size_t>(i)] + glm::vec3(0.0f, 0.0f, -1.6f);
                seed.Velocity = glm::vec3(0.0f, 0.0f, -weapon_.OptionBulletSpeed);
                seed.Visual = EBulletVisual::Laser;
                seed.Damage = weapon_.OptionDamage;
                bullets.Emit(seed, false, weapon_.BulletRadius);
            }
        }
    }

    void FPlayerShip::Apply()
    {
        if (!node_)
        {
            return;
        }

        // Dead ships are gone; invulnerable ones blink, which is the only signal the player
        // gets that the last hit did not count.
        const bool blinkOff = invulnerable_ > 0.0f && std::fmod(blinkTimer_, kBlinkPeriod * 2.0f) > kBlinkPeriod;
        Assets::NodeUtils::SetVisibleRecursive(node_, state_ == EPlayerState::Flying && !blinkOff);

        // yaw pi: the kit's airframes point at engine +Z (towards the player's side of the
        // screen), and the player is the one that has to look the other way.
        const glm::quat yaw = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat roll = glm::angleAxis(glm::radians(bank_), glm::vec3(0.0f, 0.0f, 1.0f));
        node_->SetTransform(position_, yaw * roll, glm::vec3(1.0f));

        for (int i = 0; i < static_cast<int>(optionNodes_.size()); ++i)
        {
            const std::shared_ptr<Assets::Node>& option = optionNodes_[static_cast<size_t>(i)];
            const bool live = i < options_ && state_ == EPlayerState::Flying;
            Assets::NodeUtils::SetVisibleRecursive(option, live);
            if (live)
            {
                option->SetTransform(optionPositions_[static_cast<size_t>(i)], yaw, glm::vec3(1.0f));
            }
        }
    }
}
