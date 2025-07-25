//
// Created by jpabl on 30/09/2024.
//

#include "model.h"

#include <iostream>
#include <ostream>
#include <stb_image.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

Model::Model(const std::string& filePath) {
    auto finalPath = filePath;
    size_t beginningOfPath = finalPath.find_last_of('\\');
    std::string pathWithoutModelName = finalPath.substr(0, beginningOfPath+1);
    overallPath = pathWithoutModelName;

    Assimp::Importer importer;
    unsigned int flags = aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType;
    const aiScene* scene = importer.ReadFile(finalPath, flags);

    if (scene == nullptr) {
        std::cerr << "Failed to load model " << finalPath << std::endl;
        std::cerr << importer.GetErrorString() << std::endl;
    }
    populateMeshBuffer(scene);
    populateTextures(scene);
}

void Model::populateMeshBuffer(const aiScene* scene) {
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        Mesh newMesh;
        newMesh.materialIndex = mesh->mMaterialIndex;
        newMesh.name = mesh->mName.C_Str();
        newMesh.vertices.resize(mesh->mNumVertices);
        newMesh.indices.resize(mesh->mNumFaces * 3);
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;
            vertex.position.x = mesh->mVertices[i].x;
            vertex.position.y = mesh->mVertices[i].y;
            vertex.position.z = mesh->mVertices[i].z;
            vertex.normal.x = mesh->mNormals[i].x;
            vertex.normal.y = mesh->mNormals[i].y;
            vertex.normal.z = mesh->mNormals[i].z;
            vertex.uv_x = mesh->mTextureCoords[0][i].x;
            vertex.uv_y = mesh->mTextureCoords[0][i].y;

            newMesh.vertices[i] = vertex;
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace& face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                newMesh.indices[i * 3 + j] = face.mIndices[j];
            }
        }

        meshes.push_back(newMesh);
    }
}

void Model::populateTextures(const aiScene *scene) {
    materialInfo.reserve(scene->mNumMaterials);

    std::vector<std::pair<aiTextureType, std::string>> textureTypes = {
        {aiTextureType_DIFFUSE, "texture_diffuse"},
        {aiTextureType_SPECULAR, "texture_specular"},
        {aiTextureType_NORMALS, "texture_normal"},
        {aiTextureType_AMBIENT, "texture_height"},
        {aiTextureType_LIGHTMAP, "texture_ao"},
        {aiTextureType_METALNESS, "texture_metallic"},
        {aiTextureType_DIFFUSE_ROUGHNESS, "texture_roughness"}
    };

    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        aiMaterial* material = scene->mMaterials[i];
        std::vector<std::string> allTexturePaths;
        std::vector<std::string> allTextureTypes;
        for (auto& [assimpType, typeName] : textureTypes) {
            auto texturePaths = loadMaterialTextures(scene, material, assimpType, typeName);
            allTexturePaths.insert(allTexturePaths.end(), texturePaths.begin(), texturePaths.end());
            for (auto path: texturePaths) {
                allTextureTypes.push_back(typeName);
            }
        }

        materialInfo.push_back(MaterialInfo(allTexturePaths, allTextureTypes));
    }
}

std::vector<std::string> Model::loadMaterialTextures(const aiScene *scene, const aiMaterial *material, aiTextureType type,
    std::string typeName) {
    std::vector<std::string> materialTexturePaths;

    auto numTextures = material->GetTextureCount(type);
    materialTexturePaths.reserve(numTextures);
    for (unsigned int i = 0; i < numTextures; i++) {
        aiString str;
        material->GetTexture(type, i, &str);

        auto foundImage = alreadyLoadedImages.find(str.C_Str());
        if (foundImage == alreadyLoadedImages.end()) {
            const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(str.C_Str());
            auto totalSize = 0;
            void* embeddedData = nullptr;
            if (embeddedTexture) {
                totalSize = embeddedTexture->mHeight * embeddedTexture->mWidth;
                embeddedData = embeddedTexture->pcData;
            }

            auto filePath = str.C_Str();
            auto finalPath = overallPath + filePath;
            auto textureResult = loadTexture(finalPath.c_str(), embeddedData, totalSize);
            if (textureResult.has_value()) {
                auto textureInfo = std::move(textureResult.value());
                textureInfo.type = typeName;
                textureInfo.path = finalPath;
                materialTexturePaths.push_back(finalPath);
                alreadyLoadedImages[finalPath] = std::move(textureInfo);
            }
        }
    }

    return materialTexturePaths;
}

std::optional<TextureInfo> Model::loadTexture(const char *path, void *dataBuffer, unsigned int bufferSize) {
    unsigned char* finalLoadedData = nullptr;
    TextureInfo info;
    if (dataBuffer && bufferSize != 0) {
        finalLoadedData = stbi_load_from_memory((const stbi_uc *)dataBuffer, bufferSize, &info.width, &info.height, &info.channels, 4);
    } else {
        finalLoadedData = stbi_load(path, &info.width, &info.height, &info.channels, 4);
        info.path = path;
    }

    if (!finalLoadedData) {
        std::cerr << "Failed to load texture " << std::endl;
        stbi_image_free(finalLoadedData);

        return {};
    }
    info.data = finalLoadedData;

    return info;
}

