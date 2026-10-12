//
// Created by jpabl on 30/09/2024.
//

#include "model.h"

#include "upload_batch.h"
#include "utils/util.h"
#include "vk_engine.h"
#include "vk_loader.h"

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
        return;
    }
    populateMeshBuffer(scene);
    populateTextures(scene);
    loaded = true;
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
            if (mesh->mNormals) {
                vertex.normal.x = mesh->mNormals[i].x;
                vertex.normal.y = mesh->mNormals[i].y;
                vertex.normal.z = mesh->mNormals[i].z;
            } else {
                vertex.normal = glm::vec3(0.f, 1.f, 0.f);
            }
            if (mesh->mTextureCoords[0]) {
                vertex.uv_x = mesh->mTextureCoords[0][i].x;
                vertex.uv_y = mesh->mTextureCoords[0][i].y;
            } else {
                vertex.uv_x = 0.f;
                vertex.uv_y = 0.f;
            }
            vertex.color = glm::vec4(1.f);

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

        auto finalPath = overallPath + str.C_Str();
        auto foundImage = alreadyLoadedImages.find(finalPath);
        if (foundImage == alreadyLoadedImages.end()) {
            const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(str.C_Str());
            auto totalSize = 0;
            void* embeddedData = nullptr;
            if (embeddedTexture) {
                totalSize = embeddedTexture->mHeight * embeddedTexture->mWidth;
                embeddedData = embeddedTexture->pcData;
            }

            auto textureResult = loadTexture(finalPath.c_str(), embeddedData, totalSize);
            if (textureResult.has_value()) {
                auto textureInfo = std::move(textureResult.value());
                textureInfo.type = typeName;
                textureInfo.path = finalPath;
                materialTexturePaths.push_back(finalPath);
                alreadyLoadedImages[finalPath] = std::move(textureInfo);
            }
        } else {
            materialTexturePaths.push_back(finalPath);
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

std::optional<std::shared_ptr<LoadedScene>> loadModel(VulkanEngine* engine, const std::string& filePath)
{
    Model model(filePath);
    if (!model.loaded) return {};

    auto scene = std::make_shared<LoadedScene>();
    scene->creator = engine;

    UploadBatch batch(*engine);

    for (auto& [path, textureInfo] : model.alreadyLoadedImages) {
        VkExtent3D extent = { (uint32_t)textureInfo.width, (uint32_t)textureInfo.height, 1 };
        VkFormat format = textureInfo.type == "texture_diffuse" ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        bool mipmapped = extent.width > 2 && extent.height > 2;
        scene->images[path] = batch.uploadImage(textureInfo.data, extent, format, VK_IMAGE_USAGE_SAMPLED_BIT, mipmapped);

        stbi_image_free(textureInfo.data);
        textureInfo.data = nullptr;
    }

    size_t materialCount = std::max<size_t>(model.materialInfo.size(), 1);
    scene->materialDataBuffer = vkutil::createBuffer(engine->allocator, sizeof(GPUGLTFMaterial) * materialCount,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkutil::HOST_WRITE_MAPPED);

    VkBufferDeviceAddressInfo deviceAddressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = scene->materialDataBuffer.buffer };
    VkDeviceAddress materialBufferAddress = vkGetBufferDeviceAddress(engine->device, &deviceAddressInfo);
    GPUGLTFMaterial* sceneMaterialConstants = (GPUGLTFMaterial*)scene->materialDataBuffer.info.pMappedData;

    std::vector<std::shared_ptr<GLTFMaterial>> materials;
    for (size_t i = 0; i < materialCount; i++) {
        VkImageView colorView = engine->whiteImage.imageView;
        if (i < model.materialInfo.size()) {
            const MaterialInfo& info = model.materialInfo[i];
            for (size_t t = 0; t < info.texturePaths.size(); t++) {
                if (info.textureFileTypes[t] != "texture_diffuse") continue;
                auto foundImage = scene->images.find(info.texturePaths[t]);
                if (foundImage != scene->images.end()) {
                    colorView = foundImage->second.imageView;
                    break;
                }
            }
        }

        GPUGLTFMaterial constants{};
        constants.colorFactors = glm::vec4(1.f);
        constants.metal_rough_factors = glm::vec4(1.f, 1.f, 0.f, 0.f);
        constants.colorTexID = engine->addGlobalTexture(colorView, engine->defaultSamplerLinear);
        constants.metalRoughTexID = engine->addGlobalTexture(engine->whiteImage.imageView, engine->defaultSamplerLinear);
        sceneMaterialConstants[i] = constants;

        auto newMat = std::make_shared<GLTFMaterial>();
        newMat->data.passType = MaterialPass::MainColor;
        newMat->data.pipeline = &engine->metalRoughMaterial.opaquePipeline;
        newMat->data.materialBuffer = materialBufferAddress + i * sizeof(GPUGLTFMaterial);

        materials.push_back(newMat);
        scene->materials["material" + std::to_string(i)] = newMat;
    }

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    std::vector<std::shared_ptr<MeshAsset>> meshAssets;

    for (size_t i = 0; i < model.meshes.size(); i++) {
        Mesh& mesh = model.meshes[i];
        auto meshAsset = std::make_shared<MeshAsset>();
        meshAsset->name = mesh.name.empty() ? "mesh" + std::to_string(i) : mesh.name;

        GeoSurface surface;
        surface.startIndex = (uint32_t)indices.size();
        surface.count = (uint32_t)mesh.indices.size();
        surface.material = materials[std::min<size_t>(mesh.materialIndex, materials.size() - 1)];

        size_t initialVertex = vertices.size();
        indices.reserve(indices.size() + mesh.indices.size());
        for (uint32_t idx : mesh.indices) {
            indices.push_back(idx + (uint32_t)initialVertex);
        }
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());

        if (!mesh.vertices.empty()) {
            glm::vec3 minpos = vertices[initialVertex].position;
            glm::vec3 maxpos = minpos;
            for (size_t v = initialVertex; v < vertices.size(); v++) {
                minpos = glm::min(minpos, vertices[v].position);
                maxpos = glm::max(maxpos, vertices[v].position);
            }
            surface.bounds.origin = (maxpos + minpos) / 2.f;
            surface.bounds.extents = (maxpos - minpos) / 2.f;
            surface.bounds.sphereRadius = glm::length(surface.bounds.extents);
        } else {
            surface.bounds = Bounds { glm::vec3(0.f), 0.f, glm::vec3(0.f) };
        }

        meshAsset->surfaces.push_back(surface);
        meshAssets.push_back(meshAsset);
        scene->meshes[meshAsset->name + "_" + std::to_string(i)] = meshAsset;
    }

    if (!vertices.empty()) {
        auto shared = std::make_shared<GPUMeshBuffers>(batch.uploadMesh(indices, vertices));
        for (auto& meshAsset : meshAssets) {
            meshAsset->meshBuffers = shared;
        }
    }

    for (size_t i = 0; i < meshAssets.size(); i++) {
        auto node = std::make_shared<MeshNode>();
        node->mesh = meshAssets[i];
        node->localTransform = glm::mat4(1.f);
        node->worldTransform = glm::mat4(1.f);
        scene->nodes["node" + std::to_string(i)] = node;
        scene->topNodes.push_back(node);
    }

    batch.submitAndWait();
    return scene;
}
