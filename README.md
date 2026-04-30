Render and scene stuff for the wozzits experimental engine

# Render–Scene Architecture

This repository defines a self-contained **CPU-side rendering compiler architecture**.  
It is split into two tightly defined domains:

- **Scene**: hierarchical authoring and spatial structure
- **Render**: flattened execution pipeline for drawing

The system is designed around a strict multi-stage compilation flow that converts a scene graph into a fully ordered, render-ready frame.

---

# High-Level Pipeline
Scene (hierarchical graph)
↓
Scene Compilation (flattening + transform resolution)
↓
RenderIR (render-facing intermediate representation)
↓
FrameBuilder (culling, sorting, binning)
↓
RenderFrame (execution-ready draw structure)
↓
Backend submission (GPU)


Each stage has a strict responsibility and must not depend on later stages.

---

# 1. Scene Layer

The Scene layer defines **what exists in the world**.
The Scene layer defines a hierarchical transform structure of objects in space.
It is a pure data layer. It has no rendering knowledge.

## Responsibilities

- Hierarchical transform graph (tree or forest, not a general DAG)
- Parent-child spatial relationships
- Local transform storage (authoritative input)
- Cached world transforms (derived data)
- Explicit update orchestration (CPU-side)

## Critical structural invariant

The scene graph is a forest of trees (not a general DAG)

Each node:
- has at most one parent
- may have multiple children
- contains no cycles

## Key Concept: TransformNode

# TransformNode (formal contract)

Each node stores:
- Input state (authoritative)
- local → user-defined transform
- Hierarchy state
- parent → single parent index or INVALID_NODE
- first_child
- next_sibling
- Derived state (cached)
- world → computed transform (NOT authoritative)
- Update tracking
- last_updated_frame → used ONLY for update scheduling correctness

The scene is **not render-aware**. It contains no knowledge of:
- draw calls
- GPU state
- passes
- sorting
- culling

It is purely a spatial and logical representation.

---

# 2. Scene Compilation Layer

This layer is the **critical boundary between Scene and Render**.

## Purpose

Convert hierarchical scene data into a flat, renderable form.

## Responsibilities

- Resolve transform hierarchy → world space
- Expand objects into render primitives
- Flatten scene graph
- Normalize different object types (mesh, splat, etc.)

## Output

The result is a **CompiledScene**, which is the only structure passed into the render system.

---

# 3. CompiledScene (Boundary Contract)

CompiledScene is the **single shared contract** between Scene and Render.

It contains only flat, render-agnostic data:

- Compiled primitives
- Materials
- Lights
- Bounds
- View data

## Key Rule

> No hierarchy, no transforms, no scene graph exists beyond this point.

All spatial relationships are already resolved.

---

# 4. Render Layer

The Render layer consumes only CompiledScene.

It is responsible for transforming static data into an optimized execution plan.

## 4.1 RenderIR Construction

Transforms:

CompiledScene → RenderIR

Responsibilities:
- Organize primitives for rendering
- Assign pass participation
- Generate draw references
- Prepare sort keys

RenderIR is still CPU-side and mutable during pipeline execution.

---

## 4.2 FrameBuilder

Transforms:

RenderIR → RenderFrame

Pipeline stages:
- Culling
- Classification
- Sorting
- Binning into passes

This stage produces the final ordered execution structure.

---

## 4.3 RenderFrame

RenderFrame is the **final CPU-side representation before GPU submission**.

It contains:
- Ordered draw lists
- Render passes
- Visible primitive references

It is treated as immutable after construction.

---

# 5. Backend Layer

The backend consumes RenderFrame and is responsible for:
- GPU command encoding
- draw submission
- API-specific execution

This layer is intentionally isolated from Scene and RenderIR logic.

---

# Design Principles

## 1. Strict Stage Isolation

Each stage has a single responsibility:
Scene → Compile → RenderIR → Frame → Backend

No stage may depend on a later stage.

---

## 2. Single Boundary Contract

The only shared structure between Scene and Render is:
CompiledScene

This ensures a clean separation between:
- hierarchical representation
- execution representation

---

## 3. Meshes and Splats are Unified

At compile time:
- Meshes and splats are converted into a unified primitive form
- Differences are resolved during compilation
- Render system treats all primitives uniformly

No special-case logic exists in FrameBuilder.

---

## 4. Render is a Compiler

The system is not immediate-mode.

It behaves as a deterministic compiler pipeline:

Scene → compiled representation → execution plan → GPU submission

---

## 5. Deterministic Data Flow

Data only moves forward through stages.

No stage:
- queries Scene after compilation
- reconstructs hierarchy
- reinterprets object types

Everything is flattened before rendering begins.

---

# Summary

This architecture defines a **render compiler system** with two core domains:

- Scene: hierarchical description of world state
- Render: flattened, optimized execution pipeline

The system prioritizes:
- strict separation of concerns
- predictable data flow
- unified primitive representation
- compile-time resolution of scene complexity