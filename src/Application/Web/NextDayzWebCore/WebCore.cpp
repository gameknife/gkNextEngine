#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Combat/HitGeometry.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponRuntime.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponMounts.hpp"
#include "WebPhysics.hpp"
#include "WebCharacters.hpp"
#include "Application/Game/NextDayz/Core/RigBinary.hpp"
#include "Application/Game/NextDayz/Inventory/LootTransfer.hpp"
#include "Application/Game/NextDayz/Player/PlayerActionController.hpp"
#include "Application/Game/NextDayz/Player/FirstPersonArmPose.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"
#include "Application/Game/NextDayz/Net/ClientSession.hpp"

#include <emscripten.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>
#include <climits>
#include <deque>

namespace
{
    using namespace NextDayz;
    using namespace NextDayz::Net;
    using Json = nlohmann::json;
    using NextNet::FNetWriter;

    auto V3(const glm::vec3& vector) { return std::array{vector.x, vector.y, vector.z}; }

    glm::vec3 ReadVec(const Json& input)
    {
        const auto xyz = input.get<std::array<float, 3>>();
        if (!std::ranges::all_of(xyz, [](float value) { return std::isfinite(value); }))
            throw std::runtime_error("Invalid coordinates");
        return {xyz[0], xyz[1], xyz[2]};
    }

    Json Describe(const FLootStack& stack)
    {
        const auto* item = FindItemDef(stack.itemId);
        return {{"id", stack.itemId}, {"name", item ? item->displayName : stack.itemId},
                {"description", item ? item->description : ""}, {"count", stack.count},
                {"ammo", stack.loadedAmmo}, {"hydration", item ? item->hydrationDelta : 0.f},
                {"nutrition", item ? item->hungerDelta : 0.f}, {"area", item ? item->size.Area() : 1}};
    }

    Json DescribeStacks(const std::vector<FLootStack>& stacks)
    {
        Json out = Json::array();
        for (const auto& item : stacks) out.push_back(Describe(item));
        return out;
    }

    Json DescribeClock(const TimeSystem& time)
    {
        const auto env = time.SampleEnvironment();
        return {{"minutes", time.GameMinutes()}, {"weather", WeatherName(time.Weather())},
                {"daylight", env.daylight}, {"sunRotation", env.sunRotation}, {"hasSun", env.hasSun},
                {"sunIntensity", env.sunIntensity}, {"skyIntensity", env.skyIntensity},
                {"sunColor", V3(env.sunColor)}, {"skyColor", V3(env.skyColor)},
                {"fogColor", V3(env.fogColor)}, {"fogDensity", env.fogDensity}};
    }

    Json DescribeMounts(std::string_view weapon, const FWeaponAttachments& attachments)
    {
        Json mounts = Json::array();
        for (size_t slot = 0; slot < attachments.size(); ++slot)
        {
            const auto kind = attachments[slot];
            const bool factory = slot == 0 && kind == EWeaponAttachment::None && weapon == "svd";
            if (!factory && kind == EWeaponAttachment::None) continue;
            const auto mount = WeaponMount(weapon, static_cast<EAttachmentSlot>(slot), factory);
            mounts.push_back({{"index", factory ? 8 : static_cast<int>(kind) - 1},
                              {"position", V3(mount.position)}, {"scale", mount.scale}});
        }
        return mounts;
    }

    Json DescribePose(const FPlayerPresentationState& state, const std::vector<std::string>& clothing)
    {
        const auto& move = state.locomotion;
        unsigned coverage = 0;
        for (const auto& item : clothing)
        {
            const auto* def = FindItemDef(item);
            if (!def) continue;
            if (def->equipSlot == EEquipSlot::Torso) coverage |= 1;
            if (def->equipSlot == EEquipSlot::Legs) coverage |= 2;
        }
        Json slots = Json::array();
        for (size_t i = 0; i < state.slotWeaponIds.size(); ++i)
            slots.push_back({{"id", state.slotWeaponIds[i]},
                {"mounts", DescribeMounts(state.slotWeaponIds[i], state.slotAttachments[i])}});
        return {{"slots", slots}, {"activeSlot", state.activeWeaponSlot},
                {"weaponAction", static_cast<int>(state.weaponAction)}, {"weaponActionTime", state.weaponActionTime01},
                {"clothingCoverage", coverage}, {"stance", StanceName(move.actualStance)}, {"gait", GaitName(move.gait)},
                {"speed", move.horizontalSpeed}, {"move", {move.localMove.x, move.localMove.y}},
                {"grounded", move.onGround}, {"jump", JumpPhaseName(move.jumpPhase)}, {"jumpTime", move.jumpPhaseTime01},
                {"traversal", TraversalActionName(move.traversalAction)}, {"traversalTime", move.traversalTime01},
                {"traversalYaw", move.traversalYaw}, {"sprint", state.sprintAlpha},
                {"action", static_cast<int>(state.action)}, {"actionTime", state.actionTime01},
                {"pitch", state.aimPitchRadians}, {"aim", state.aimWeight}, {"weapon", state.weaponId}, {"clothing", clothing}};
    }

    enum EWebControl : uint32_t
    {
        MoveForward = 1u << 0, MoveBack = 1u << 1, MoveLeft = 1u << 2, MoveRight = 1u << 3,
        Walk = 1u << 4, Sprint = 1u << 5, FreeLook = 1u << 6, JumpHeld = 1u << 7, Aim = 1u << 8,
        Jump = 1u << 9, Crouch = 1u << 10, ToggleView = 1u << 11,
        Trigger = 1u << 12, TriggerPress = 1u << 13, Reload = 1u << 14,
        SlotOne = 1u << 15, SlotTwo = 1u << 16, SlotThree = 1u << 17, PreviousWeapon = 1u << 18,
    };

    struct FLootDestination
    {
        FContainerId container = 0;
        EEquipSlot slot = EEquipSlot::None;
        std::optional<FGridPlacement> placement;
    };

    struct FPendingLoot
    {
        FLootReplica spot;
        FLootDestination destination;
    };

    struct FPacket
    {
        bool reliable;
        std::vector<std::byte> bytes;
    };

    template<class Graph>
    struct FAnimatedEntity
    {
        Graph graph;
        FWebCharacterPose pose;
        uint32_t lastFrame = 0;
    };

    class WebCore
    {
    public:
        WebCore()
        {
            time_.Reset(config_.Time, 1);
            actions_.Configure(config_.Action);
            weapons_.Configure(config_.Weapon);
            weapons_.SetTargetTracer([this](const glm::vec3& origin, const glm::vec3& direction, float distance)
                { return TraceInfected(origin, direction, distance); });
        }

        void LoadRig(int kind, std::span<const std::byte> data)
        {
            if (InWorld() || (kind != 0 && kind != 1)) throw std::runtime_error("Invalid rig load");
            auto& slot = kind == 0 ? survivorRig_ : infectedRig_;
            slot = std::make_unique<Assets::FRigDefinition>(ReadBakedRig(data));
        }

        std::span<const float> Poses() const { return poseFloats_; }

        void BeginCollision(size_t meshCount, size_t instanceCount)
        {
            if (InWorld()) throw std::runtime_error("Cannot replace collision during play");
            physics_ = std::make_unique<FWebPhysics>(meshCount, instanceCount);
        }

        void CollisionMesh(std::span<const float> positions, std::span<const uint32_t> indices)
        {
            if (!physics_) throw std::runtime_error("No open map collision");
            physics_->AddMesh(positions, indices);
        }

        void CollisionInstances(std::span<const uint32_t> records)
        {
            if (!physics_) throw std::runtime_error("No open map collision");
            physics_->AddInstances(records);
        }

