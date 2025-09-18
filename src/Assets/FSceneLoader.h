#pragma once

#include "Model.hpp"

namespace Assets
{
    struct FMaterial;
    class Node;
    
    class FSceneLoader
    {
    public:
        static Camera AutoFocusCamera(Assets::EnvironmentSetting& cameraInit, std::vector<Model>& models);
        
        static bool LoadGLTFScene(const std::string& filename, Assets::EnvironmentSetting& cameraInit, std::vector< std::shared_ptr<Assets::Node> >& nodes,
                                  std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks);

        static void GenerateMikkTSpace(Assets::Model *m);

        static std::string currSceneName;
    };

}
