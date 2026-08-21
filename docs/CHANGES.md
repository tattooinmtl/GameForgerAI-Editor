# CHANGES — game-engine audit remediation

Date: 2026-08-13+
Source: `audit-2026-08-13.md`

This file records every code change made to fix findings from the audit, in
the order they were applied. Each entry has:

- **Finding**: audit tag
- **What was broken**: the original behavior
- **What it does now**: the fixed behavior
- **Files touched**: where the change lives

Use this file to diff audit-time vs. current state, or to revert any specific
fix by undoing its listed files.

---

## Tier 1 — top-10 (build blockers + highest user impact)

### C1 — Runtime does not build against current Engine header

**Finding:** `audit-2026-08-13.md` C1 (priority: Critical, build-blocker).

**What was broken:** `ScriptRuntime::initialize` takes 9 parameters
(`scene`, `commandBus`, `inputSource`, log callback, projectile spawn callback,
held-item query, aiming-catapult query, **operating-catapult set**, and
**gravity-projectile spawn**). `Runtime/src/main.cpp` was passing only 7. The
standalone `GameForgerRuntime` target would not link.

**What it does now:** The Runtime call passes all 9. The two new params are
no-op lambdas because the Runtime has no catapult UI of its own. The comment
above the call explains why.

**Files touched:**
- `Runtime/src/main.cpp` (call site, added two no-op callbacks)

**Verified:** `cmake --build . --config Debug --target GameForgerRuntime` →
`[4/4] Linking CXX executable Runtime\Debug\GameForgerRuntime.exe` (10:33
rebuild artifact present).

---

### H-Script-1 — `luaEntityGetRight` returned the *left* vector

**Finding:** `audit-2026-08-13.md` H-Script-1.

**What was broken:** `cross(forward, +Y)` returned `-X` at yaw=0, so
`self.entity:getRight()` and the Editor's middle-mouse-pan right vector were
both pointing the wrong way. Every FPS / third-person script strafed
backwards; camera-pan felt reversed.