        void Controls(uint32_t flags, float movementX, float movementY)
        {
            if (!InWorld() || !physics_ || !alive_) return;
            if (std::isfinite(movementX) && std::isfinite(movementY))
                player_.OnLook(std::clamp(movementX, -1800.f, 1800.f), std::clamp(movementY, -1800.f, 1800.f));
            for (int dir = 0; dir < 4; ++dir) player_.SetMoveKey(dir, (flags & (1u << dir)) != 0);
            player_.SetWalkModifier((flags & Walk) != 0);
            player_.SetSprintModifier((flags & Sprint) != 0);
            player_.SetFreeLook((flags & FreeLook) != 0);
            player_.SetJumpHeld((flags & JumpHeld) != 0);
            const bool aim = (flags & Aim) != 0;
            if (aim != aimHeld_)
            {
                aimHeld_ = aim;
                player_.SetAimButton(aim, weapons_.HasActiveWeapon() && !weapons_.ActiveIsMelee() && !actions_.IsActive());
            }
            if (flags & (MoveForward | MoveBack | MoveLeft | MoveRight | Jump)) actions_.RequestCancel();
            if (flags & Jump && !actions_.IsActive()) player_.QueueContextualJump(rayQuery_);
            if (flags & Crouch) player_.QueueCrouchToggle();
            if (flags & ToggleView) player_.ToggleView();
            if (flags & Reload && !player_.IsTraversing() && !actions_.IsActive()) weapons_.RequestReload(inventory_);
            for (int slot = 0; slot < kWeaponSlotCount; ++slot)
                if (flags & (SlotOne << slot)) weapons_.SwitchSlot(slot);
            if (flags & PreviousWeapon) weapons_.SwitchPrevious();
            triggerHeld_ = (flags & Trigger) != 0;
            triggerPress_ = triggerPress_ || (flags & TriggerPress) != 0;
        }

        void Command(const Json& command)
        {
            const auto action = command.at("type").get<std::string>();
            if (action == "login" && (session_.lobby == FClientSession::ELobby::Off || session_.lobby == FClientSession::ELobby::Login))
            {
                name_ = command.at("name").get<std::string>();
                if (name_.empty()) throw std::runtime_error("A survivor name is required");
                message_.clear();
                Http("login", session_.BeginLogin(name_, command.value("password", std::string{})));
            }
            else if (action == "http") Reply(command);
            else if (action == "profile" && !session_.token.empty() && !session_.connected) RequestProfile();
            else if (action == "deploy" && session_.lobby == FClientSession::ELobby::Profile)
            {
                map_ = command.value("map", std::string{});
                bring_.clear();
                if (const auto found = command.find("bring"); found != command.end())
                {
                    for (const auto& item : *found)
                        bring_.push_back({item.at("index").get<int>(), item.at("count").get<int>()});
                }
                message_.clear();
                RequestDeploy();
            }
            else if (action == "direct" && !session_.connected)
            {
                name_ = command.at("name").get<std::string>();
                expectedScene_.clear();
                session_.deploy = {};
                session_.lobby = FClientSession::ELobby::Joining;
                effects_.push_back({{"type", "connect"}, {"direct", true}});
            }
            else if (action == "connected" && session_.lobby == FClientSession::ELobby::Joining)
            {
                session_.connected = true;
                Send(session_.MakeHello(name_, expectedScene_));
            }
            else if (action == "closed")
            {
                const auto detail = command.value("reason", std::string{});
                if (message_.empty() && !detail.empty()) message_ = detail;
                CloseConnection();
            }
            else if (action == "leave" && (session_.connected || session_.lobby == FClientSession::ELobby::Joining))
                effects_.push_back({{"type", "disconnect"}});
            else if (action == "mapReady")
            {
                mapReady_ = command.at("scene").get<std::string>() == scene_;
                if (mapReady_)
                {
                    wells_.clear();
                    extractions_.clear();
                    if (const auto sites = command.find("sites"); sites != command.end())
                    {
                        for (const auto& site : *sites)
                        {
                            const auto kind = site.at("type").get<std::string>();
                            if (kind == "well") wells_.push_back(ReadVec(site.at("position")));
                            if (kind == "extraction") extractions_.push_back(ReadVec(site.at("position")));
                        }
                    }
                }
                if (mapReady_ && session_.deploy.ticket.empty() && !spawned_)
                {
                    feet_ = ReadVec(command.at("spawn"));
                    life_ = 1;
                    body_.Reset();
                    spawned_ = true;
                }
                TryEnter();
            }
            else if (action == "reviewPlace" && InWorld() && physics_ && alive_)
            {
                const auto position = ReadVec(command.at("position"));
                const float yaw = command.value("yaw", 0.f);
                if (!std::isfinite(yaw)) throw std::runtime_error("Invalid heading");
                const auto floor = rayQuery_(position, glm::vec3(0.f, -1.f, 0.f), 60.f);
                if (!floor || floor->Normal.y < 0.55f) throw std::runtime_error("No walkable surface at review position");
                player_.Create([this](const FCharacterControllerSettings& settings)
                    { return physics_->CreateCharacter(settings); }, {}, floor->HitPoint + glm::vec3(0.f, .06f, 0.f), config_);
                player_.SetYaw(yaw);
                player_.SetPitch(0.f);
                aimHeld_ = false;
                actions_.Reset();
                actionLoot_.clear();
                triggerHeld_ = triggerPress_ = false;
            }
            else if (action == "reviewCrowd" && InWorld() && physics_)
            {
                reviewCrowd_.clear();
                const int count = std::clamp(command.value("count", 0), 0, 48);
                const auto heading = glm::vec3(std::sin(yaw_), 0.f, std::cos(yaw_));
                const auto side = glm::vec3(heading.z, 0.f, -heading.x);
                for (int i = 0; i < count; ++i)
                {
                    FZombieRuntime zombie;
                    zombie.handle = {10000u + static_cast<uint32_t>(i), 1};
                    zombie.position = feet_ + side * ((i % 8 - 3.5f) * .83f) + heading * (6.2f + (i / 8) * 1.75f);
                    const auto floor = rayQuery_(zombie.position + glm::vec3(0.f, 0.65f, 0.f), {0.f, -1.f, 0.f}, 4.f);
                    if (floor && floor->Normal.y > .55f) zombie.position.y = floor->HitPoint.y;
                    zombie.forward = -heading;
                    zombie.velocity = zombie.forward * (0.5f + static_cast<float>(i % 4) * .38f);
                    zombie.profile = static_cast<EZombieProfile>(i % 3);
                    zombie.state = EZombieState::Wander;
                    reviewCrowd_.push_back(std::move(zombie));
                }
            }
            else if (action == "target" && InWorld() && !physics_) target_ = ReadVec(command.at("position"));
            else if ((action == "equip" || action == "unequip") && InWorld() && pendingTakes_.empty())
            {
                const auto instance = command.at("instance").get<FItemInstanceId>();
                const auto* item = inventory_.FindInstance(instance);
                const auto* definition = item ? FindItemDef(item->defId) : nullptr;
                if (definition && IsWieldable(*definition))
                {
                    const int slot = command.value("slot", PreferredWeaponSlot(*definition));
                    if (action == "equip")
                    {
                        if (!weapons_.EquipInstance(slot, inventory_, instance)) message_ = "Weapon cannot go into that slot";
                    }
                    else
                    {
                        for (int index = 0; index < kWeaponSlotCount; ++index)
                            if (weapons_.SlotInstance(index) == instance) weapons_.Unequip(index);
                        player_.CancelAiming();
                    }
                }
                else
                {
                    const bool ok = action == "equip" ? inventory_.TryEquip(instance) : inventory_.TryUnequip(instance);
                    if (!ok) message_ = "Equipment does not fit";
                }
            }
            else if (action == "interact" && InWorld() && alive_)
            {
                if (const auto* entry = FindNearbyLoot()) BeginTake(entry->id);
                else if (Near(wells_, Eye(), config_.Loot.ReachMeters) && !player_.IsTraversing())
                    actions_.BeginUse(EPlayerAction::DrinkFromWell);
            }
            else if (action == "cancelAction" && InWorld()) actions_.RequestCancel();
            else if (action == "inventory" && InWorld() && alive_ && pendingTakes_.empty())
                EditInventory(command);
            else if (action == "take" && InWorld())
            {
                const auto id = command.at("id").get<std::string>();
                if (!physics_) Take(id);
                else
                {
                    FLootDestination destination;
                    destination.container = command.value("container", FContainerId{0});
                    const auto slot = command.value("slot", 0);
                    if (slot > 0 && slot <= static_cast<int>(EEquipSlot::Sidearm)) destination.slot = static_cast<EEquipSlot>(slot);
                    if (command.contains("x") && command.contains("y"))
                        destination.placement = FGridPlacement{command.at("x").get<int>(), command.at("y").get<int>(),
                                                               command.value("rotated", false)};
                    BeginTake(id, destination);
                }
            }
            else if (action == "drop" && InWorld() && pendingTakes_.empty()) Drop(command.at("instance").get<FItemInstanceId>());
            else if (action == "use" && InWorld() && pendingTakes_.empty())
            {
                const auto item = command.at("instance").get<FItemInstanceId>();
                if (!physics_) UseItem(item);
                else if (const auto* instance = inventory_.FindInstance(item))
                {
                    const auto* definition = FindItemDef(instance->defId);
                    if (definition && !player_.IsTraversing())
                        actions_.BeginUse(UseAction(definition->use), item);
                }
            }
            else if (action == "extract" && InWorld())
            {
                FNetWriter writer;
                const bool begin = command.value("begin", true);
                if (physics_ && begin && (!alive_ || !Near(extractions_, feet_, 12.f))) return;
                WriteExtract(writer, begin);
                Push(writer, true);
            }
        }

