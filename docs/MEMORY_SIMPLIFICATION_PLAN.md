# Memory Management & LOC Simplification Plan

## Objective
To drastically reduce Lines of Code (LOC) and eliminate convoluted manual memory management by replacing the manual `DeletionQueue` with a modern, automated RAII and Frame-based Garbage Collection system.

## The Core Problem
Currently, the codebase uses a manual `DeletionQueue` requiring lambdas to destroy Vulkan resources. This approach:
- Bloats the codebase with hundreds of lines of cleanup lambdas.
- Invites memory leaks if a lambda is forgotten.
- For dynamic assets (like loading new models), global deletion queues hold onto memory until the app closes, leaking VRAM.
- Temporary per-frame resources (like `GPUSceneData` and staging buffers) are allocated and destroyed every frame, which hurts performance and fragments the VMA heap.

## The Solution: RAII + Frame Garbage

### Phase 1: Infrastructure (The "Hidden" Queue)
We will build automated infrastructure so developers never manually call `vkDestroy` or push deletion lambdas again.

1. **`FrameGarbage` Struct:**
   Add a garbage bin to `FrameData` that holds raw Vulkan handles, not lambdas.
   ```cpp
   struct FrameGarbage {
       std::vector<VkBuffer> buffers;
       std::vector<VmaAllocation> allocations;
       std::vector<VkImage> images;
       std::vector<VkImageView> imageViews;
       
       void flush(VkDevice device, VmaAllocator allocator); // Executes actual destruction
   };
   ```

2. **`VulkanEngine::defer_destroy()`:**
   A central method that accepts dying Vulkan handles and pushes them into the *current* frame's garbage bin: `_frames[_frameNumber % FRAME_OVERLAP].garbage`.

3. **RAII Resource Wrappers:**
   Update `AllocatedBuffer`, `AllocatedImage`, etc., with custom C++ destructors. When these objects go out of C++ scope, their destructors automatically call `engine->defer_destroy(...)`.

### Phase 2: Resource Lifecycle Strategies

#### 1. Long-Lived Resources (App Lifetime)
* **Examples:** Swapchain, pipeline layouts, global descriptor pools, default textures.
* **Strategy:** Store them as simple value types inside the `VulkanEngine` class. 
* **Cleanup:** When the app closes, `VulkanEngine::cleanup()` simply calls `vkDeviceWaitIdle(device)`. As the `VulkanEngine` class is destroyed, C++ automatically calls the destructors for these resources. Because the device is idle, they are safely destroyed immediately. Zero LOC spent on manual cleanup.

#### 2. Temporary Resources (Per-Frame Uploads & Scene Data)
* **Examples:** `GPUSceneData` uniform buffers, Mesh Staging Buffers.
* **Strategy:** Stop allocating and freeing these dynamically. Pre-allocate them as persistent mapped buffers inside `FrameData` during startup.
* **Cleanup:** None. During the render loop, simply `memcpy` into the existing mapped pointers. This entirely removes VMA allocation/deletion overhead from the hot render loop.

#### 3. Dynamic Resources (Loaded Models, Textures, Meshes)
* **Examples:** Textures and vertex buffers belonging to a specific level or loaded glTF model.
* **Strategy:** Store them in standard C++ containers (e.g., `std::vector<AllocatedBuffer>`). 
* **Cleanup:** When a new model is loaded, overwriting or clearing the old vector triggers the RAII destructors. The destructors pass the old Vulkan handles to the *current frame's* garbage bin. C++ scoping rules handle the entire memory management lifecycle seamlessly.

### Phase 3: The Synchronization Guarantee (Why this is safe)
When a C++ resource is destroyed, how do we guarantee the GPU isn't still using it?

We rely on the **FIFO (First-In, First-Out)** nature of Vulkan Queues and the Ring Buffer structure of `FrameData`.

**The Timeline:**
Assuming a `FRAME_OVERLAP` of 2 (Frame 0 and Frame 1):
1. **Time = 0 (CPU processing Frame 1):** CPU submits `OldTexture` to the GPU.
2. **Time = 1 (CPU processing Frame 0):** The level changes. `OldTexture` is destroyed in C++. Its destructor pushes the handle to **Frame 0's Garbage Bin**. CPU submits Frame 0.
3. **Time = 2 (CPU processing Frame 1):** CPU waits on Frame 1's Fence. Clears Frame 1's Garbage.
4. **Time = 3 (CPU processing Frame 0):** CPU waits on Frame 0's Fence. **Frame 0's Garbage is flushed.**

Because Vulkan command buffers execute in order, when the GPU signals Frame 0's fence (Time=3), it guarantees that Frame 0 (and the preceding Frame 1) have fully completed execution. Therefore, by the time Frame 0's garbage bin is flushed, no active GPU commands are using `OldTexture`.

### Success Criteria
- [ ] `DeletionQueue` struct is completely removed from the project.
- [ ] Render loop no longer makes any calls to `vmaCreateBuffer` or `vmaDestroyBuffer`.
- [ ] Resource cleanup is implicit and driven purely by C++ scope and RAII.