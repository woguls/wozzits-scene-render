# wozzits render-scene

A CPU-side rendering compiler architecture for experimental game engines.
The library converts a hierarchical scene graph into a fully ordered,
render-ready frame through a series of well-defined, independently testable stages.

---

## Architecture

Data moves forward through five stages. No stage depends on a later one.

```
PolytreeStorage<TransformNode>     scene graph — spatial hierarchy
        ↓  compile() / update_view()
CompiledScene                      flat boundary — no hierarchy beyond this point
        ↓  build_render_ir()
RenderIR                           sorted draw references — backend agnostic
        ↓  build_frame()
RenderFrame                        ordered draw commands — ready for submission
        ↓  submit()
Backend                            GPU command encoding
```

---

## Namespaces

| Namespace | Contents |
|---|---|
| `wz::core::graph` | DAG, Polytree, traversal, graph algorithms |
| `wz::core::algo::pipeline` | map / filter / reduce, sink protocol |
| `wz::scene` | TransformNode, scene graph, compiler |
| `wz::render` | RenderIR, RenderFrame, FrameBuilder |
| `wz::render::backend` | Stub backend — replace with your GPU backend |

---

## Render pipelines

Each pipeline defines its own sort rules, data shape, and execution model.
Classification happens at compile time — the render layer never inspects object types.

| Pipeline | Sort | Typical use |
|---|---|---|
| `OpaqueGeometry` | by material, ascending | static meshes, terrain |
| `Splat` | depth, back-to-front | Gaussian splats |
| `TransparentGeometry` | depth, back-to-front | glass, water, foliage |
| `Particle` | depth, back-to-front | fire, smoke, debris |

Submission order is fixed: **opaque → splat → transparent → particle**.

---

## Quick start — a static opaque scene

This example builds a scene with three opaque mesh objects, compiles it,
sorts it, and submits it through the stub backend.

### 1. Build the scene graph

```cpp
#include <scene/scene_graph.h>

using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// Helper — column-major Mat4 translation along Z
Mat4 translation_z(float z)
{
    Mat4 m = mat4_identity();
    m.m[14] = z;
    return m;
}

SceneStorage build_test_scene()
{
    SceneBuilder b;

    // Root node — spatial anchor, not renderable
    TransformNode root{};
    root.local = mat4_identity();
    auto root_h = add_node(b, root);

    // Three renderable children at different depths
    for (int i = 0; i < 3; ++i) {
        TransformNode node{};
        node.local = translation_z(static_cast<float>(i + 1) * 2.f);
        node.flags = TransformNodeFlag::RenderDomain;
        auto h = add_node(b, node);
        add_edge(b, root_h, h);
    }

    auto result = build(std::move(b));
    assert(result.has_value()); // fails only if a cycle was introduced
    return std::move(*result);
}
```

### 2. Describe renderables

`RenderableDescriptor` maps each scene node to asset handles and a pipeline.
It lives parallel to the node array, indexed by `NodeHandle`.

```cpp
std::vector<RenderableDescriptor> build_descriptors()
{
    // One entry per node: root + 3 children = 4 entries
    std::vector<RenderableDescriptor> descs(4);

    // Root — not renderable
    descs[0] = { RenderPipeline::None };

    // Children — opaque geometry, each with a distinct mesh and material
    descs[1] = { RenderPipeline::OpaqueGeometry, /*mesh=*/0u, /*material=*/0u,
                 /*local_bounds=*/{ Vec3{-0.5f,-0.5f,-0.5f},
                                    Vec3{ 0.5f, 0.5f, 0.5f} } };
    descs[2] = { RenderPipeline::OpaqueGeometry, 1u, 1u,
                 { Vec3{-0.5f,-0.5f,-0.5f}, Vec3{0.5f,0.5f,0.5f} } };
    descs[3] = { RenderPipeline::OpaqueGeometry, 2u, 2u,
                 { Vec3{-0.5f,-0.5f,-0.5f}, Vec3{0.5f,0.5f,0.5f} } };

    return descs;
}
```

### 3. Propagate transforms and compile

```cpp
#include <scene/compile/scene_compiler.h>

ViewData make_camera()
{
    ViewData v{};
    v.view            = mat4_identity();
    v.view.m[14]      = -5.f;   // camera 5 units back along Z
    v.projection      = mat4_identity();
    v.view_projection = mat4_identity();
    v.camera_position = Vec3{ 0.f, 0.f, 5.f };
    return v;
}

// After building the scene, propagate world transforms.
// For a fully static scene this is called once after build().
// For scenes with animated nodes, call update_static() or update_animated()
// each frame instead.

auto scene   = build_test_scene();
auto descs   = build_descriptors();
auto view    = make_camera();

propagate_all(scene.polytree);

auto compiled = compile(scene.polytree, descs, /*lights=*/{}, view);
```