        void Receive(std::span<const std::byte> bytes)
        {
            auto packet = FClientSession::Decode(bytes);
            std::visit([this](auto& message) { Apply(message); }, packet);
        }

        void Advance(float dt)
        {
            dt = std::clamp(std::isfinite(dt) ? dt : 0.f, 0.f, 0.15f);
            if (session_.lobby == FClientSession::ELobby::Deploying)
            {
                retrySeconds_ -= dt;
                if (retrySeconds_ <= 0.f) RequestDeploy();
            }
            if (!InWorld()) return;

            time_.Advance(dt);
            session_.StepRemotes(dt);
            if (extract_.secondsLeft > 0.f) extract_.secondsLeft = std::max(.01f, extract_.secondsLeft - dt);
            zombieStep_ += dt;
            hitMarker_ = std::max(0.f, hitMarker_ - dt);
            hurtMarker_ = std::max(0.f, hurtMarker_ - dt);

            glm::vec3 velocity{};
            if (physics_)
            {
                constexpr float stepSeconds = 1.f / 60.f;
                AdvanceActions(dt);
                player_.SetMovementLocked(actions_.IsActive());
                physicsTime_ += dt;
                while (physicsTime_ >= stepSeconds)
                {
                    if (alive_)
                    {
                        const bool attacking = !actions_.IsActive() && !player_.IsTraversing() &&
                            (triggerHeld_ || triggerPress_);
                        weapons_.SetTriggerDown(attacking);
                        player_.SetMeleeEquipped(weapons_.ActiveIsMelee());
                        player_.SetSprintBlocked((attacking && weapons_.HasActiveWeapon()) || weapons_.IsSwinging() ||
                                                 weapons_.MeleeInputPending() || body_.IsWeak());
                        player_.Update(stepSeconds, rayQuery_);
                        weapons_.Update(stepSeconds, player_, inventory_, rayQuery_);
                        weapons_.CopyMagazinesTo(inventory_);
                        if (weapons_.IsReloading() || weapons_.IsSwitching()) player_.CancelAiming();
                        ConsumeWeaponEvents(stepSeconds);
                        triggerPress_ = false;
                    }
                    physicsTime_ -= stepSeconds;
                }
                feet_ = player_.Position();
                yaw_ = player_.Yaw();
            }
            else if (target_)
            {
                const auto offset = *target_ - feet_;
                const float distance = glm::length(offset);
                const float step = config_.Player.StandWalkSpeed * dt;
                if (distance <= step)
                {
                    feet_ = *target_;
                    target_.reset();
                }
                else if (distance > 0.f)
                {
                    velocity = offset / distance * config_.Player.StandWalkSpeed;
                    feet_ += velocity * dt;
                    if (glm::length(glm::vec2(velocity.x, velocity.z)) > 1e-4f)
                        yaw_ = std::atan2(velocity.x, velocity.z);
                }
            }

            if (session_.ShouldSendPresence(dt))
            {
                const auto forward = Forward();
                FPlayerState report;
                report.presence = {session_.survivorId, feet_, Eye(), forward, alive_};
                report.presentation = LocalPresentation();
                if (!physics_)
                {
                    report.presentation.locomotion.onGround = true;
                    report.presentation.locomotion.worldVelocity = velocity;
                    report.presentation.locomotion.horizontalSpeed = glm::length(glm::vec2(velocity.x, velocity.z));
                    report.presentation.locomotion.gait = target_ ? EPlayerGait::Walk : EPlayerGait::Idle;
                }
                report.clothing = inventory_.EquippedClothing();
                Send(report, false);
            }

            AdvancePoses(dt);

            if (session_.ShouldSyncInventory(dt, alive_ && pendingTakes_.empty()))
            {
                FNetWriter writer;
                if (session_.WriteInventoryIfChanged(writer, life_, inventory_.Capture())) Push(writer, true);
            }
        }

        std::string Frame() const
        {
            Json remotes = Json::array();
            for (const auto& [id, remote] : session_.remotes)
            {
                const auto animated = remotePoses_.find(id);
                remotes.push_back({{"id", id}, {"position", V3(remote.feet)}, {"alive", remote.alive},
                    {"yaw", animated == remotePoses_.end() ? remote.presentation.aimYawRadians : animated->second.pose.yaw},
                    {"matrixOffset", animated == remotePoses_.end() ? -1 : static_cast<int>(animated->second.pose.offset)},
                    {"pose", DescribePose(remote.presentation, remote.clothing)}});
            }
            Json camera, pose;
            if (physics_ && InWorld())
            {
                camera = {{"position", V3(player_.CameraPosition())}, {"forward", V3(player_.CameraForward())},
                          {"up", V3(player_.CameraUp())}, {"fov", player_.CurrentFov()},
                          {"firstPerson", player_.IsFirstPerson()}, {"arm", player_.CameraArmLength()}};
                pose = DescribePose(LocalPresentation(), inventory_.EquippedClothing());
                pose["probe"] = player_.TraversalProbeResult();
                pose["viewModelOffset"] = viewModelOffset_;
                pose["viewModelArms"] = viewModelArms_;
                pose["bodyYaw"] = localPose_.pose.yaw;
                pose["matrixOffset"] = localPose_.lastFrame ? static_cast<int>(localPose_.pose.offset) : -1;
            }
            Json zombies = Json::array();
            for (const auto& zombie : VisibleInfected())
            {
                const auto p = zombie.position + zombie.velocity * std::min(zombieStep_, 0.10f);
                const auto animated = infectedPoses_.find(zombie.handle.index);
                zombies.push_back({{"id", zombie.handle.index}, {"position", V3(p)},
                    {"yaw", std::atan2(zombie.forward.x, zombie.forward.z)}, {"profile", static_cast<int>(zombie.profile)},
                    {"state", static_cast<int>(zombie.state)}, {"speed", glm::length(glm::vec2(zombie.velocity.x, zombie.velocity.z))},
                    {"matrixOffset", animated == infectedPoses_.end() ? -1 : static_cast<int>(animated->second.pose.offset)}});
            }
            const auto body = body_.Snapshot();
            const auto weapon = weapons_.ActiveWeapon();
            return Json{
                {"phase", Phase()}, {"message", message_}, {"scene", scene_}, {"id", session_.survivorId},
                {"feet", V3(feet_)}, {"eye", V3(Eye())}, {"forward", V3(Forward())}, {"yaw", yaw_},
                {"combat", {{"shots", weapons_.LastShotSequence()}, {"hits", hits_}, {"kills", kills_},
                    {"hitMarker", hitMarker_}, {"hurt", hurtMarker_}, {"headshot", headshot_}, {"kill", killMarker_},
                    {"weapon", weapon ? weapon->displayName : std::string_view{}}, {"magazine", weapons_.AmmoInMag()},
                    {"reserve", weapons_.AmmoReserve(inventory_)}, {"capacity", weapons_.MagazineCapacity()},
                    {"action", static_cast<int>(weapons_.PresentationAction())}, {"progress", weapons_.PresentationActionTime()}}},
                {"snapshots", snapshots_}, {"remotes", remotes}, {"zombies", zombies}, {"camera", camera}, {"pose", pose}, {"lootCount", loot_.size()}, {"lootReady", lootReady_},
                {"moving", target_.has_value()}, {"alive", alive_}, {"pvp", session_.pvp}, {"fixes", fixes_},
                {"takes", takes_}, {"denials", denials_}, {"drops", drops_}, {"lastFix", lastFix_}, {"extracted", extracted_},
                {"extractSeconds", extract_.secondsLeft}, {"extractBroken", extract_.broken},
                {"nearExtraction", Near(extractions_, feet_, 12.f)}, {"nearWell", Near(wells_, Eye(), config_.Loot.ReachMeters)},
                {"body", {{"health", body.health}, {"hunger", body.hunger}, {"hydration", body.hydration},
                    {"bleeding", body.bleedingWounds}, {"cold", body.cold}, {"healing", body.healing}, {"cause", body.lastDamageSource}}},
                {"clock", DescribeClock(time_)}
            }.dump();
        }

