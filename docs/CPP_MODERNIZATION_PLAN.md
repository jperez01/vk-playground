# C++ Modernization Plan

## Executive Summary
This document outlines a comprehensive plan to modernize the C++ codebase of the Vulkan Playground project. The current code uses C++20 but can benefit from modern C++ best practices, improved memory management, better error handling, and enhanced code organization.

---

## 1. Memory Management & Smart Pointers

### Current Issues
- Extensive use of raw pointers and manual resource management
- Deletion queues for cleanup instead of RAII
- Manual `new`/`delete` patterns with shared_ptr wrapping
- Inconsistent ownership semantics

### Modernization Actions

#### 1.1 Replace DeletionQueue with RAII Wrappers
**Priority: High**

**Files Affected:**
- `src/vk_engine.h` (DeletionQueue struct)
- `src/vk_engine.cpp` (all deletion queue usage)
- `shared/vk_types.h` (resource structs)

**Action Items:**
- Create RAII wrapper classes for Vulkan resources:
  ```cpp
  template<typename VkHandle, auto Deleter>
  class VulkanResource {
      VkHandle handle;
      VkDevice device;
  public:
      VulkanResource(VkDevice dev, VkHandle h) : device(dev), handle(h) {}
      ~VulkanResource() { if (handle) Deleter(device, handle, nullptr); }
      // Move semantics, no copy
  };
  ```
- Replace `DeletionQueue` usage with these RAII wrappers
- Use `std::unique_ptr` with custom deleters for VMA-allocated resources

**Benefits:**
- Automatic cleanup on scope exit
- Exception-safe resource management
- Clearer ownership semantics
- Reduced chance of resource leaks

#### 1.2 Modernize AllocatedImage and AllocatedBuffer
**Priority: Medium**

**Files Affected:**
- `shared/vk_types.h`
- `src/utils/util.h` and `util.cpp`

**Action Items:**
- Add constructors, destructors, and move semantics to `AllocatedImage` and `AllocatedBuffer`
- Make them non-copyable but movable
- Integrate with custom deleters for automatic cleanup
- Consider using `std::unique_ptr<AllocatedImage>` for ownership transfer

#### 1.3 Replace Raw Pointers with Smart Pointers
**Priority: Medium**

**Files Affected:**
- `src/vk_engine.h` (SDL_Window*, MaterialInstance*)
- `src/model/model.h`

**Action Items:**
- Use `std::unique_ptr` for exclusively owned resources
- Use `std::shared_ptr` only when shared ownership is truly needed
- Consider SDL2 smart pointer wrappers or custom deleters
- Replace `MaterialInstance*` with `std::shared_ptr<MaterialInstance>` or value types

---

## 2. Error Handling

### Current Issues
- Macros (`VK_CHECK`) for error handling
- Inconsistent error reporting (fmt::print, std::cerr, abort())
- No structured error recovery
- Optional returns without proper error context

### Modernization Actions

#### 2.1 Implement Result/Expected Type
**Priority: High**

**Files Affected:**
- Create new `shared/result.h`
- Refactor throughout codebase

**Action Items:**
- Implement or use `std::expected` (C++23) or a backport:
  ```cpp
  template<typename T, typename E = std::string>
  using Result = std::expected<T, E>;
  ```
- Create custom error types for different failure categories:
  ```cpp
  enum class VulkanError {
      InitializationFailed,
      DeviceLost,
      OutOfMemory,
      ShaderCompilationFailed,
      // ...
  };
  
  struct Error {
      VulkanError code;
      std::string message;
      std::source_location location;
  };
  ```
- Replace `VK_CHECK` macro with error-returning functions
- Replace `std::optional` returns with `Result<T, Error>` for better error context

#### 2.2 Structured Error Logging
**Priority: Medium**

**Files Affected:**
- Create new `shared/logger.h`
- All files using fmt::print or std::cerr

**Action Items:**
- Implement a proper logging system with levels (Debug, Info, Warning, Error, Critical)
- Use structured logging with categories (Vulkan, Rendering, IO, etc.)
- Replace direct fmt::print/std::cerr calls with logger calls
- Add compile-time log level filtering

#### 2.3 Exception Safety
**Priority: Medium**

**Files Affected:**
- All initialization code
- Resource creation functions

**Action Items:**
- Ensure strong exception guarantee for initialization functions
- Use RAII throughout to maintain exception safety
- Document exception guarantees for public APIs
- Consider adding `noexcept` where appropriate

---

## 3. Modern C++ Features

### Current Issues
- Inconsistent use of modern C++ features
- C-style arrays and pointer arithmetic
- Manual loops where algorithms would be clearer
- Missing constexpr usage