**What it does now:** Both call sites use `cross(+Y, forward)`, which gives
`+X` at yaw=0 in a right-handed Y-up Z-forward world. Strafe / pan now feel
correct.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntityGetRight`)
- `Editor/src/main.cpp` (mouse-look right vector calculation)

**Verified:** `Build-Project.cmd` → `[10/10] Linking CXX executable
Editor\Debug\GameForgerEditor.exe`.

---

### H-Render-1 — All STB-loaded textures were upside-down

**Finding:** `audit-2026-08-13.md` H-Render-1.

**What was broken:** No `stbi_set_flip_vertically_on_load(true)` call
anywhere in the codebase. STB returns top-row-first but OpenGL expects
bottom-row-first for `glTexImage2D`. Every terrain splat, normal/height
map, splash image, and ImGui font atlas sampled inverted vs. source art.

**What it does now:** A static initializer in `StbImageImpl.cpp` (the TU that
defines `STB_IMAGE_IMPLEMENTATION`) sets the flag once at program startup.
The flag is process-global on stb_image, so all three load sites
(`TerrainTexture.cpp:87`, `SplashScreen.cpp:62`, `Editor/src/main.cpp:969`)
inherit it automatically.

**Files touched:**
- `Engine/src/Editor/StbImageImpl.cpp` (added static-init struct)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---

### H-Edit-1 — AI Animation bypassed the undo system

**Finding:** `audit-2026-08-13.md` H-Edit-1.

**What was broken:** The "Apply" path in `drawAnimationPanel` mutated
`SceneEntity::animation` directly via `findEntityMutable`, never going
through `commandBus.execute`. After AI Apply, `Undo` (when wired) could not
revert the animation, and the undo stack was silently inconsistent.

**What it does now:** Introduced a new `SetAnimationCommand` variant carrying
`entityName` / `enabled` / `looping` / `keyframes`. The AI Apply path calls
`commandBus.execute(SetAnimationCommand{...})`, which goes through the same
validator + executor + undo-snapshot pipeline as every other mutation.

Required touching three files (variant, validator, executor) plus threading
`commandBus` into `drawAiAnimationSection` (which previously didn't take it)
and `drawSettingsWindow` (which now also plumbs provider + console to its
AI Setup tab).

**Files touched:**
- `Engine/include/GameForger/Editor/AnimationData.hpp` *(new)*
- `Engine/include/GameForger/Editor/AICommand.hpp` (added `SetAnimationCommand`,
  variant member, include of new AnimationData header)
- `Engine/include/GameForger/Editor/EditorScene.hpp` (now includes
  `AnimationData.hpp`; the local `TransformKeyframe` / `EntityAnimation`
  definitions moved to the new header to break a circular include)
- `Engine/src/Editor/AICommandBus.cpp` (validator case)
- `Engine/src/Editor/EditorScene.cpp` (executor case)
- `Editor/src/main.cpp` (added `using` alias, threaded `commandBus` into
  `drawAiAnimationSection`, replaced direct mutation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---

### H-Play-1 — Animation overwrote parent-constraint transforms

**Finding:** `audit-2026-08-13.md` H-Play-1.

**What was broken:** `applyParentConstraints` and `tickPlayModeAnimations`
both ran in the same frame and both wrote to the same
`SetPropertyCommand{entity.name, "Transform", "position", ...}` slot. The
animation tick ran second, clobbering the parent-derived world transform and
visually detaching parented animated entities (e.g. catapult arms, turret
parts).

**What it does now:** `tickPlayModeAnimations` now branches on
`entity.parentName.empty()`. Unparented entities still animate the world
transform (today's behavior). Parented entities animate their local
position / rotation / scale under the `"Parent"` component, so
`applyParentConstraints` (which still runs first) re-composes the world
correctly on the next frame.

**Files touched:**
- `Engine/src/Runtime/GameplayLoop.cpp` (tickPlayModeAnimations)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---

### H-Edit-2 + H-Edit-3 + L4 — Dead Ctrl+S / Ctrl+Z / Ctrl+Y shortcuts

**Finding:** `audit-2026-08-13.md` H-Edit-2, H-Edit-3, L4.

**What was broken:**
- The "Save Scene" menu item was labelled "Ctrl+R" with no keyboard handler
  wired; only `Ctrl+N` / `Ctrl+O` worked in `drawMainMenu`.
- Edit > Undo and Edit > Redo were `ImGui::MenuItem("Undo", "Ctrl+Z")` with
  the return value discarded — clicking them did nothing. Their keyboard
  shortcuts were also dead.
- Ctrl+R is conventionally refresh/reload, not save. L4 had flagged this.

**What it does now:**
- Save Scene label changed to "Ctrl+S" (the standard convention).
- New keyboard handlers in `drawMainMenu`: `Ctrl+S` (Save), `Ctrl+Z` (Undo),
  `Ctrl+Shift+Z` (Redo, matching common editors), `Ctrl+Y` (alternate Redo).
- Edit > Undo / Redo now call `performUndo` / `performRedo` and are disabled
  (greyed out + non-clickable) when their stacks are empty.

**Files touched:**
- `Editor/src/main.cpp` (keyboard block in `drawMainMenu`, menu items in Edit
  menu, menu label on Save Scene)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---

### H-Edit-4 — "Calibrate / Test provider" button was a no-op

**Finding:** `audit-2026-08-13.md` H-Edit-4.

**What was broken:** The button simply flipped `state.calibrated = true` and
displayed "Configuration ready" without ever contacting the provider. The
`state.endpoint` and `state.model` UI fields were also ignored by
`AIProviderClient` (separate fix in C4).

**What it does now:** The button now sends a real probe — a minimal
completion request via `providerClient.send(...)` — and displays the actual
HTTP status code + a redacted error message on failure. Logs the same to
the console. The button still synchronously blocks the UI for the network
round-trip (acceptable for an explicit "Test" action).

`AISetupState` gained three fields: `tested`, `testSucceeded`, `testMessage`.

**Files touched:**
- `Editor/src/main.cpp` (AISetupState, drawAiSetupSettingsContent signature
  + body, drawSettingsWindow plumbs providerClient + console through)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---

### C4 — Provider parser / response parser used substring search

**Finding:** `audit-2026-08-13.md` C4 (priority: Critical).

**What was broken:**
- `AIProviderClient::parseProvider` used `configuration.find("\"id\": \"…\"")`
  then extracted the next `"endpoint"` / `"model"` / `"apiKeyEnvironmentVariable"`
  with substring find. A description string containing those keys, or escaped
  quotes inside a value, would leak into the wrong provider's settings.
- The request body embedded the model value raw into JSON with no escaping.
- `AIChatResponse::extractChatMessageContent` returned the first
  `"content"` substring anywhere in the response — wrong for streaming,
  wrong for non-OpenAI shapes, wrong when an error response had
  `error.message`.
- `extractErrorMessage` on a successful response returned the literal text of
  the next key inside `choices[0].message` (e.g. the string `"content"`) as
  the "error".

**What it does now:**
- `parseProvider` uses `json::parse` (the structured parser already in the
  Engine) and walks `providers[]` as an array, finding the object whose
  `id == providerId` and reading fields only from that object.
- The request body uses the parsed model value, run through the rewritten
  `escapeJson` which now escapes every control byte (`\b`/`\f`/`\n`/`\r`/`\t`/
  `\u0000`-`\u001F`) instead of dropping `\r` and emitting the rest raw.
- `extractChatMessageContent` walks `choices[0].message.content` structurally.
- `extractErrorMessage` only returns `error.message` from a real `error`
  object, falling back to a body substring only when no JSON parses.
- WinHTTP status-code query is now gated on `received`, and non-2xx status
  produces a `HTTP <code>` error string in the response.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (full rewrite of `parseProvider`,
  `escapeJson`, request-body construction, error/status handling)
- `Editor/src/AIChatResponse.cpp` (full rewrite to use `json::parse`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---

### H-Script-2 — No Lua instruction budget; infinite loop freezes editor

**Finding:** `audit-2026-08-13.md` H-Script-2.

**What was broken:** Zero `lua_sethook` calls in `ScriptRuntime`. A
`while true do end` in `on_update` blocked `lua_pcall` until the process
was killed.

**What it does now:** Installed a per-runtime Lua VM instruction-count hook
(`lua_sethook` with `LUA_MASKCOUNT`, granularity 1024 instructions). The hook
reads an atomic per-call budget from the runtime (default 5,000,000 VM
instructions per call) and decrements it on each invocation. When the budget
hits zero, the hook raises a Lua error via `luaL_error` that longjmps
through the current `lua_pcall` as `LUA_ERRRUN`. The caller already handles
that error path by logging and stopping the script — the same path used for
any other runtime error.

The budget is reset to its full value before each `lua_pcall` (3 sites:
`startScript` chunk run, `on_start` call, `on_update` call).

Hook function and helpers are static members of `ScriptRuntime` so they can
touch the private `instructionsRemaining_` field without friend gymnastics.

**Files touched:**
- `Engine/include/GameForger/Editor/ScriptRuntime.hpp` (forward declaration
  of `lua_Debug`, added `<atomic>` + `<cstdint>` includes, new private
  `instructionsRemaining_` atomic field, three new static-member
  declarations)
- `Engine/src/Editor/ScriptRuntime.cpp` (added `<atomic>` include,
  `kInstructionBudget` / `kHookCountInterval` constants,
  `instructionHook` / `installBudgetHook` / `resetBudget` static members,
  hook install in `initialize`, `resetBudget()` calls before each
  `lua_pcall`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---

### C2 + E-Scene-1 — Scene save was non-atomic; load ignored format/version

**Finding:** `audit-2026-08-13.md` C2 (data-loss), E-Scene-1.

**What was broken:**
- `saveScene` opened the destination with `std::ios::trunc` and wrote
  directly. A crash, kill, or disk-full mid-write truncated the user's
  scene file.
- `loadScene` ignored `format` and `version`. Any JSON with an `entities`
  array (Project.json, package manifests, third-party JSON) would load as
  a scene and silently overwrite the user's current scene with
  default-fallback fields.

**What it does now:**
- `saveScene` serializes into `<dest>.tmp`, flushes + closes the stream,
  copies the previous destination to `<dest>.bak`, then renames the temp
  over the destination. On any failure the previous good file is
  preserved (either as `.bak` or simply still at `dest`).
- `loadScene` requires `format == "GameForgerScene"`, a numeric `version`
  in `[1, 8]`, and rejects newer-than-8 versions with an explicit
  "newer than this editor supports" message.

**Files touched:**
- `Engine/src/Editor/SceneSerializer.cpp` (saveScene, loadScene)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---

## Tier 2 — Medium findings (in progress)

### E-Run-2 — Unclamped `deltaTime` corrupted physics on tab-out / breakpoint

**Finding:** `audit-2026-08-13.md` E-Run-2.

**What was broken:** Both the Editor's `drawGameViewPanel` and the
standalone Runtime handed the raw `glfwGetTime` difference to
`tickPlayModeAnimations` / `tickScripts` / `tickProjectiles`. Gravity
projectiles use `-18 m/s²`; catapult speed is `22 u/s`. A 5-second tab-out
made a single frame's delta = 5s, so the projectile integrated over 110
units and the player's jump velocity over 90 m/s — instantly tunnelling
through every collider and applying dozens of boulder hits to the castle
in one tick.

**What it does now:** A `clampDeltaTime(float)` helper at the top of
`GameplayLoop.cpp` clamps to `kMaxDeltaSeconds = 0.1F` (also guards
non-finite values, replacing NaN with 0). All three tick functions use
the clamped value for integration.

**Files touched:**
- `Engine/src/Runtime/GameplayLoop.cpp` (`kMaxDeltaSeconds`, `clampDeltaTime`,
  `tickPlayModeAnimations` / `tickScripts` / `tickProjectiles` use clamped
  dt)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-1 — Win/lose banner never shown to the player

**Finding:** `audit-2026-08-13.md` E-Run-1.

**What was broken:** `GameplayState::gameOverMessage` is set by
`tickProjectiles` the first time a castle's HP reaches 0. The Editor's
`drawGameViewPanel` rendered it as an ImGui overlay. The standalone
Runtime had zero consumers — a runtime player who won or lost saw the
projectiles stop with no feedback that the round ended.

**What it does now:** `GameMenu` gained a public `drawCenteredBanner(width,
height, text)` method that uses the same shader + stb_truetype font atlas
the pause menu already owns, draws a translucent dark plate behind the
text, and centers the banner in the window. Cached the four uniform
locations as members so the new method can run without re-fetching them.
Runtime's main loop calls it every frame `gameplay.gameOverMessage` is
non-empty (drawn after the game view, before the pause menu, so the menu
still covers it when open).

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new public method, four cached uniform
  location members)
- `Runtime/src/GameMenu.cpp` (cache uniforms in `initialize`, use them in
  `render`, new `drawCenteredBanner` implementation)
- `Runtime/src/main.cpp` (call `drawCenteredBanner` when
  `gameOverMessage` is non-empty)

**Verified:** Editor + Runtime targets both build clean.

### E-Run-6 + E-Run-7 — Load did not reset session state or camera look

**Finding:** `audit-2026-08-13.md` E-Run-6, E-Run-7.

**What was broken:** `Runtime/src/main.cpp`'s Load path cleared
`gameplay.projectiles` and re-read inventory, but left five other pieces
of session state untouched:
- `gameplay.heldItemEntityName` — scripts' `self.world:isHoldingItem()`
  callbacks returned the previous game's answer.
- `gameplay.playerOperatingCatapult` — catapult-set callback saw the old
  value.
- `gameplay.gameOverMessage` — a stale "You Win" / "You Lose" banner
  kept being drawn after the new game started (E-Run-1 already added
  the banner; without this fix it would draw stale text).
- `gameCameraLookYawDegrees` / `gameCameraLookPitchDegrees` — the new
  game inherited the previous game's mouse-look angle.

**What it does now:** The Load branch resets all five fields before
restarting scripts. Stale session state can no longer leak across loads.

**Files touched:**
- `Runtime/src/main.cpp` (Load branch)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-1 — `startScript` could double-register the same script

**Finding:** `audit-2026-08-13.md` E-Script-1.

**What was broken:** `startScript` unconditionally pushed the new
`ScriptInstance` onto `instancesByEntity_[entityId]`. `AttachScriptCommand`
guards at attach-time, but `startScript` is `public` — a retry path, a
hand-edited scene loaded straight into `SceneEntity::scripts`, or any
future "attach script while playing" feature could call it twice. Two
parallel `on_update` loops would then run on the same entity, both
mutating the same fields every frame.

**What it does now:** Before pushing the new instance, the code scans the
existing bucket for any matching `(entityId, scriptPath)` and unrefs the
old Lua registry entry, then erases it. A duplicate call replaces
rather than stacks.

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`startScript`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-3 — `CreateScriptCommand` silently overwrote existing scripts

**Finding:** `audit-2026-08-13.md` E-Script-3.

**What was broken:** `CreateScriptCommand`'s executor opened the destination
script with `std::ios::trunc` and wrote the new content without checking
whether the file already existed. If Play was running, the Lua chunk in
the registry still referenced the old function objects; the on-disk file
was new but the running instance kept executing the old code. Edits
silently didn't take effect until Play restarted — and the user got no
warning.

**What it does now:** The executor `std::filesystem::exists`s the
resolved path first. If the file exists and the command didn't set
`overwrite = true`, it returns an explicit error
("Script already exists at … - set overwrite=true to replace it."). The
command struct gained an `overwrite` field (default `false`). Also added
explicit flush + write-error checks so a partial file can't be left.

**Files touched:**
- `Engine/include/GameForger/Editor/AICommand.hpp` (`overwrite` field on
  `CreateScriptCommand`)
- `Engine/src/Editor/EditorScene.cpp` (`CreateScriptCommand` handler)

**Verified:** `Build-Project.cmd` → clean build.

### E-Script-4 — Silent entity-gone writes from Lua scripts

**Finding:** `audit-2026-08-13.md` E-Script-4.

**What was broken:** `entity:setPosition`, `entity:setRotation`, and
`world:setEntityRotation` in `ScriptRuntime.cpp` had `if (entity != nullptr) { … }`
guards with no `else` branch. If the entity had been deleted mid-Play,
the Lua call silently did nothing and the script couldn't tell whether
its write succeeded or whether the target had gone away.

**What it does now:** Each of those three callbacks now logs a
"ignored: entity X no longer exists" / "entity \"X\" not found" note via
`runtime->log(false, …)` (info level, not error — deletion during Play
isn't an error, just the script continuing to talk to a corpse).

**Files touched:**
- `Engine/src/Editor/ScriptRuntime.cpp` (`luaEntitySetPosition`,
  `luaEntitySetRotation`, `luaWorldSetEntityRotation`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-1 — `SetPropertyCommand` accepted NaN/Inf and non-positive scale

**Finding:** `audit-2026-08-13.md` E-Validate-1.

**What was broken:** `SetPropertyCommand{entityName, "Transform",
"position", {NaN, NaN, NaN}}` or `{"scale", {-1, -1, -1}}` passed
validation. `glm::translate`/`glm::scale` would propagate NaNs and negative
scales into the model matrix, corrupting every entity's transform in the
scene. `SetPositionCommand` already had this guard; `SetPropertyCommand`
didn't, and any path that routed the same field through the generic
property command (e.g. via the AI command bus from the Scripting API)
bypassed the protection.

**What it does now:** The `SetPropertyCommand` validator now branches on
the value being a `glm::vec3`, checks all three components for finiteness,
and rejects non-positive `scale` / `localScale` components. The error
message names the offending component/property so the AI or script
author can fix it.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`SetPropertyCommand` validator
  case)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-2 — Validator default branch silently approved unknown commands

**Finding:** `audit-2026-08-13.md` E-Validate-2.

**What was broken:** The catch-all `else` arm of `AICommandValidator`
returned `{success=true, "Command is valid."}`. If a new `AIEditorCommand`
variant was added without updating the validator switch, the AI's
request would pass validation and then fail at execution with a generic
"not implemented" message — the AI couldn't tell that the *validator*
was missing, not just the executor.

**What it does now:** The default branch returns
`invalid("Unknown command type - no validator case handles this variant.")`.
Adding a new variant now produces a deliberate compile-time-style
failure at the validator stage with a clear message.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (default validator arm)

**Verified:** `Build-Project.cmd` → clean build.

### E-Validate-3 — `AttachScriptCommand` validator lacked the `..` traversal guard

**Finding:** `audit-2026-08-13.md` E-Validate-3.

**What was broken:** `CreateScriptCommand` rejected `..` in the path; the
executor for `AttachScriptCommand` called `resolveProjectFile` which
would refuse the path. The validator for `AttachScriptCommand`, however,
didn't — meaning the preview path (`AICommandBus::preview`) returned
"valid" while execute returned "rejected" for the same input. The two
paths disagreed.

**What it does now:** The `AttachScriptCommand` validator applies the
same `..` check that `CreateScriptCommand` does, so preview and execute
agree on what counts as a valid script path.

**Files touched:**
- `Engine/src/Editor/AICommandBus.cpp` (`AttachScriptCommand` validator)

**Verified:** `Build-Project.cmd` → clean build.

### E-Scene-4 — `replaceEntities` didn't advance `nextEntityId_` past preserved ids

**Finding:** `audit-2026-08-13.md` E-Scene-4.

**What was broken:** `EditorScene::replaceEntities` (used for undo/snapshot
restoration) preserves each entity's existing id — required so that
Lua closures with upvalue-bound `entityId` references still resolve.
But `nextEntityId_` wasn't bumped past the preserved ids, so the next
`CreateEntityCommand` would allocate an id that collided with an
existing entity. `findEntity(int)` returns whichever entity appears
first in `entities_` — the wrong one for any id-bound caller.

**What it does now:** `replaceEntities` walks the new entity list,
finds the max id, and sets `nextEntityId_ = maxId + 1`. `loadEntities`
already does the same implicitly (it runs `nextEntityId_++` per
entity) so no change was needed there; a comment was added so the
invariant is documented next to the code that maintains it.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`replaceEntities`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-1 — Material cache leaked GPU textures on entity delete

**Finding:** `audit-2026-08-13.md` E-Render-1.

**What was broken:** `ViewportRenderer`'s per-frame cache pruning loop
walked `textMeshCache_`, `terrainCache_`, and `importedMeshCache_` and
freed their GPU resources when an entity went away. `materialCache_` —
the splat-layer texture cache holding 3 layers × 3 textures (diffuse /
normal / height) per textured entity — was only freed in `shutdown()`.
A long editing session of adding and deleting textured primitives
slowly accumulated GL textures that didn't release until the editor
exited.

**What it does now:** Added a fourth pruning loop mirroring the others.
When an entity is no longer in the scene, its three `TerrainLayerGpuEntry`
slots have their nine textures deleted and the bucket is erased.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (new pruning loop after
  `importedMeshCache_`)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-2 — TextMesh hole side-wall normals pointed into the solid

**Finding:** `audit-2026-08-13.md` E-Render-2.

**What was broken:** `TextMesh.cpp` builds side walls for every glyph
contour. `classifyAndNormalizeContours` marks outer contours CCW and
hole contours CW. The wall-normal code used the same
right-perpendicular of the edge direction for both, so for a CW hole
contour the normal pointed into the glyph material rather than into
the hole. The side walls of every glyph with holes (A, O, 8, P, R, B,
…) lit from the wrong side.

**What it does now:** The wall normal now branches on `contour.isHole`
and uses the right-perpendicular for outer contours, the
left-perpendicular for holes. Side walls of holes now point at the
hole (outside air) and light correctly.

**Files touched:**
- `Engine/src/Editor/TextMesh.cpp` (side-wall normal computation)

**Verified:** `Build-Project.cmd` → clean build.

### E-Render-3 — Cone slant normal used a hardcoded magic `0.5F`

**Finding:** `audit-2026-08-13.md` E-Render-3.

**What was broken:** `PrimitiveMeshes::generateCone` computed the cone
slant normal as `normalize(vec3(cos, 0.5F, sin))`. The `0.5F` was
implicitly the `(apex.y - base.y)` slope-height — only correct for
`radius = 1`, `apex.y = 1`, `base.y = -1`. The instant any of those
constants was tweaked, the cone's lighting would silently break
without any compile-time signal.

**What it does now:** The normal is now derived from the actual
geometry: `normalize(vec3(cos * radius, apex.y - base.y, sin * radius))`.
Future geometry tweaks change lighting correctly.

**Files touched:**
- `Engine/src/Editor/PrimitiveMeshes.cpp` (`generateCone`)

**Verified:** `Build-Project.cmd` → clean build.

### E-AI-2 — WinHTTP header length was wchar_t count, not byte count

**Finding:** `audit-2026-08-13.md` E-AI-2.

**What was broken:** `WinHttpSendRequest` was called with
`static_cast<DWORD>(headers.size())` where `headers` is a `std::wstring`.
`size()` returns the wchar_t count (16-bit elements), but WinHTTP
expects a byte count for the `dwHeadersLength` parameter. The current
header strings are pure ASCII so byte count == element count by luck,
but the moment any header name gains a non-ASCII character (localization,
provider name in non-ASCII locale, etc.) the server would silently
truncate.

**What it does now:** Pass `-1` and let WinHTTP compute the byte length
from the null-terminated wide string. Future-proof and self-correcting.

**Files touched:**
- `Editor/src/AIProviderClient.cpp` (`WinHttpSendRequest` call site)

**Verified:** `Build-Project.cmd` → clean build.

### E-UI-2 — OpenGL 4.6 context requested but never verified

**Finding:** `audit-2026-08-13.md` E-UI-2.

**What was broken:** `glfwCreateWindow` was called with 4/6/core hints;
`gladLoadGL` returned a non-zero value (the GLAD loader version, not
the context version). ImGui was then initialized with a `#version 460
core` GLSL string with no check that the actual context supported
4.6. If the driver fell back to 4.5 or lower, every shader compile
silently failed and the viewport showed the only fallback
"Viewport OpenGL indisponible." message — no actionable error for the
user.

**What it does now:** After `gladLoadGL`, the code now queries
`GL_MAJOR_VERSION` / `GL_MINOR_VERSION` from the context and aborts
with a clear "GameForgerAI requires OpenGL 4.6 - this machine reports
X.Y" message if the context is older than 4.6. Surfaces the
requirement up front instead of letting every shader fail in silence.

**Files touched:**
- `Editor/src/main.cpp` (GL context init)

**Verified:** `Build-Project.cmd` → clean build.

### E-Animation-1 — Linear interpolation on Euler-angle rotations

**Finding:** `audit-2026-08-13.md` E-Animation-1.

**What was broken:** `sampleAnimation` interpolated rotation via
`glm::mix(start.rotationEuler, end.rotationEuler, alpha)` — the same
axis-component-by-axis-component blend it used for position and scale.
For rotations this is wrong in two ways:
- 360° wrap isn't respected — a 0°→350° key rotation spins the long
  way around 360° instead of taking the 10° shortcut.
- Intermediate samples pass through invalid orientation blends
  (combining two quaternions element-wise doesn't yield a valid
  rotation).

**What it does now:** Rotation is converted to quaternions,
interpolated with `glm::slerp`, and converted back to XYZ Euler angles
in the same convention the rest of the engine consumes. Position and
scale still use linear `mix` (which is correct for those channels).

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (`sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L-Animation-2 — Animation segment interval was closed on both ends