        std::string Profile() const
        {
            const auto& profile = session_.profile;
            Json maps = Json::array();
            for (const auto& map : profile.maps)
                maps.push_back({{"id", map.id}, {"name", map.name}, {"scene", map.scene}, {"training", map.training}});
            Json character;
            if (profile.character)
                character = {{"map", profile.character->map}, {"storage", DescribeStacks(profile.character->inventory)}};
            return Json{{"name", profile.name}, {"gold", profile.gold}, {"storage", DescribeStacks(profile.storage)},
                        {"maps", maps}, {"character", character}, {"sorties", profile.sorties},
                        {"extractions", profile.extractions}, {"deaths", profile.deaths},
                        {"playing", profile.playing}, {"lastOutcome", profile.lastOutcome}}.dump();
        }

        std::string Loot() const
        {
            Json loot = Json::array();
            for (const auto& [id, entry] : loot_)
                if (entry.available)
                    loot.push_back({{"id", id}, {"position", V3(entry.position)}, {"contents", DescribeStacks(entry.contents)}});
            return loot.dump();
        }

        std::string NearbyLoot() const
        {
            const auto* closest = FindNearbyLoot();
            return closest ? Json{{"id", closest->id}, {"position", V3(closest->position)},
                {"contents", DescribeStacks(closest->contents)}}.dump() : "null";
        }

        std::string GroundItems() const
        {
            Json items = Json::array();
            if (InWorld())
            {
                for (const auto& [id, entry] : loot_)
                    if (entry.available && !entry.contents.empty() && glm::distance(entry.position, feet_) < 55.f)
                        items.push_back({{"id", id}, {"item", entry.contents.front().itemId},
                                         {"position", V3(entry.position)}, {"yaw", entry.yaw}});
            }
            return items.dump();
        }

        Json InventoryRows() const
        {
            Json inventory = Json::array();
            for (const auto& item : inventory_.Instances())
            {
                auto row = Describe({item.defId, item.count, item.loadedAmmo, item.attachments});
                row["instance"] = item.instanceId;
                int slot = -1;
                for (int index = 0; index < kWeaponSlotCount; ++index)
                    if (weapons_.SlotInstance(index) == item.instanceId) slot = index;
                row["worn"] = item.containerId == kWornContainer || slot >= 0;
                row["weaponSlot"] = slot;
                row["container"] = item.containerId;
                row["x"] = item.placement.x;
                row["y"] = item.placement.y;
                row["rotated"] = item.placement.rotated;
                const auto dimensions = Inventory::InstanceSize(item);
                row["width"] = dimensions.width;
                row["height"] = dimensions.height;
                row["attachments"] = item.attachments;
                const auto* definition = FindItemDef(item.defId);
                row["wearable"] = definition && definition->equipSlot != EEquipSlot::None;
                row["equipSlot"] = definition ? static_cast<int>(definition->equipSlot) : 0;
                row["attachable"] = definition && FindAttachmentDef(definition->id) != nullptr;
                row["wieldable"] = definition && IsWieldable(*definition);
                if (row["wieldable"].get<bool>()) row["preferredSlot"] = PreferredWeaponSlot(*definition);
                row["usable"] = definition && UseAction(definition->use) != EPlayerAction::None;
                inventory.push_back(std::move(row));
            }
            return inventory;
        }

        std::string Cargo() const
        {
            Json containers = Json::array();
            for (const auto& box : inventory_.Containers())
                if (box.active) containers.push_back({{"id", box.containerId}, {"owner", box.ownerItemInstanceId},
                    {"name", box.displayName}, {"width", box.size.width}, {"height", box.size.height},
                    {"weaponOnly", box.weaponOnly}, {"used", inventory_.ContainerUsed(box.containerId)}});
            return Json{{"items", InventoryRows()}, {"containers", containers}, {"activeSlot", weapons_.ActiveSlot()}}.dump();
        }

        std::string DrainEffects()
        {
            auto out = std::move(effects_);
            effects_ = Json::array();
            return out.dump();
        }

        bool PopPacket()
        {
            if (packets_.empty()) return false;
            outgoing = std::move(packets_.front());
            packets_.pop_front();
            return true;
        }

        std::string result;
        FPacket outgoing;

    private:
        bool InWorld() const { return session_.lobby == FClientSession::ELobby::Playing && session_.connected; }
        glm::vec3 Eye() const
        {
            return physics_ && InWorld() ? player_.EyePosition() : feet_ + glm::vec3(0.f, config_.Player.StandingEyeHeight, 0.f);
        }

        glm::vec3 Forward() const
        {
            return physics_ && InWorld() ? player_.Forward() : glm::vec3(std::sin(yaw_), 0.f, std::cos(yaw_));
        }

        FPlayerPresentationState LocalPresentation() const
        {
            FPlayerPresentationState state;
            state.aimYawRadians = yaw_;
            state.aimDirection = Forward();
            if (!physics_) return state;
            state.locomotion = player_.LocomotionState();
            state.action = actions_.Action();
            state.actionTime01 = actions_.IsActive() ? actions_.NormalizedTime() : 0.f;
            state.aimPitchRadians = player_.Pitch();
            state.aimWeight = player_.IsAiming() ? 1.f : 0.f;
            state.sprintAlpha = player_.SprintAlpha();
            state.weaponId = weapons_.ActiveWeaponId();
            state.hasWeapon = weapons_.HasActiveWeapon();
            state.weaponAction = weapons_.PresentationAction();
            state.weaponActionTime01 = weapons_.PresentationActionTime();
            state.activeWeaponSlot = weapons_.ActiveSlot();
            state.switchSourceSlot = weapons_.SwitchSourceSlot();
            state.switchTargetSlot = weapons_.SwitchTargetSlot();
            for (int slot = 0; slot < kWeaponSlotCount; ++slot)
            {
                state.slotWeaponIds[slot] = weapons_.SlotWeaponId(slot);
                state.slotAttachments[slot] = weapons_.SlotAttachments(slot);
            }
            return state;
        }

