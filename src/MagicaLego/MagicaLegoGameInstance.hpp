#include "Runtime/Application.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

enum ELegoMode
{
	ELM_Dig,
	ELM_Place,
	ELM_Select,
};

struct FBasicBlock
{
	// just need modelIdNow
	int modelId_;
	int matType;
	glm::vec4 color;
};

struct FPlacedBlock
{
	glm::ivec3 location; // 对应一个hash
	int modelId_; // 如果为-1，表示已经被挖掉了
};


using namespace glm;

namespace std
{
	template <>
	struct hash<FPlacedBlock> final
	{
		size_t operator()(const FPlacedBlock& b) const noexcept
		{
			return
				Combine(hash<glm::ivec3>()(b.location), hash<int>()(b.modelId_));
		}

	private:
		static size_t Combine(size_t hash0, size_t hash1)
		{
			return hash0 ^ (hash1 + 0x9e3779b9 + (hash0 << 6) + (hash0 >> 2));
		}
	};
};

class MagicaLegoGameInstance : public NextGameInstanceBase
{
public:
	MagicaLegoGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine);
    ~MagicaLegoGameInstance() {}
	
    void OnInit() override {}
    void OnTick() override {}
    void OnDestroy() override {}
	bool OnRenderUI() override;
    void OnRayHitResponse(Assets::RayCastResult& result) override;

	void OnSceneLoaded() override;
	void OnSceneUnloaded() override;
	
    bool OnKey(int key, int scancode, int action, int mods) override {return false;}
    bool OnCursorPosition(double xpos, double ypos) override {return false;}
    bool OnMouseButton(int button, int action, int mods) override {return false;}

protected:
	void AddBasicBlock(std::string blockName);
	FBasicBlock* GetBasicBlock(uint32_t BlockIdx);

	void PlaceDynamicBlock(FPlacedBlock Block);
	void RemoveDynamicBlock(FPlacedBlock Block);
	
	void RebuildScene();
	
	void DrawLeftBar();
	void DrawRightBar();
	
private:
	ELegoMode currentMode_;

	// 基础的方块
	std::vector<FBasicBlock> BasicNodes;

	int currentBlockIdx_ {};

	// 起始的方块位置，之后的instance都是rebuild出来的
	int instanceCountBeforeDynamics_ {};

	// 起始的方块，静态，无需加速结构，不会被重建
	std::vector<FPlacedBlock> BlocksFromScene;

	// 基础加速结构，location -> uint64_t，存储已经放置的方块
	std::unordered_map<glm::ivec3, FPlacedBlock> BlocksDynamics;



	NextRendererApplication& GetEngine() {return *engine_;}
	NextRendererApplication* engine_;
};