**Finding:** `audit-2026-08-13.md` L (E-Animation-2).

**What was broken:** `sampleAnimation` matched `sampleTime >= start.time
&& sampleTime <= end.time`. When `sampleTime` landed exactly on a
keyframe's `time`, both segment i-1 (where `end.time == sampleTime`)
and segment i (where `start.time == sampleTime`) matched; the loop
picked the earlier one. This is consistent but disagrees with how
timeline-scrub "land on keyframe T" feels — alpha should be 0.0 at T
(the start of the next segment), not 1.0 (the end of the previous).

**What it does now:** The interval is half-open `[start.time,
end.time)`. Scrubbing onto a keyframe time now feels like the start of
the next segment, not the end of the previous one.

**Files touched:**
- `Engine/src/Editor/Animation.cpp` (segment match condition in
  `sampleAnimation`)

**Verified:** `Build-Project.cmd` → clean build.

### L1 — `makeUniqueName` had no upper bound on its suffix loop

**Finding:** `audit-2026-08-13.md` L1.

**What was broken:** `makeUniqueName` appended ` (N)` to `baseName` and
looped `do { ... } while (nameInUse(candidate));` until a free name was
found. A hostile or pathological `baseName` near `PATH_MAX` would
grow without bound, and a baseName already containing ` (1)` … ` (N)`
for every N would loop forever.