### Modernization Actions

#### 3.1 Adopt Ranges and Views (C++20)
**Priority: Medium**

**Files Affected:**
- `src/vk_engine.cpp` (draw_geometry, update_scene)
- `src/vk_loader.cpp`
- `src/model/model.cpp`

**Action Items:**
- Replace manual loops with range-based operations:
  ```cpp
  // Before
  for (int i = 0; i < drawCommands.OpaqueSurfaces.size(); i++) {
      if (is_visible(...)) opaque_draws.push_back(i);
  }
  
  // After
  auto visible = drawCommands.OpaqueSurfaces 
      | std::views::enumerate
      | std::views::filter([](auto&& item) { 
          return is_visible(std::get<1>(item), ...); 
      })
      | std::views::keys;
  ```
- Use `std::ranges::sort` instead of `std::sort`
- Apply range adaptors for filtering and transformation

#### 3.2 Structured Bindings and Decomposition
**Priority: Low**

**Files Affected:**
- Throughout codebase

**Action Items:**
- Use structured bindings for tuple-like types:
  ```cpp
  for (auto& [type, name] : textureTypes) { /* ... */ }
  ```
- Apply to range enumeration and map iteration

#### 3.3 Constexpr and Compile-Time Computation
**Priority: Low**

**Files Affected:**
- `shared/vk_types.h`
- `src/vk_engine.h`

**Action Items:**
- Mark constants and simple functions as `constexpr`:
  ```cpp
  constexpr unsigned int FRAME_OVERLAP = 2;
  constexpr VkExtent2D defaultWindowExtent{1280, 720};
  ```
- Use `consteval` for compile-time only functions
- Consider `constinit` for static variables

#### 3.4 Concepts for Template Constraints
**Priority: Low**

**Files Affected:**
- Any templated code

**Action Items:**
- Define concepts for Vulkan resources:
  ```cpp
  template<typename T>
  concept VulkanHandle = requires(T t) {
      { t.handle } -> std::convertible_to<VkHandle>;
  };
  ```
- Use concepts to constrain template parameters

---

## 4. Code Organization & Architecture

### Current Issues
- Monolithic VulkanEngine class (292 lines header, 1400+ lines implementation)
- Mixed responsibilities (rendering, resource management, UI)
- Tight coupling between systems
- Global singleton pattern

### Modernization Actions

#### 4.1 Separate Concerns - Extract Subsystems
**Priority: High**

**Files Affected:**
- `src/vk_engine.h` and `vk_engine.cpp`
- Create new subsystem files

**Action Items:**
- Extract renderer subsystem:
  - `src/rendering/renderer.h/cpp` - Core rendering loop
  - `src/rendering/draw_context.h/cpp` - Draw command management
  - `src/rendering/render_pass_manager.h/cpp` - Render pass abstraction
  
- Extract resource management:
  - `src/resources/resource_manager.h/cpp` - Centralized resource tracking
  - `src/resources/texture_manager.h/cpp` - Texture loading and caching
  - `src/resources/mesh_manager.h/cpp` - Mesh buffer management
  
- Extract Vulkan context:
  - `src/vulkan/context.h/cpp` - Device, queue, instance management
  - `src/vulkan/swapchain.h/cpp` - Swapchain management
  - `src/vulkan/command_buffer_manager.h/cpp` - Command buffer allocation
  
- Update VulkanEngine to be a facade coordinating these subsystems

#### 4.2 Dependency Injection Instead of Singleton
**Priority: Medium**

**Files Affected:**
- `src/vk_engine.h` (Get() method)
- All files calling VulkanEngine::Get()

**Action Items:**
- Remove global `loadedEngine` pointer
- Remove `VulkanEngine::Get()` singleton accessor
- Pass engine reference/pointer to functions that need it
- Use constructor injection for subsystems

#### 4.3 Interface Segregation
**Priority: Medium**

**Files Affected:**
- New interface files

**Action Items:**
- Define narrow interfaces for different responsibilities:
  ```cpp
  class IImageCreator {
  public:
      virtual ~IImageCreator() = default;
      virtual Result<AllocatedImage> createImage(...) = 0;
  };
  
  class IMeshUploader {
  public:
      virtual ~IMeshUploader() = default;
      virtual Result<GPUMeshBuffers> uploadMesh(...) = 0;
  };
  ```
- Make VulkanEngine implement multiple interfaces
- Allow subsystems to depend on interfaces, not concrete VulkanEngine

#### 4.4 Modern CMake Usage
**Priority: Low**

**Files Affected:**
- `CMakeLists.txt`
- `src/CMakeLists.txt`
- `shared/CMakeLists.txt`

