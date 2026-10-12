//
// Created by jpabl on 30/09/2024.
//

#ifndef MODEL_H
#define MODEL_H

#include <assimp/scene.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "vk_types.h"

class VulkanEngine;
struct LoadedScene;

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::string name;
    unsigned int materialIndex;
};

struct TextureInfo {
    unsigned char* data;

    std::string path;
    std::string type;

    int width;
    int height;
    int channels;
};

struct MaterialInfo {
    std::vector<std::string> texturePaths;
    std::vector<std::string> textureFileTypes;
};

class Model {
public:
    Model(const std::string& filePath);

    bool loaded { false };
    std::string overallPath;

    std::vector<Mesh> meshes;
    std::vector<MaterialInfo> materialInfo;
    std::unordered_map<std::string, TextureInfo> alreadyLoadedImages;

private:
    void populateMeshBuffer(const aiScene* scene);
    void populateTextures(const aiScene* scene);

    std::vector<std::string> loadMaterialTextures(const aiScene *scene, const aiMaterial *material, aiTextureType type, std::string typeName);
    std::optional<TextureInfo> loadTexture(const char* path, void* dataBuffer, unsigned int bufferSize);
};



std::optional<std::shared_ptr<LoadedScene>> loadModel(VulkanEngine* engine, const std::string& filePath);

#endif //MODEL_H