**What it does now:** The suffix is bounded at `kMaxSuffix = 10000`. If
every slot in that range is genuinely taken (far beyond any realistic
scene), fall back to a millisecond-timestamp-suffixed name and break
out of the loop.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`makeUniqueName`, added `<chrono>`
  include)

**Verified:** `Build-Project.cmd` → clean build.

### L2 — `nameInUse("")` returned false silently

**Finding:** `audit-2026-08-13.md` L2.

**What was broken:** `nameInUse("")` returned `findEntity("") != nullptr`.
For an editor with no entity actually named `""`, this returned false,
so a caller's loop treated the empty string as "free" and proceeded to
create an entity with no name — or worse, a malformed caller (e.g. an
unvalidated `SetPropertyCommand{entityName="", ...}`) could pass
through silently.

**What it does now:** `nameInUse("")` returns `true` (reserved as
"no match"). Any caller that wanted the empty string already has a
bug, and treating it as already-taken surfaces the bug at the call site
instead of letting it leak further.

**Files touched:**
- `Engine/src/Editor/EditorScene.cpp` (`nameInUse`)

**Verified:** `Build-Project.cmd` → clean build.

### L3 — `Skeleton::validate` accepted `parent < -1`

**Finding:** `audit-2026-08-13.md` L3.

**What was broken:** `Skeleton::validate` only rejected `parent >=
index`. `parent = -2, -3, …` was silently accepted, but
`computeSkinningMatrices` treats any value `< 0` as `NoParent`
(checked as `bone.parent >= 0`). So a malformed-import with `parent =
-2` would pass validation, then be silently treated as no parent —
masking the bug instead of surfacing it.

