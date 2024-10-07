//
// Created by jpabl on 30/09/2024.
//

#ifndef MODEL_H
#define MODEL_H

#include <assimp/scene.h>

#include "vk_engine.h"
#include "vk_types.h"

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
    VkDescriptorSet materialSet;
};

class Model {
public:
    Model(const std::string& filePath);

    std::string overallPath;

    std::vector<Mesh> meshes;
    std::vector<MaterialInfo> materialInfo;
    std::unordered_map<std::string, TextureInfo> alreadyLoadedImages;

    std::vector<GPUMeshBuffers> gpuMeshBuffers;
    std::unordered_map<std::string, AllocatedImage> gpuTextures;

private:
    void populateMeshBuffer(const aiScene* scene);
    void populateTextures(const aiScene* scene);

    std::vector<std::string> loadMaterialTextures(const aiScene *scene, const aiMaterial *material, aiTextureType type, std::string typeName);
    std::optional<TextureInfo> loadTexture(const char* path, void* dataBuffer, unsigned int bufferSize);
};



#endif //MODEL_H