`compiled.scene` is now a flat `CompiledScene` containing one
`OpaqueGeometryPrimitive` per visible, renderable node.
No scene graph exists beyond this point.

### 4. Build RenderIR and RenderFrame

```cpp
#include <render/ir/render_ir.h>
#include <render/frame/render_frame.h>

auto ir    = build_render_ir(compiled.scene);
auto frame = build_frame(ir, compiled.scene);
```

`frame.frame.commands` is a flat span of `DrawCommand` in submission order,
sorted by material within the opaque pipeline.

### 5. Submit to your backend

```cpp
// Stub backend — replace with your Vulkan / Metal / DX12 implementation.
#include <render/backend/stub_backend.h>

auto result = wz::render::backend::submit(frame.frame);

// result.log contains one human-readable entry per draw command.
// result.opaque_count() == 3
```

To implement a real backend, iterate `frame.frame.commands` linearly:

```cpp
for (const auto& cmd : frame.frame.commands) {
    switch (cmd.stage) {
        case PipelineStage::OpaqueGeometry:
            // cmd.mesh     — index into your mesh asset table
            // cmd.material — index into your material asset table
            // cmd.world    — Mat4, column-major, float[16]
            // encode your draw call here
            break;

        case PipelineStage::Splat:
            // cmd.splat_position — Vec3 world space center
            // cmd.splat_scale    — Vec3 per-axis gaussian scale
            // cmd.splat_rotation — Vec4 quaternion
            // cmd.splat_color    — Vec3 RGB
            // cmd.splat_opacity  — float
            // cmd.splat_depth    — float, view space, for your alpha sort
            break;

        case PipelineStage::TransparentGeometry:
        case PipelineStage::Particle:
            // same fields as OpaqueGeometry
            break;
    }
}
```

---

## Handling a moving camera

When the camera moves but the scene does not change structurally,
call `update_view()` and `update_render_ir()` instead of recompiling:

```cpp
// Every frame the camera moves:
ViewData new_view = get_current_camera();

update_view(compiled, new_view);       // recomputes depth values only
update_render_ir(ir);                  // re-sorts view-dependent pipelines

auto frame  = build_frame(ir, compiled.scene);
auto result = backend::submit(frame.frame);
```

`compile()` is only called again when the scene structure changes —
nodes added, removed, or reparented.

---

## Key invariants

**Scene layer**
- Every node has at most one parent (`Polytree` enforces this at build time).
- `TransformNode::world` is derived data — never set it directly, use `set_local()`.
- The scene layer contains no rendering knowledge.

**CompiledScene boundary**
- No scene graph handles, no hierarchy, no `NodeHandle` exists beyond this point.
- All spatial relationships are fully resolved into world-space transforms.
- `CompiledScene` is immutable after `compile()` except for depth values
  updated by `update_view()`.

**RenderIR**
- Draw references are indices into `CompiledScene` spans — no data is copied during sort.
- Opaque sort is stable across frames (material-based). Depth sorts change with the camera.

**RenderFrame**
- Submission order is fixed: opaque → splat → transparent → particle.
- `DrawCommand` is self-contained — the backend never touches `CompiledScene` or `RenderIR`.

---

## Adding a Gaussian Splat node

```cpp
TransformNode splat_node{};
splat_node.local = translation_z(3.f);
splat_node.flags = TransformNodeFlag::RenderDomain;
auto splat_h = add_node(b, splat_node);
add_edge(b, root_h, splat_h);

// In your descriptor table:
descs[splat_h] = {
    RenderPipeline::Splat,
    INVALID_MESH,
    INVALID_MATERIAL,
    {},  // no mesh bounds
    SplatDescriptor {
        .scale    = Vec3{ 0.1f, 0.2f, 0.1f },
        .rotation = Vec4{ 0.f, 0.f, 0.f, 1.f },
        .color    = Vec3{ 1.f, 0.8f, 0.6f },
        .opacity  = 0.9f,
    }
};
```

Splat depth is recomputed automatically by `update_view()` every frame.
Splats are always submitted after opaque geometry and before transparent geometry.