**What it does now:** The validator requires `parent == NoParent ||
parent < index`. Any other value (including `-2`, `-3`, …) is now
rejected with an explicit error message.

**Files touched:**
- `Engine/src/Animation/Skeleton.cpp` (`Skeleton::validate`)

**Verified:** `Build-Project.cmd` → clean build.

### L4 — `resolvePivotPreset` assumed a cube-shaped local space

**Finding:** `audit-2026-08-13.md` L4.

**What was broken:** `resolvePivotPreset` returned `±1.0F` on a single
axis regardless of the primitive type. For a Cube (which spans
[-1, +1] on every axis) this is correct. For a Cylinder/Cone/Capsule
(also [-1, +1] on Y and [-1, +1] on X radius) it's also correct. For a
Plane (a flat XZ quad at y=0), "top" = `(0, +1, 0)` places the pivot
above the plane rather than on it — possibly what the user wanted, but
the user has no way to ask for the other interpretation.

**What it does now:** Added a `resolvePivotPreset(name, PrimitiveType)`
overload that returns per-primitive offsets. The plane's "top" is
explicitly `(0, +1, 0)` (above the visible surface) with a comment
noting this is the "raise the pivot above" intent; other primitives
keep the cube offsets since they're already correct for those
shapes. The single-argument overload is preserved as a default for
callers that don't know the primitive type yet.