**Action Items:**
- Use target-based CMake instead of variables:
  ```cmake
  target_compile_features(demo PUBLIC cxx_std_20)
  target_include_directories(demo PUBLIC 
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  ```
- Separate public and private dependencies
- Use modern package finding mechanisms
- Add CMake presets for common configurations

---

## 5. Type Safety & Strong Types

### Current Issues
- Primitive types used for domain concepts
- Indices passed as generic integers
- No compile-time distinction between different ID types
- Implicit conversions can cause bugs

### Modernization Actions

#### 5.1 Strong Type Wrappers
**Priority: Medium**

**Files Affected:**
- Create `shared/strong_types.h`
- Refactor throughout codebase

**Action Items:**
- Create strong type wrappers:
  ```cpp
  template<typename T, typename Tag>
  struct StrongType {
      T value;
      explicit StrongType(T v) : value(v) {}
      explicit operator T() const { return value; }
      // Comparison operators...
  };
  
  using FrameIndex = StrongType<uint32_t, struct FrameIndexTag>;
  using MeshIndex = StrongType<uint32_t, struct MeshIndexTag>;
  using MaterialIndex = StrongType<uint32_t, struct MaterialIndexTag>;
  ```
- Replace raw indices with strong types
- Prevents accidentally mixing different index types

#### 5.2 Enum Classes for All Enumerations
**Priority: Low**

**Files Affected:**
- `shared/vk_types.h`
- Any files with C-style enums

**Action Items:**
- Ensure all enums are `enum class`
- Add explicit underlying types where appropriate
- Implement bitwise operators for flag enums using inline constexpr functions

---

## 6. String Handling

### Current Issues
- Mix of std::string, C-strings, and string views
- Unnecessary string copies
- Path manipulation using string operations

### Modernization Actions

#### 6.1 Consistent String View Usage
**Priority: Medium**

**Files Affected:**
- All function signatures taking strings
- `src/vk_loader.cpp`
- `src/model/model.cpp`

**Action Items:**
- Use `std::string_view` for function parameters that only read strings
- Use `std::string` for owned strings
- Never pass `const std::string&` when `std::string_view` suffices
- Document when string lifetime must extend beyond function call

#### 6.2 Filesystem Library Usage
**Priority: Medium**

**Files Affected:**
- `src/model/model.cpp` (path manipulation)
- `src/vk_loader.cpp`
- `shared/vk_pipelines.cpp` (shader loading)

**Action Items:**
- Replace string-based path manipulation with `std::filesystem::path`
- Use proper path concatenation: `path / "filename"`
- Utilize `path.parent_path()`, `path.stem()`, `path.extension()`
- Handle path encoding correctly across platforms

---

## 7. Initialization & Configuration

### Current Issues
- Hardcoded constants scattered throughout code
- No configuration file support
- Magic numbers in multiple locations
- Initialization order dependencies

### Modernization Actions

#### 7.1 Configuration System
**Priority: Medium**

**Files Affected:**
- Create `src/config/config.h/cpp`
- All files with hardcoded values

**Action Items:**
- Implement configuration structure:
  ```cpp
  struct EngineConfig {
      struct {
          uint32_t width = 1280;
          uint32_t height = 720;
          bool fullscreen = false;
          bool vsync = true;
      } window;
      
      struct {
          uint32_t frameOverlap = 2;
          float renderScale = 1.0f;
          bool validationLayers = true;
      } rendering;
      
      // ... more sections
  };
  ```
- Support loading from JSON/TOML configuration files
- Provide defaults and validation
- Pass config to subsystems during initialization

#### 7.2 Builder Pattern for Complex Objects
**Priority: Low**

**Files Affected:**
- `src/vk_engine.h/cpp` (initialization)
- `shared/vk_pipelines.h/cpp`

**Action Items:**
- Extend PipelineBuilder pattern to other complex objects
- Create EngineBuilder for step-by-step engine initialization:
  ```cpp
  auto engine = EngineBuilder()
      .withWindow(1920, 1080)
      .withValidation(true)
      .withRenderScale(1.5f)
      .build();
  ```
- Validate configuration before building

---

## 8. Containers & Data Structures

### Current Issues
- Inefficient data structures for some use cases
- Unnecessary allocations in hot paths
- Missing small vector optimizations
- No object pooling for frequent allocations

### Modernization Actions

#### 8.1 Optimize Container Choices
**Priority: Medium**

**Files Affected:**
- `src/vk_engine.h` (various vectors)
- `shared/vk_descriptors.h` (deque usage)