        void RestoreWeapons()
        {
            weapons_.ResetRuntime();
            int first = -1;
            for (const auto& item : inventory_.Instances())
            {
                const auto* info = FindItemDef(item.defId);
                if (!info || !IsWieldable(*info)) continue;
                const auto slot = PreferredWeaponSlot(*info);
                if (!weapons_.SlotInstance(slot) && weapons_.EquipInstance(slot, inventory_, item.instanceId) && first < 0)
                    first = slot;
            }
            weapons_.RestoreSelection(first, -1);
        }

        std::optional<FHitboxHit> TraceInfected(const glm::vec3& origin, const glm::vec3& direction, float distance) const
        {
            std::optional<FHitboxHit> nearest;
            for (const auto& zombie : zombies_)
            {
                if (zombie.state == EZombieState::Dead || !zombie.handle.IsValid()) continue;
                const auto it = infectedPoses_.find(zombie.handle.index);
                if (it == infectedPoses_.end() || !infectedRig_) continue;
                const glm::vec3 position = zombie.position + zombie.velocity * std::min(zombieStep_, .1f);
                const float along = glm::dot(position + glm::vec3(0.f, .9f, 0.f) - origin, direction);
                if (along < -2.f || along > distance + 2.f) continue;
                if (glm::length(position + glm::vec3(0.f, .9f, 0.f) - (origin + direction * along)) > 2.f) continue;
                const auto world = glm::translate(glm::mat4(1.f), position) *
                    glm::mat4_cast(glm::angleAxis(std::atan2(zombie.forward.x, zombie.forward.z), glm::vec3(0.f, 1.f, 0.f)));
                std::array<FHitCapsule, 11> hitboxes;
                const auto count = BuildInfectedHitboxes(*infectedRig_, it->second.graph.Transforms(), world, hitboxes);
                for (const auto& capsule : std::span(hitboxes).first(count))
                {
                    const auto hit = RayCapsuleDistance(origin, direction, capsule);
                    if (!hit || *hit > distance) continue;
                    distance = *hit;
                    nearest = FHitboxHit{zombie.handle, capsule.zone, *hit, origin + direction * *hit, -direction};
                }
            }
            return nearest;
        }

        void CheckPvp(const FWeaponTraceEvent& trace)
        {
            const auto* definition = weapons_.ActiveWeapon();
            if (!session_.pvp || !definition) return;
            float distance = glm::distance(trace.origin, trace.endPoint);
            uint32_t victim = 0;
            for (const auto& [id, survivor] : session_.remotes)
            {
                if (!survivor.alive) continue;
                constexpr float radius = .32f;
                const auto hit = RayCapsuleDistance(trace.origin, trace.direction,
                    {survivor.feet + glm::vec3(0.f, radius, 0.f),
                     survivor.feet + glm::vec3(0.f, 1.75f - radius, 0.f), radius});
                if (!hit || *hit > distance) continue;
                victim = id;
                distance = *hit;
            }
            if (!victim) return;
            const auto point = trace.origin + trace.direction * distance;
            const float height = point.y - session_.remotes.at(victim).feet.y;
            const float zone = height > 1.45f ? 2.f : height < .9f ? .6f : 1.f;
            Send(FPvpHit{victim, definition->baseDamage * zone * .6f, trace.origin, point});
        }

        void Present(std::string_view kind, const glm::vec3& position, Json data = Json::object())
        {
            data["type"] = "presentation";
            data["event"] = kind;
            data["position"] = V3(position);
            effects_.push_back(std::move(data));
        }

        void ConsumeWeaponEvents(float dt)
        {
            if (weapons_.ConsumeDryFire()) Present("dry_fire", player_.EyePosition());
            const auto action = weapons_.PresentationAction();
            if (action != lastWeaponAction_)
            {
                if (action == EWeaponPresentationAction::Reload) Present("reload", player_.EyePosition());
                if (action == EWeaponPresentationAction::Switch) Present("switch", player_.EyePosition());
                lastWeaponAction_ = action;
            }
            for (const auto& hit : weapons_.ConsumeHitEvents())
                if (hit.zombie.IsValid()) Send(hit);
            for (const auto& trace : weapons_.ConsumeTraceEvents())
            {
                CheckPvp(trace);
                if (!trace.melee && trace.pellet % 3 == 0)
                    Present("trace", trace.origin, {{"end", V3(trace.endPoint)}, {"impact", trace.hit && !trace.hitZombie}});
            }
            for (const auto& shot : weapons_.ConsumeShotEvents())
            {
                const auto* definition = FindWeaponDef(shot.weaponId);
                if (!definition) continue;
                const auto family = definition->family;
                const ENoiseType kind = shot.melee ? ENoiseType::Melee : family == EWeaponFamily::Shotgun
                    ? ENoiseType::Shotgun : family == EWeaponFamily::Sidearm || family == EWeaponFamily::Smg
                    ? ENoiseType::Pistol : ENoiseType::Rifle;
                Send(FNoise{player_.Position(), definition->noiseRadius * (shot.suppressed ? .25f : 1.f),
                    shot.melee ? .5f : 1.f, kind}, false);
                const auto up = glm::normalize(glm::cross(player_.Right(), player_.Forward()));
                const auto muzzle = player_.EyePosition() + player_.Forward() * .58f + player_.Right() * .13f - up * .09f;
                Present(shot.melee ? "melee" : "shot", muzzle,
                        {{"weapon", shot.weaponId}, {"suppressed", shot.suppressed}});
                if (!shot.melee)
                {
                    player_.ApplyCameraRecoil(shot.cameraImpulseRadians);
                    viewKick_ += shot.viewModelImpulse * .6f;
                }
            }
            viewKick_ *= std::exp(-dt * 14.f);
        }

        const std::vector<FZombieRuntime>& VisibleInfected() const
        {
            return reviewCrowd_.empty() ? zombies_ : reviewCrowd_;
        }

        void AdvancePoses(float dt)
        {
            if (!survivorRig_ || !infectedRig_ || !physics_) return;
            ++poseFrame_;
            poseFloats_.clear();
            if (!localPose_.lastFrame) localPose_.graph.Bind(*survivorRig_);
            const auto local = LocalPresentation();
            localPose_.pose = localPose_.graph.Advance(local, config_.Animation, dt, poseFloats_);
            viewModelOffset_ = -1;
            viewModelArms_ = false;
            if (alive_ && player_.IsFirstPerson() && !player_.IsTraversing() && local.hasWeapon &&
                local.action != EPlayerAction::LootGround)
                WriteViewModel(local);
            localPose_.lastFrame = poseFrame_;
            for (const auto& [id, remote] : session_.remotes)
            {
                auto [it, added] = remotePoses_.try_emplace(id);
                auto& entry = it->second;
                if (added) entry.graph.Bind(*survivorRig_);
                entry.pose = entry.graph.Advance(remote.presentation, config_.Animation, dt, poseFloats_);
                entry.lastFrame = poseFrame_;
            }
            for (const auto& zombie : VisibleInfected())
            {
                auto [it, added] = infectedPoses_.try_emplace(zombie.handle.index);
                auto& entry = it->second;
                if (added) entry.graph.Bind(*infectedRig_);
                entry.pose = entry.graph.Advance(zombie, config_.Zombie, dt, poseFloats_);
                entry.lastFrame = poseFrame_;
            }
            std::erase_if(remotePoses_, [this](const auto& pair) { return pair.second.lastFrame != poseFrame_; });
            std::erase_if(infectedPoses_, [this](const auto& pair) { return pair.second.lastFrame != poseFrame_; });
        }