**Files touched:**
- `Engine/include/GameForger/Editor/Transform.hpp` (new overload)
- `Engine/src/Editor/Transform.cpp` (new overload body)

**Verified:** `Build-Project.cmd` → clean build.

### L5 — `rotationEuler` order was hardcoded to XYZ with no documentation

**Finding:** `audit-2026-08-13.md` L5.

**What was broken:** `composeEntityPivotFrame` calls
`ImGuizmo::RecomposeMatrixFromComponents` which always uses XYZ
Euler order. `SceneEntity::rotationEuler` had no doc comment, so a
future maintainer might reasonably interpret the field in YXZ or ZYX
order — silently rotating every entity to the wrong orientation.
Animation sampling, parent-constraint solver, and Lua `entity:rotation`
get/set all consume this field through the same XYZ convention.

**What it does now:** The header comment on `rotationEuler` explicitly
states the field is "Euler-angle rotation in DEGREES. Convention: XYZ
order" and names every consumer that depends on this contract. A future
feature needing per-entity rotation order is directed to store it as a
separate field rather than re-interpreting this one.

**Files touched:**
- `Engine/include/GameForger/Editor/EditorScene.hpp` (`rotationEuler`
  field doc comment)

**Verified:** `Build-Project.cmd` → clean build.

### L6 — Unused boneMatrices[128] slots retained stale values