**Action Items:**
- Consider `std::array` for fixed-size collections (FrameData[FRAME_OVERLAP])
- Use `std::vector::reserve()` when size is predictable
- Consider `std::deque` or custom ring buffer for frame resources
- Evaluate `absl::InlinedVector` or similar for small vectors

#### 8.2 Object Pooling for Frame Resources
**Priority: Low**

**Files Affected:**
- `src/vk_engine.cpp` (frame resource allocation)
- Create `src/resources/pool.h`

**Action Items:**
- Implement object pool for frequently allocated frame resources
- Pool descriptor sets, buffers for push constants
- Reduce allocator pressure in hot rendering loop

---

## 9. Testing & Verification

### Current Issues
- No unit tests
- No integration tests
- Difficult to test due to tight coupling
- No mocking infrastructure

### Modernization Actions

#### 9.1 Add Testing Infrastructure
**Priority: High**

**Files Affected:**
- Create `tests/` directory
- Add test CMakeLists.txt

**Action Items:**
- Integrate Catch2 or GoogleTest
- Create test utilities for Vulkan mocking
- Write unit tests for pure functions (is_visible, utility functions)
- Create integration tests for subsystems
- Add CI pipeline for automated testing

#### 9.2 Design for Testability
**Priority: Medium**

**Files Affected:**
- Throughout codebase during refactoring

**Action Items:**
- Extract pure functions for easy testing
- Use dependency injection for testable code
- Create mock interfaces for Vulkan objects
- Separate business logic from Vulkan API calls

---

## 10. Documentation & Code Quality

### Current Issues
- Minimal inline documentation
- No API documentation
- Inconsistent naming conventions
- Missing const correctness in places

### Modernization Actions

#### 10.1 Documentation Standards
**Priority: Low**

**Action Items:**
- Add Doxygen-style comments for public APIs
- Document preconditions, postconditions, and invariants
- Create architecture documentation
- Add examples for common usage patterns

#### 10.2 Code Quality Tools
**Priority: Medium**

**Action Items:**
- Integrate clang-tidy with modern C++ checks
- Add clang-format configuration for consistent style
- Use static analyzers (PVS-Studio, Coverity, or clang static analyzer)
- Add warnings as errors for new code
- Consider sanitizers (ASan, UBSan, TSan) in CI

#### 10.3 Const Correctness
**Priority: Low**

**Files Affected:**
- Throughout codebase

**Action Items:**
- Add `const` to member functions that don't modify state
- Use `const` references for function parameters
- Mark member variables `const` where appropriate
- Use `std::as_const` to avoid accidental modifications

---

## Implementation Roadmap

### Phase 1: Foundation (Weeks 1-2)
1. Set up testing infrastructure
2. Add code quality tools (clang-tidy, clang-format)
3. Implement Result/Expected error handling
4. Create logging system

### Phase 2: Memory Management (Weeks 3-4)
1. Create RAII wrappers for Vulkan resources
2. Modernize AllocatedImage/AllocatedBuffer
3. Replace DeletionQueue with RAII
4. Fix smart pointer usage

### Phase 3: Architecture (Weeks 5-7)
1. Extract subsystems from VulkanEngine
2. Remove singleton pattern
3. Implement dependency injection
4. Create interface abstractions

### Phase 4: Type Safety & Modern Features (Weeks 8-9)
1. Implement strong types
2. Adopt ranges and views
3. Improve string handling
4. Add constexpr where applicable

### Phase 5: Polish & Optimization (Weeks 10-12)
1. Configuration system
2. Container optimizations
3. Complete documentation
4. Performance profiling and optimization

---

## Success Metrics

- **Code Quality**: All code passes clang-tidy checks with no warnings
- **Test Coverage**: >70% unit test coverage for non-Vulkan code
- **Build Time**: Build time reduced by at least 10% through better header organization
- **Error Handling**: Zero uses of abort() or exit() in non-critical paths
- **Memory Safety**: Zero memory leaks detected by sanitizers
- **Maintainability**: Average cyclomatic complexity <10 per function

---

## Risks & Mitigation

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Breaking existing functionality | High | Medium | Comprehensive testing before changes |
| Increased complexity | Medium | Low | Keep changes incremental, document decisions |
| Performance regression | High | Low | Profile before and after major changes |
| Team learning curve | Medium | Medium | Provide training materials and code reviews |
| Scope creep | Medium | High | Stick to prioritized plan, defer nice-to-haves |

---

## Conclusion

This modernization plan transforms the codebase into a more maintainable, type-safe, and idiomatic C++20 application. The phased approach allows for incremental improvements while maintaining stability. Priority should be given to high-impact items like error handling, memory management, and architectural improvements before moving to lower-priority polish items.