        void WriteViewModel(const FPlayerPresentationState& state)
        {
            const auto* weapon = FindWeaponDef(state.weaponId);
            if (!weapon) return;
            const float scale = config_.Weapon.ViewModelScale;
            const float aim = player_.AimBlend();
            glm::vec3 offset = config_.Weapon.ViewModelHipOffset;
            if (!weapon->IsMelee())
            {
                const auto points = WeaponSightPoints(state.weaponId, state.slotAttachments[state.activeWeaponSlot]);
                offset = glm::mix(offset, glm::vec3(0.f, -points.sightHeight * scale,
                    points.adsEyeRelief - points.rearSightForward * scale), aim);
            }
            offset += state.sprintAlpha * glm::vec3(.04f, -.10f, -.015f);
            offset.z -= viewKick_;
            if (state.weaponAction != EWeaponPresentationAction::None)
                offset.y -= .19f * std::sin(glm::pi<float>() * state.weaponActionTime01);
            const glm::mat4 weaponInEye = glm::scale(glm::translate(glm::mat4(1.f), glm::vec3(-offset.x, offset.y, offset.z)),
                                                   glm::vec3(scale));
            viewModelOffset_ = static_cast<int>(poseFloats_.size());
            const auto append = [this](const glm::mat4& matrix)
            {
                const auto* values = glm::value_ptr(matrix);
                poseFloats_.insert(poseFloats_.end(), values, values + 16);
            };
            append(weaponInEye);
            const auto& rig = *survivorRig_;
            const FFirstPersonArmBones armBones{
                rig.FindBone("bone_upperarm_l"), rig.FindBone("bone_forearm_l"), rig.FindBone("bone_hand_l"),
                rig.FindBone("bone_upperarm_r"), rig.FindBone("bone_forearm_r"), rig.FindBone("bone_hand_r")
            };
            if (armBones.foreArmL < 0 || armBones.foreArmR < 0 || weapon->IsMelee()) return;
            viewModelArms_ = true;
            armPose_.Solve(rig, armBones, state.weaponId, weaponInEye, config_.Weapon.TpsWeaponScale, scale, true);
            append(armPose_.hands[0]);
            append(armPose_.hands[1]);
            for (const auto bone : {armBones.foreArmR, armBones.foreArmL})
            {
                const glm::mat4 transform = armPose_.Forearm(bone);
                const glm::vec3 slim = FFirstPersonArmPose::ForearmScale(transform);
                glm::mat4 part = transform;
                for (int axis = 0; axis < 3; ++axis)
                    part[axis] = glm::vec4(glm::normalize(glm::vec3(transform[axis])) * slim[axis], 0.f);
                append(part);
            }
        }

        const char* Phase() const
        {
            using P = FClientSession::ELobby;
            switch (session_.lobby)
            {
            case P::Waiting: return "waiting";
            case P::Profile: return "profile";
            case P::Deploying: return "deploying";
            case P::Joining: return "joining";
            case P::Playing: return "playing";
            default: return "login";
            }
        }

        void Http(std::string_view request, std::string body)
        {
            effects_.push_back({{"type", "http"}, {"request", request}, {"body", std::move(body)},
                                {"token", request == "login" ? std::string{} : session_.token}});
        }

        void RequestProfile()
        {
            session_.lobby = FClientSession::ELobby::Waiting;
            Http("profile", "{}");
        }

        void RequestDeploy() { Http("deploy", session_.BeginDeploy(map_, bring_)); }

        void Reply(const Json& command)
        {
            const auto request = command.at("request").get<std::string>();
            const auto body = command.at("body").get<std::string>();
            const auto status = command.at("status").get<int>();
            const bool ok = status >= 200 && status < 300;

            if (request == "login" && ok && session_.AcceptLogin(body)) RequestProfile();
            else if (request == "profile" && ok && session_.AcceptProfile(body)) {}
            else if (request == "deploy" && ok && session_.AcceptDeploy(body))
            {
                if (session_.lobby == FClientSession::ELobby::Deploying) retrySeconds_ = 1.f;
                else
                {
                    expectedScene_ = session_.deploy.scene;
                    effects_.push_back({{"type", "connect"}, {"scene", expectedScene_}, {"server", session_.deploy.server},
                                        {"ticket", session_.deploy.ticket}, {"token", session_.token}});
                }
            }
            else
            {
                message_ = command.value("error", std::string{});
                if (message_.empty()) message_ = "The Master could not complete the request";
                session_.lobby = session_.token.empty() || status == 401
                    ? FClientSession::ELobby::Login : FClientSession::ELobby::Profile;
            }
        }

        void CloseConnection()
        {
            session_.ResetConnection();
            session_.deploy = {};
            session_.rejected.clear();
            scene_.clear();
            expectedScene_.clear();
            mapReady_ = spawned_ = lootReady_ = alive_ = false;
            session_.remotes.clear();
            zombies_.clear();
            reviewCrowd_.clear();
            localPose_ = {};
            remotePoses_.clear();
            infectedPoses_.clear();
            poseFloats_.clear();
            poseFrame_ = 0;
            player_.Destroy();
            actions_.Reset();
            actionLoot_.clear();
            physics_.reset();
            physicsTime_ = 0.f;
            aimHeld_ = false;
            triggerHeld_ = triggerPress_ = false;
            weapons_.ResetRuntime();
            hits_ = kills_ = 0;
            viewKick_ = hitMarker_ = hurtMarker_ = 0.f;
            lastWeaponAction_ = EWeaponPresentationAction::None;
            loot_.clear();
            pendingTakes_.clear();
            packets_.clear();
            target_.reset();
            extract_ = {};
            inventory_.Clear();
            snapshots_ = 0;
            if (session_.token.empty()) session_.lobby = FClientSession::ELobby::Login;
            else RequestProfile();
        }

        void TryEnter()
        {
            if (!mapReady_ || !spawned_ || session_.survivorId == 0 || InWorld()) return;
            if (physics_)
            {
                player_.SetFirstPerson(false);
                player_.Create([this](const FCharacterControllerSettings& settings)
                    { return physics_->CreateCharacter(settings); }, {}, feet_, config_);
                player_.SetYaw(yaw_);
                player_.SetPitch(0.f);
                player_.Update(1.f / 60.f, rayQuery_);
            }
            session_.lobby = FClientSession::ELobby::Playing;
            alive_ = body_.IsAlive();
            FNetWriter writer;
            WriteLootSync(writer);
            Push(writer, true);
        }

        template <class Message>
        void Send(const Message& message, bool reliable = true)
        {
            FNetWriter writer;
            Write(writer, message);
            Push(writer, reliable);
        }

        void Push(const FNetWriter& writer, bool reliable)
        {
            const auto bytes = writer.Bytes();
            packets_.push_back({reliable, {bytes.begin(), bytes.end()}});
        }

        static bool Near(const std::vector<glm::vec3>& sites, const glm::vec3& point, float reach)
        {
            return std::ranges::any_of(sites, [&](const auto& site) { return glm::distance(site, point) <= reach; });
        }

        const FLootReplica* FindNearbyLoot() const
        {
            const FLootReplica* closest = nullptr;
            float distance = config_.Loot.ReachMeters;
            for (const auto& [id, entry] : loot_)
            {
                if (!entry.available) continue;
                const auto toward = entry.position - Eye();
                const float reach = glm::length(toward);
                if (reach >= distance) continue;
                if (physics_ && reach > .1f)
                {
                    const auto obstacle = rayQuery_(Eye(), toward / reach, reach - .1f);
                    if (obstacle) continue;
                }
                closest = &entry;
                distance = reach;
            }
            return closest;
        }

        void BeginTake(const std::string& id, const FLootDestination& destination = {})
        {
            const auto entry = loot_.find(id);
            if (player_.IsTraversing() || !pendingTakes_.empty() || entry == loot_.end() || !entry->second.available ||
                glm::distance(entry->second.position, Eye()) > config_.Loot.ReachMeters) return;
            if (actions_.BeginLoot({0, 0, 1}))
            {
                player_.CancelAiming();
                actionLoot_ = id;
                actionDestination_ = destination;
            }
        }