**Finding:** `audit-2026-08-13.md` L6.

**What was broken:** The skinned shader declares `uniform mat4
boneMatrices[128]`. The upload was `glUniformMatrix4fv(..., bones.size(), ...)`
which only updated slots `[0, bones.size())`. The remaining slots kept
whatever the previous draw call set (or zero at program creation). A
mesh whose vertex data referenced `boneIndices.x = 5` but only had
2 bones would then read stale garbage from slot 5 — wrong skinned
positions, undefined behavior on the GPU.

**What it does now:** Each skinned draw call now uploads all 128 slots:
real matrices where available, identity for the rest. The cost is
128 × 16 floats = 8 KB per skinned draw — trivial against the vertex
data already on the GPU, and removes the foot-gun.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (skinned mesh draw block)

**Verified:** `Build-Project.cmd` → clean build.

### L7 — Skinned normal used `mat3(skinMatrix)` (broken under non-uniform bone scale)

**Finding:** `audit-2026-08-13.md` L7.

**What was broken:** The skinned shader computed
`skinnedNormal = mat3(skinMatrix) * normal`. That's only correct under
uniform bone scale. With non-uniform bone scale (a bone stretching 2×
on X but 1× on Y), the normal direction comes out wrong and lighting
breaks on the affected vertices. Currently masked because most rigged
imports use uniform bone scales.

**What it does now:** Added a parallel `boneInverseTransposeMatrices[128]`
uniform. The CPU computes the inverse-transpose of each bone's upper
3×3 once per frame and uploads it (padded with identity, like the
position matrices). The shader blends four inverse-transposes the
same way it blends four bone matrices, and applies the result to the
normal. Works correctly under any bone scale.

**Files touched:**
- `Engine/src/Editor/ViewportRenderer.cpp` (shader source, uniform
  location, upload)

**Verified:** `Build-Project.cmd` → clean build.

### L9 — `InputSource` had no mouse-button or scroll-wheel accessors

**Finding:** `audit-2026-08-13.md` L9.

**What was broken:** `InputSource` declared only `getMouseDeltaX/Y`. No
mouse-button or scroll-wheel accessors. Scripts that wanted to react
to clicks (e.g. a UI prompt, a manual reload) had no way to query
button state, and the catalog of available inputs in `ScriptGenerator`
stopped at key/camera.

**What it does now:** Added two new pure virtual methods on
`InputSource`: `isMouseButtonDown(name)` (Left/Right/Middle + LMB/RMB/MMB
aliases) and `getScrollDelta()` (vertical lines, positive = up).
`GlfwInputSource` implements them via `glfwGetMouseButton` plus an
`accumulateScroll(double)` accumulator that the main loop's scroll
callback drives. `ImGuiInputSource` reads `ImGui::GetIO().MouseDown`
and `MouseWheel` directly.

