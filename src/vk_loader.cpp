#include "stb_image.h"
#include <iostream>
#include <unordered_set>
#include <vk_loader.h>

#include "vk_engine.h"
#include "model/model.h"
#include "upload_batch.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/util.hpp>

#include "utils/util.h"
std::optional<AllocatedImage> loadImage(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image, UploadBatch& batch, VkFormat format)
{
    AllocatedImage newImage {};

    int width, height, nrChannels;

    std::visit(
        fastgltf::visitor {
            [](auto& arg) {},
            [&](fastgltf::sources::URI& filePath) {
                assert(filePath.fileByteOffset == 0); // We don't support offsets with stbi.
                assert(filePath.uri.isLocalPath()); // We're only capable of loading
                                                    // local files.

                const std::string path(filePath.uri.path().begin(),
                    filePath.uri.path().end()); // Thanks C++.
                unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
                if (data) {
                    VkExtent3D imagesize;
                    imagesize.width = width;
                    imagesize.height = height;
                    imagesize.depth = 1;

                    newImage = batch.uploadImage(data, imagesize, format, VK_IMAGE_USAGE_SAMPLED_BIT,true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::Vector& vector) {
                unsigned char* data = stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()),
                    &width, &height, &nrChannels, 4);
                if (data) {
                    VkExtent3D imagesize;
                    imagesize.width = width;
                    imagesize.height = height;
                    imagesize.depth = 1;

                    newImage = batch.uploadImage(data, imagesize, format, VK_IMAGE_USAGE_SAMPLED_BIT,true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::BufferView& view) {
                auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                auto& buffer = asset.buffers[bufferView.bufferIndex];

                std::visit(fastgltf::visitor { // We only care about VectorWithMime here, because we
                                               // specify LoadExternalBuffers, meaning all buffers
                                               // are already loaded into a vector.
                               [](auto& arg) {},
                               [&](fastgltf::sources::Vector& vector) {
                                   unsigned char* data = stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset,
                                       static_cast<int>(bufferView.byteLength),
                                       &width, &height, &nrChannels, 4);
                                   if (data) {
                                       VkExtent3D imagesize;
                                       imagesize.width = width;
                                       imagesize.height = height;
                                       imagesize.depth = 1;

                                       newImage = batch.uploadImage(data, imagesize, format,
                                           VK_IMAGE_USAGE_SAMPLED_BIT,true);

                                       stbi_image_free(data);
                                   }
                               } },
                    buffer.data);
            },
        },
        image.data);

    // if any of the attempts to load the data failed, we havent written the image
    // so handle is null
    if (newImage.image == VK_NULL_HANDLE) {
        return {};
    } else {
        return newImage;
    }
}
VkFilter extractFilter(fastgltf::Filter filter)
{
    switch (filter) {
    // nearest samplers
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return VK_FILTER_NEAREST;

    // linear samplers
    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode extractMipmapMode(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;

    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

std::optional<std::shared_ptr<LoadedScene>> loadGltf(VulkanEngine* engine, std::string_view filePath)
{
    fmt::print("Loading GLTF: {}", filePath);

    std::shared_ptr<LoadedScene> scene = std::make_shared<LoadedScene>();
    scene->creator = engine;
    LoadedScene& file = *scene.get();

    fastgltf::Parser parser {};

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
    // fastgltf::Options::LoadExternalImages;

    fastgltf::GltfDataBuffer data;
    data.loadFromFile(filePath);

    fastgltf::Asset gltf;

    std::filesystem::path path = filePath;

    auto type = fastgltf::determineGltfFileType(&data);
    if (type == fastgltf::GltfType::glTF) {
        auto load = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
            return {};
        }
    } else if (type == fastgltf::GltfType::GLB) {
        auto load = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
            return {};
        }
    } else {
        std::cerr << "Failed to determine glTF container" << std::endl;
        return {};
    }

    UploadBatch batch(engine->uploadContext);

    // we can stimate the descriptors we will need accurately

    // load samplers
    for (fastgltf::Sampler& sampler : gltf.samplers) {

        VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr};
        sampl.maxLod = VK_LOD_CLAMP_NONE;
        sampl.minLod = 0;

        sampl.magFilter = extractFilter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        sampl.minFilter = extractFilter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        sampl.mipmapMode= extractMipmapMode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler newSampler;
        vkCreateSampler(engine->device, &sampl, nullptr, &newSampler);

        file.samplers.push_back(newSampler);
    }
    // temporal arrays for all the objects to use while creating the GLTF data
    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<AllocatedImage*> images;
    std::vector<std::shared_ptr<GLTFMaterial>> materials;

    // load all textures
    std::unordered_set<size_t> srgbImages;
    for (fastgltf::Material& mat : gltf.materials) {
        if (mat.pbrData.baseColorTexture.has_value()) {
            auto imageIndex = gltf.textures[mat.pbrData.baseColorTexture->textureIndex].imageIndex;
            if (imageIndex.has_value()) srgbImages.insert(imageIndex.value());
        }
        if (mat.emissiveTexture.has_value()) {
            auto imageIndex = gltf.textures[mat.emissiveTexture->textureIndex].imageIndex;
            if (imageIndex.has_value()) srgbImages.insert(imageIndex.value());
        }
    }

    for (size_t imageIdx = 0; imageIdx < gltf.images.size(); imageIdx++) {
        fastgltf::Image& image = gltf.images[imageIdx];
        VkFormat format = srgbImages.contains(imageIdx) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        std::optional<AllocatedImage> img = loadImage(engine, gltf, image, batch, format);

        if (img.has_value()) {
            file.images[image.name.c_str()] = std::move(*img);
            images.push_back(&file.images[image.name.c_str()]);
        } else {
            // we failed to load, so lets give the slot a default white texture to not
            // completely break loading
            images.push_back(&engine->errorCheckerboardImage);
            std::cout << "gltf failed to load texture " << image.name << std::endl;
        }
    }

    // create buffer to hold the material data
    file.materialDataBuffer = vkutil::createBuffer(engine->allocator, sizeof(GPUGLTFMaterial) * gltf.materials.size(),
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkutil::HOST_WRITE_MAPPED);

    VkBufferDeviceAddressInfo deviceAddressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = file.materialDataBuffer.buffer };
    VkDeviceAddress materialBufferAddress = vkGetBufferDeviceAddress(engine->device, &deviceAddressInfo);

    int data_index = 0;
    GPUGLTFMaterial* sceneMaterialConstants = (GPUGLTFMaterial*)file.materialDataBuffer.info.pMappedData;
    //
    for (fastgltf::Material& mat : gltf.materials) {
        std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
        materials.push_back(newMat);
        file.materials[mat.name.c_str()] = newMat;

        GPUGLTFMaterial constants{};
        constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
        constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
        constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
        constants.colorFactors.w = mat.pbrData.baseColorFactor[3];

        constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
        constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;

        MaterialPass passType = MaterialPass::MainColor;
        if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
            passType = MaterialPass::Transparent;
        }

        // grab textures from gltf file
        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t img = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

            constants.colorTexID = engine->addGlobalTexture(images[img]->imageView, file.samplers[sampler]);
        } else {
            constants.colorTexID = engine->addGlobalTexture(engine->whiteImage.imageView, engine->defaultSamplerLinear);
        }

        if (mat.pbrData.metallicRoughnessTexture.has_value()) {
            size_t img = gltf.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex].imageIndex.value();
            size_t sampler = gltf.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex].samplerIndex.value();

            constants.metalRoughTexID = engine->addGlobalTexture(images[img]->imageView, file.samplers[sampler]);
        } else {
            constants.metalRoughTexID = engine->addGlobalTexture(engine->whiteImage.imageView, engine->defaultSamplerLinear);
        }

        // write material parameters to buffer
        sceneMaterialConstants[data_index] = constants;

        // build material instance
        newMat->data.passType = passType;
        newMat->data.pipeline = (passType == MaterialPass::Transparent) ? &engine->metalRoughMaterial.transparentPipeline : &engine->metalRoughMaterial.opaquePipeline;
        newMat->data.materialBuffer = materialBufferAddress + (data_index * sizeof(GPUGLTFMaterial));

        data_index++;
    }

    // all meshes in the file share one vertex buffer and one index buffer
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    for (fastgltf::Mesh& mesh : gltf.meshes) {
        std::shared_ptr<MeshAsset> newmesh = std::make_shared<MeshAsset>();
        meshes.push_back(newmesh);
        file.meshes[mesh.name.c_str()] = newmesh;
        newmesh->name = mesh.name;

        for (auto&& p : mesh.primitives) {
            GeoSurface newSurface;
            newSurface.startIndex = (uint32_t)indices.size();
            newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

            size_t initial_vtx = vertices.size();

            // load indexes
            {
                fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexaccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
                    [&](std::uint32_t idx) {
                        indices.push_back(idx + initial_vtx);
                    });
            }

            // load vertex positions
            {
                fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
                    [&](glm::vec3 v, size_t index) {
                        Vertex newvtx;
                        newvtx.position = v;
                        newvtx.normal = { 1, 0, 0 };
                        newvtx.color = glm::vec4 { 1.f };
                        newvtx.uv_x = 0;
                        newvtx.uv_y = 0;
                        vertices[initial_vtx + index] = newvtx;
                    });
            }

            // load vertex normals
            auto normals = p.findAttribute("NORMAL");
            if (normals != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second],
                    [&](glm::vec3 v, size_t index) {
                        vertices[initial_vtx + index].normal = v;
                    });
            }

            // load UVs
            auto uv = p.findAttribute("TEXCOORD_0");
            if (uv != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second],
                    [&](glm::vec2 v, size_t index) {
                        vertices[initial_vtx + index].uv_x = v.x;
                        vertices[initial_vtx + index].uv_y = v.y;
                    });
            }

            // load vertex colors
            auto colors = p.findAttribute("COLOR_0");
            if (colors != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second],
                    [&](glm::vec4 v, size_t index) {
                        vertices[initial_vtx + index].color = v;
                    });
            }

            if (p.materialIndex.has_value()) {
                newSurface.material = materials[p.materialIndex.value()];
            } else {
                newSurface.material = materials[0];
            }

            glm::vec3 minpos = vertices[initial_vtx].position;
            glm::vec3 maxpos = vertices[initial_vtx].position;
            for (int i = initial_vtx; i < vertices.size(); i++) {
                minpos = glm::min(minpos, vertices[i].position);
                maxpos = glm::max(maxpos, vertices[i].position);
            }

            newSurface.bounds.origin = (maxpos + minpos) / 2.f;
            newSurface.bounds.extents = (maxpos - minpos) / 2.f;
            newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);
            newmesh->surfaces.push_back(newSurface);
        }

    }

    if (!vertices.empty()) {
        auto sharedBuffers = std::make_shared<GPUMeshBuffers>(batch.uploadMesh(indices, vertices));
        for (auto& m : meshes) {
            m->meshBuffers = sharedBuffers;
        }
    }
    // load all nodes and their meshes
    for (fastgltf::Node& node : gltf.nodes) {
        std::shared_ptr<Node> newNode;

        // find if the node has a mesh, and if it does hook it to the mesh pointer and allocate it with the meshnode class
        if (node.meshIndex.has_value()) {
            newNode = std::make_shared<MeshNode>();
            static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
        } else {
            newNode = std::make_shared<Node>();
        }

        nodes.push_back(newNode);
        file.nodes[node.name.c_str()];

        std::visit(fastgltf::visitor { [&](fastgltf::Node::TransformMatrix matrix) {
                                          memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
                                      },
                       [&](fastgltf::Node::TRS transform) {
                           glm::vec3 tl(transform.translation[0], transform.translation[1],
                               transform.translation[2]);
                           glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1],
                               transform.rotation[2]);
                           glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

                           glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
                           glm::mat4 rm = glm::toMat4(rot);
                           glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

                           newNode->localTransform = tm * rm * sm;
                       } },
            node.transform);
    }
    // run loop again to setup transform hierarchy
    for (int i = 0; i < gltf.nodes.size(); i++) {
        fastgltf::Node& node = gltf.nodes[i];
        std::shared_ptr<Node>& sceneNode = nodes[i];

        for (auto& c : node.children) {
            sceneNode->children.push_back(nodes[c]);
            nodes[c]->parent = sceneNode;
        }
    }

    // find the top nodes, with no parents
    for (auto& node : nodes) {
        if (node->parent.lock() == nullptr) {
            file.topNodes.push_back(node);
            node->refreshTransform(glm::mat4 { 1.f });
        }
    }
    batch.submitAndWait();

    return scene;
}

void LoadedScene::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    // create renderables from the scenenodes
    for (auto& n : topNodes) {
        n->Draw(topMatrix, ctx);
    }
}

void LoadedScene::clearAll()
{
    for (auto& sampler : samplers) {
        creator->deferDestroy(sampler);
    }
}

std::optional<std::shared_ptr<LoadedScene>> loadModel(VulkanEngine* engine, const std::string& filePath)
{
    Model model(filePath);
    if (!model.loaded) return {};

    auto scene = std::make_shared<LoadedScene>();
    scene->creator = engine;

    UploadBatch batch(engine->uploadContext);

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