        void EditInventory(const Json& command)
        {
            const auto edit = command.at("edit").get<std::string>();
            const auto item = command.at("instance").get<FItemInstanceId>();
            bool accepted = false;
            if (edit == "place")
            {
                const auto container = command.at("container").get<FContainerId>();
                const auto x = command.at("x").get<int>();
                const auto y = command.at("y").get<int>();
                if (x >= 0 && x <= 128 && y >= 0 && y <= 128)
                    accepted = inventory_.TryPlace(item, container, {x, y, command.value("rotated", false)});
            }
            else if (edit == "merge") accepted = inventory_.TryMerge(item, command.at("target").get<FItemInstanceId>());
            else if (edit == "split")
            {
                FItemInstanceId split{};
                accepted = inventory_.TrySplit(item, command.at("count").get<int>(), split);
            }
            else if (edit == "attach") accepted = inventory_.TryAttach(command.at("target").get<FItemInstanceId>(), item);
            else if (edit == "detach")
            {
                const auto slot = command.at("slot").get<int>();
                if (slot >= 0 && slot < static_cast<int>(EAttachmentSlot::Count))
                    accepted = inventory_.TryDetach(item, static_cast<EAttachmentSlot>(slot));
            }
            message_ = accepted ? "" : "Item does not fit";
            if (accepted) weapons_.RefreshInventory(inventory_);
        }

        static EPlayerAction UseAction(EItemUse use)
        {
            switch (use)
            {
            case EItemUse::Eat: return EPlayerAction::Eat;
            case EItemUse::Drink: return EPlayerAction::Drink;
            case EItemUse::Bandage:
            case EItemUse::FirstAid:
            case EItemUse::Painkillers: return EPlayerAction::Heal;
            case EItemUse::Rest: return EPlayerAction::Rest;
            default: return EPlayerAction::None;
            }
        }

        void UseItem(FItemInstanceId item)
        {
            const auto* instance = inventory_.FindInstance(item);
            const auto* definition = instance ? FindItemDef(instance->defId) : nullptr;
            const auto action = definition ? UseAction(definition->use) : EPlayerAction::None;
            if (!body_.TryUseItem(inventory_, item)) return;
            Send(FSurvivalAction{ESurvivalAction::UseItem, item});
            if (physics_) Present(action == EPlayerAction::Drink ? "drink" : action == EPlayerAction::Eat ? "eat" : "bandage", Eye());
        }

        void AdvanceActions(float dt)
        {
            if (!alive_)
            {
                actions_.Reset();
                actionLoot_.clear();
                return;
            }
            actions_.Update(dt);
            if (actions_.ConsumeCancelRequest()) actionLoot_.clear();
            if (actions_.ConsumeCommitRequest())
            {
                Take(actionLoot_, actionDestination_);
                actionLoot_.clear();
            }
            if (const auto commit = actions_.ConsumeItemCommitRequest())
            {
                if (commit->action == EPlayerAction::DrinkFromWell && Near(wells_, Eye(), config_.Loot.ReachMeters))
                {
                    Send(FSurvivalAction{ESurvivalAction::DrinkFromWell});
                    Present("drink", Eye());
                }
                else if (commit->action == EPlayerAction::Rest) Send(FSurvivalAction{ESurvivalAction::Rest});
                else UseItem(commit->instanceId);
            }
        }

        void Take(const std::string& id, const FLootDestination& destination = {})
        {
            if (!pendingTakes_.empty()) return;
            const auto entry = loot_.find(id);
            if (entry == loot_.end() || !entry->second.available) return;
            const auto request = nextTake_++;
            pendingTakes_.emplace(request, FPendingLoot{entry->second, destination});
            Send(FLootTake{request, id});
        }

        void Drop(FItemInstanceId instance)
        {
            auto dropped = inventory_.TryRemove(instance);
            if (!dropped) return;
            Send(FLootDrop{feet_, yaw_, {{dropped->defId, dropped->count, dropped->loadedAmmo, dropped->attachments}}});
            ++drops_;
            if (physics_) Present("drop", feet_);
        }

        template<class Message> void Apply(Message&) {}

        void Apply(FWelcome& welcome)
        {
            if (!session_.ApplyWelcome(welcome) || (!expectedScene_.empty() && expectedScene_ != welcome.scene))
            {
                message_ = "The DS returned a different map or protocol";
                effects_.push_back({{"type", "disconnect"}});
                return;
            }
            scene_ = welcome.scene;
            effects_.push_back({{"type", "map"}, {"scene", scene_}});
        }

        void Apply(FCharacterSpawn& spawn)
        {
            if (!inventory_.Restore(spawn.inventory)) return;
            RestoreWeapons();
            body_.Restore(spawn.body);
            life_ = spawn.life;
            feet_ = spawn.feet;
            yaw_ = spawn.yaw;
            spawned_ = true;
            session_.lastInventorySync.clear();
            TryEnter();
        }

        void Apply(FSnapshot& snapshot)
        {
            ++snapshots_;
            if (physics_)
            {
                zombies_ = std::move(snapshot.zombies);
                zombieStep_ = 0.f;
            }
            FClientSession::ReconcileClock(time_, snapshot.time, snapshots_ == 1);
            session_.SynchronizeRemotes(snapshot.survivors);
        }

        void Apply(FLootState& state)
        {
            if (state.full)
            {
                loot_.clear();
                lootReady_ = true;
            }
            if (lootReady_)
                for (auto& entry : state.entries)
                {
                    auto id = entry.id;
                    loot_.insert_or_assign(std::move(id), std::move(entry));
                }
        }

        void Apply(FLootGrant& grant)
        {
            const auto pending = pendingTakes_.find(grant.request);
            if (pending == pendingTakes_.end()) return;
            if (grant.granted)
            {
                const auto& [spot, destination] = pending->second;
                if (auto found = loot_.find(spot.id); found != loot_.end()) found->second.available = false;
                std::vector<FLootStack> left;
                if (!AddLootToInventory(grant.contents, inventory_, destination.container, destination.slot,
                                        destination.placement, left).empty())
                {
                    ++takes_;
                    if (physics_)
                    {
                        Present("pickup", Eye());
                        for (const auto& item : inventory_.Instances())
                        {
                            const auto* def = FindItemDef(item.defId);
                            if (def && IsWieldable(*def) && !weapons_.SlotInstance(PreferredWeaponSlot(*def)))
                                weapons_.EquipInstance(PreferredWeaponSlot(*def), inventory_, item.instanceId);
                        }
                    }
                }
                if (!left.empty())
                {
                    Send(FLootDrop{spot.position, spot.yaw, std::move(left)});
                    message_ = "Item does not fit";
                }
            }
            else ++denials_;
            pendingTakes_.erase(pending);
        }

        void Apply(FInventoryFix& fix)
        {
            if (fix.life != life_) return;
            ++fixes_;
            lastFix_ = fix.reason;
            if (inventory_.Restore(fix.inventory)) RestoreWeapons();
        }

        void Apply(FSurvivalState& state)
        {
            if (state.life != life_) return;
            body_.Restore(state.body);
            alive_ = body_.IsAlive();
        }

        void Apply(FNoise& noise)
        {
            if (noise.type == ENoiseType::Melee) Present("melee", noise.position);
            else if (noise.type == ENoiseType::Rifle || noise.type == ENoiseType::Shotgun || noise.type == ENoiseType::Pistol)
                Present("remote_shot", noise.position,
                    {{"weapon", noise.type == ENoiseType::Shotgun ? "shotgun" : noise.type == ENoiseType::Pistol ? "pistol" : "ak"}});
        }