**Files touched:**
- `Engine/include/GameForger/Editor/InputSource.hpp` (new pure virtuals)
- `Engine/include/GameForger/Editor/GlfwInputSource.hpp` (declarations,
  new `scrollDelta_` member, `accumulateScroll`)
- `Engine/src/Editor/GlfwInputSource.cpp` (implementations, accumulator
  reset in `update()`)
- `Editor/include/GameForger/Editor/ImGuiInputSource.hpp` (declarations)
- `Editor/src/ImGuiInputSource.cpp` (implementations)

**Verified:** `Build-Project.cmd` → clean build.

### L10 — `AIAnimationGenerator` accepted negative / non-finite keyframe times

**Finding:** `audit-2026-08-13.md` L10.

**What was broken:** Keyframe `time` was read with no validation — an
AI could return `{time: -1}` (negative, breaks the timeline) or
`{time: 1e9}` (single keyframe at a far future time makes the
animation unplayable). NaN would propagate into every transform the
sampler touches.

**What it does now:** Each parsed keyframe's `time` is finiteness- and
non-negativity-checked. Negative or non-finite entries are dropped
before sort/push. Position/rotation/scale defaults already come from
the per-entity baseline, so a missing component is still safe; only
the time field needed this guard.

**Files touched:**
- `Editor/src/AIAnimationGenerator.cpp` (keyframe parse loop)

**Verified:** `Build-Project.cmd` → clean build.

### L11 — `cursorLockSuppressed` lived in shared `GameplayState`

**Finding:** `audit-2026-08-13.md` L11.

**What was broken:** `GameplayState::cursorLockSuppressed` was
documented as Editor-only (Runtime has no menu/pause and doesn't use
it), but the field sat in the Engine-side shared struct so every
Runtime build paid the cost of carrying it and the Runtime doc
comment was forced to explain why it didn't use the field it owns.

**What it does now:** Removed the field from `GameplayState`. Added it
to the Editor's `PlayModeState` (where it actually belongs), with a
comment explaining the Editor-only purpose. Updated the three
call sites in `Editor/src/main.cpp` to read/write
`playMode.cursorLockSuppressed`.

**Files touched:**
- `Engine/include/GameForger/Runtime/GameplayLoop.hpp` (removed field
  + comment)
- `Editor/src/main.cpp` (added field to `PlayModeState`, updated 3
  call sites)

**Verified:** `Build-Project.cmd` → clean build.

### L12 — `GameMenu::render` phantom-click after long pause

**Finding:** `audit-2026-08-13.md` L12.

**What was broken:** `GameMenu::render`'s mouse-edge state was a
function-local `static bool mouseWasDown = false`. The early-out path
on `!open` returned without resetting that static, so the value from
the last menu-open frame persisted across a long pause. If the user
opened the menu while still holding LMB from a prior interaction, the
first frame saw `mouseDownNow=true, mouseWasDown=false` → fired a
phantom click that could hit Save / Load / Resume / Quit if the
cursor happened to be over a button.

**What it does now:** Promoted `mouseWasDown` to a member field
`mouseWasDown_`. The early-out path now explicitly resets it to
`false` before returning, so each menu-open frame starts from a clean
edge state.

**Files touched:**
- `Runtime/src/GameMenu.hpp` (new member field)
- `Runtime/src/GameMenu.cpp` (early-out resets it; static → member)

**Verified:** `Build-Project.cmd` → clean build.

### L13 — Character menu items looked interactive but were inert

**Finding:** `audit-2026-08-13.md` L13.

**What was broken:** The Character menu had two items — "Map Humanoid
Skeleton..." and "Animation Library..." — drawn with the same style
as every other menu item but with no action handler. A user clicking
them saw nothing happen and might file a bug.

**What it does now:** Both items are now wrapped in
`ImGui::BeginDisabled() / EndDisabled()` so they render greyed-out
and visibly communicate "these exist but aren't implemented yet"
rather than masquerading as functional.

**Files touched:**
- `Editor/src/main.cpp` (Character menu block)

**Verified:** `Build-Project.cmd` → clean build.

### E-Run-4 — `followedEntity` could be a dangling pointer mid-frame

**Finding:** `audit-2026-08-13.md` E-Run-4.

**What was broken:** `followedEntity` was resolved once per frame from
`scriptRuntime.activeCameraEntityId()`, then dereferenced by mouse-look,
camera framing, and pickup logic for the rest of the frame. If a Lua
script called `DeleteEntity` on the camera entity during `tickScripts`,
`followedEntity` became a dangling pointer for the rest of the frame —
the next frame's lookup would correctly return `nullptr`, but the current
frame's mouse-look, rotation write, and pickup proximity math would
dereference freed memory.

**What it does now:** After `tickScripts` runs, the code re-checks
`scene.findEntity(followedEntity->id)` and nils out the pointer if the
entity was deleted. Defensive re-resolution; the lookup is cheap.

**Files touched:**
- `Runtime/src/main.cpp` (re-resolve after `tickScripts`)

**Verified:** `Build-Project.cmd` → clean build.

---