        void Apply(FDamageNotice& notice)
        {
            Present("blood", notice.damage.worldPoint, {{"killed", notice.damage.killed}});
            if (notice.shooterId != session_.survivorId) return;
            ++hits_;
            if (notice.damage.killed) ++kills_;
            hitMarker_ = .26f;
            killMarker_ = notice.damage.killed;
            headshot_ = notice.damage.hitZone == EHitZone::Head;
            Present(killMarker_ ? "kill" : headshot_ ? "headshot" : "hit", Eye());
        }

        void Apply(FSurvivorStrike& strike)
        {
            hurtMarker_ = .55f;
            Present("hurt", strike.source);
        }

        void Apply(FPvpHit& hit)
        {
            hurtMarker_ = .55f;
            Present("hurt", hit.source);
        }

        void Apply(FExtractState& state)
        {
            extract_ = state;
            if (state.done)
            {
                extracted_ = true;
                message_ = "Extracted. What you carried is in your storage.";
            }
        }
        void Apply(FFarewell& farewell)
        {
            if (farewell.reason != EFarewell::Extracted) message_ = farewell.message;
        }
        void Apply(FServerReject& reject) { message_ = reject.reason; }
        void Apply(FServerPvp& pvp) { session_.pvp = pvp.enabled; }
        void Apply(FPlayerLeft& left) { session_.remotes.erase(left.id); }

        std::unique_ptr<FWebPhysics> physics_;
        PlayerController player_;
        PlayerActionController actions_;
        std::string actionLoot_;
        FLootDestination actionDestination_;
        const FWorldRayQuery rayQuery_ = [this](const glm::vec3& origin, const glm::vec3& direction, float distance)
            { return physics_->Raycast(origin, direction, distance); };
        std::unique_ptr<Assets::FRigDefinition> survivorRig_, infectedRig_;
        FAnimatedEntity<FWebSurvivorPose> localPose_;
        std::map<uint32_t, FAnimatedEntity<FWebSurvivorPose>> remotePoses_;
        std::map<uint32_t, FAnimatedEntity<FWebInfectedPose>> infectedPoses_;
        std::vector<float> poseFloats_;
        uint32_t poseFrame_ = 0;
        FFirstPersonArmPose armPose_;
        int viewModelOffset_ = -1;
        bool viewModelArms_ = false;
        std::vector<FZombieRuntime> zombies_, reviewCrowd_;
        float zombieStep_ = 0.f;
        float physicsTime_ = 0.f;
        bool aimHeld_ = false;
        bool triggerHeld_ = false, triggerPress_ = false;
        WeaponRuntime weapons_;
        EWeaponPresentationAction lastWeaponAction_ = EWeaponPresentationAction::None;
        uint32_t hits_ = 0, kills_ = 0;
        float viewKick_ = 0.f, hitMarker_ = 0.f, hurtMarker_ = 0.f;
        bool killMarker_ = false, headshot_ = false;
        FClientSession session_;
        FConfig config_;
        TimeSystem time_;
        SurvivalSystem body_;
        Inventory inventory_;
        std::vector<glm::vec3> wells_, extractions_;
        std::map<std::string, FLootReplica> loot_;
        std::map<uint32_t, FPendingLoot> pendingTakes_;
        uint32_t nextTake_ = 1;
        std::vector<Master::FBring> bring_;
        Json effects_ = Json::array();
        std::deque<FPacket> packets_;
        std::string name_, scene_, expectedScene_, map_, message_, lastFix_;
        glm::vec3 feet_{};
        std::optional<glm::vec3> target_;
        float yaw_ = 0.f;
        float retrySeconds_ = 0.f;
        uint32_t life_ = 0, snapshots_ = 0, fixes_ = 0, takes_ = 0, drops_ = 0, denials_ = 0;
        bool mapReady_ = false, spawned_ = false, lootReady_ = false, alive_ = false, extracted_ = false;
        FExtractState extract_{};
    };
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE WebCore* NdCreate() { return new WebCore; }
    EMSCRIPTEN_KEEPALIVE void NdDestroy(WebCore* core) { delete core; }
    EMSCRIPTEN_KEEPALIVE void NdReceive(WebCore* core, const std::byte* bytes, size_t length) { core->Receive({bytes, length}); }
    EMSCRIPTEN_KEEPALIVE void NdTick(WebCore* core, float dt) { core->Advance(dt); }

    EMSCRIPTEN_KEEPALIVE const char* NdLoadRig(WebCore* core, int kind, const std::byte* data, size_t length)
    {
        try
        {
            core->LoadRig(kind, {data, length});
            return nullptr;
        }
        catch (const std::exception& error)
        {
            core->result = error.what();
            return core->result.c_str();
        }
    }

    EMSCRIPTEN_KEEPALIVE const float* NdPoseData(WebCore* core) { return core->Poses().data(); }
    EMSCRIPTEN_KEEPALIVE size_t NdPoseSize(WebCore* core) { return core->Poses().size(); }

    EMSCRIPTEN_KEEPALIVE void NdControls(WebCore* core, uint32_t flags, float dx, float dy)
    {
        core->Controls(flags, dx, dy);
    }

    EMSCRIPTEN_KEEPALIVE const char* NdCollision(WebCore* core, uint32_t operation, const uint32_t* data,
                                                 size_t first, size_t second)
    {
        try
        {
            if (operation == 0) core->BeginCollision(first, second);
            else if (operation == 1) core->CollisionMesh({reinterpret_cast<const float*>(data), first}, {data + first, second});
            else if (operation == 2) core->CollisionInstances({data, first});
            else throw std::runtime_error("Unknown collision operation");
            return nullptr;
        }
        catch (const std::exception& error)
        {
            core->result = error.what();
            return core->result.c_str();
        }
    }

    EMSCRIPTEN_KEEPALIVE const char* NdCommand(WebCore* core, const char* data, size_t length)
    {
        try
        {
            core->Command(Json::parse(data, data + length));
            return nullptr;
        }
        catch (const std::exception& error)
        {
            core->result = error.what();
            return core->result.c_str();
        }
    }

    EMSCRIPTEN_KEEPALIVE const char* NdRead(WebCore* core, uint32_t view)
    {
        switch (view)
        {
        case 1: core->result = core->Profile(); break;
        case 2: core->result = core->DrainEffects(); break;
        case 3: core->result = core->Loot(); break;
        case 4: core->result = core->InventoryRows().dump(); break;
        case 5: core->result = core->NearbyLoot(); break;
        case 6: core->result = core->Cargo(); break;
        case 7: core->result = core->GroundItems(); break;
        default: core->result = core->Frame(); break;
        }
        return core->result.c_str();
    }

    EMSCRIPTEN_KEEPALIVE const char* NdPreviewClock(WebCore* core, float minutes, uint8_t weather)
    {
        TimeSystem preview;
        preview.Reset(FTimeConfig{}, 1);
        auto state = preview.Capture();
        state.minutes = std::clamp(static_cast<double>(minutes), 0.0, 1440.0);
        state.weather = static_cast<EWeather>(std::min<uint8_t>(weather, 2));
        state.blend = weather == 0 ? 0.f : weather == 1 ? .6f : .8f;
        preview.Restore(state);
        core->result = DescribeClock(preview).dump();
        return core->result.c_str();
    }

    EMSCRIPTEN_KEEPALIVE size_t NdPopPacket(WebCore* core)
    {
        return core->PopPacket() ? core->outgoing.bytes.size() : 0;
    }
    EMSCRIPTEN_KEEPALIVE const void* NdPacket(WebCore* core) { return core->outgoing.bytes.data(); }
    EMSCRIPTEN_KEEPALIVE bool NdPacketReliable(WebCore* core) { return core->outgoing.reliable; }
    EMSCRIPTEN_KEEPALIVE uint16_t NdProtocol() { return kProtocolVersion; }
}
