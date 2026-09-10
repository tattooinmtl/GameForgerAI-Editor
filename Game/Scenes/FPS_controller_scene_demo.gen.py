import json

E = []
def add(name, **k):
    e = {
        "name": name, "active": True, "tags": k.get("tags", []),
        "primitive": k.get("prim", "cube"),
        "position": k.get("pos", [0, 0, 0]),
        "rotation": k.get("rot", [0, 0, 0]),
        "scale":    k.get("scale", [1, 1, 1]),
        "color":    k.get("color", [0.8, 0.8, 0.8]),
        "pivot":    k.get("pivot", [0, 0, 0]),
        "parentName": k.get("parent", ""),
        "localPosition": k.get("lpos", [0, 0, 0]),
        "localRotationEuler": k.get("lrot", [0, 0, 0]),
        "localScale": k.get("lscale", [1, 1, 1]),
        "scripts": k.get("scripts", []),
        "hasCollider": k.get("col", False),
    }
    if "model" in k:
        e["isImportedMesh"] = True
        e["importedMesh"] = {"sourcePath": "Game/Models/kit/" + k["model"]}
        e["colliderType"] = "mesh" if k.get("col") else "box"
    if "light" in k:
        e["isLight"] = True
        e["light"] = k["light"]
    if "camera" in k:
        e["isCamera"] = True
        e["camera"] = k["camera"]
    if "ui" in k:
        e["isUIElement"] = True
        e["ui"] = k["ui"]
    if "pickup" in k:
        e["isPickupItem"] = True
        e["pickupItem"] = {"itemName": k["pickup"], "iconPath": ""}
    E.append(e)

# ---------- lighting ----------
add("Sun", pos=[0, 14, 0], rot=[48, -38, 0],
    light={"type": "directional", "color": [1.0, 0.96, 0.88], "intensity": 1.05,
           "castShadows": True, "shadowBias": 0.0018})
add("RoomLamp", pos=[0, 4.2, 6], rot=[90, 0, 0],
    light={"type": "spot", "color": [1.0, 0.88, 0.65], "intensity": 2.6, "range": 14.0,
           "innerCone": 26, "outerCone": 40, "castShadows": True, "shadowBias": 0.0016})
add("VaultGlow", pos=[0, 2.2, 20],
    light={"type": "point", "color": [0.45, 0.85, 1.0], "intensity": 2.4, "range": 8.0,
           "castShadows": False})

# ---------- floor + walls ----------
add("Floor", prim="plane", pos=[0, 0, 8], scale=[16, 1, 16], color=[0.42, 0.44, 0.47],
    col=True, tags=["Ground"])
W = 0.4
def wall(n, pos, scale):
    add(n, prim="cube", pos=pos, scale=scale, color=[0.55, 0.53, 0.50], col=True)
wall("Wall_West",  [-15, 2, 8],  [W, 2, 16])
wall("Wall_East",  [15, 2, 8],   [W, 2, 16])
wall("Wall_South", [0, 2, -8],   [15, 2, W])
wall("Wall_North", [0, 2, 24],   [15, 2, W])
wall("Divider_A", [-10.5, 2, 12], [4.5, 2, W])
wall("Divider_B", [0, 2, 12],     [3.0, 2, W])
wall("Divider_C", [10.5, 2, 12],  [4.5, 2, W])

# ---------- player ----------
add("Player", prim="capsule", pos=[0, 1.0, 0], color=[0.30, 0.55, 0.85],
    tags=["Player"], col=True,
    scripts=["Game/Scripts/fps_controller.lua",
             "Game/Scripts/inventory_system.lua",
             "Game/Scripts/weapons_system.lua"])
add("GameManager", prim="empty", pos=[0, 0, -6],
    scripts=["Game/Scripts/game_manager.lua"])

# ---------- main camera + viewmodel children ----------
add("MainCamera", pos=[0, 1.75, 0], rot=[0, 0, 0],
    camera={"fieldOfView": 68.0, "nearClip": 0.05, "farClip": 400.0,
            "clearColor": [0.05, 0.06, 0.09], "isMainCamera": True,
            # Lens layers. Restrained on purpose - the demo should show that
            # the stack works without hiding the scene behind a heavy grade.
            # The Inspector's Presets list has Old Film, Noir, CRT, Thermal.
            "effects": {"enabled": True, "colorFilter": "none", "contrast": 1.12,
                        "saturation": 1.10, "grainAmount": 0.10, "grainSize": 2.0,
                        "vignetteAmount": 0.35, "vignetteSoftness": 0.55,
                        "chromaticAberration": 0.08}})

# Viewmodel placement. These numbers were tuned by running the demo and
# looking at it, not derived - a viewmodel is judged by eye. The rule of thumb
# that made them work: the weapon must sit far enough right and low enough to
# leave the centre of the screen clear for the crosshair, and small enough that
# it reads as "held" rather than "standing in front of the camera".
#
# The models are all authored with their length along +Y (blade up), so the
# melee weapons need a large negative X rotation to point forward. The tripod
# gun is genuinely a mounted weapon, not a handheld one - scaled right down so
# only its receiver and barrel fill the corner.
#
# X IS NEGATIVE FOR A RIGHT-HANDED WEAPON. Screen-right in this engine is
# -X when forward is +Z, because GLM lookAtRH puts right at
# cross(forward, +Y) - the same convention the script API getRight() is
# locked to by testGetRightMatchesFpsCamera. A positive X offset here puts
# the gun on the LEFT of the screen, which is exactly what it did the first
# time these numbers were tried.
add("Hand", parent="MainCamera", model="righthand-item-6c7ff0.glb",
    lpos=[-0.20, -0.20, 0.46], lrot=[8, -96, 0], lscale=[0.30, 0.30, 0.30],
    color=[0.85, 0.68, 0.55])
# The gun is built from primitives rather than an imported model, on purpose.
# tripod-machine-gun is a single flattened mesh with the weapon welded to its
# tripod - the importer has no sub-object hierarchy (see MissingFunctions 3.5),
# so there is no way to take just the gun. Dragging a tripod around at eye
# level looks broken. A carbine assembled from five boxes reads correctly as a
# held weapon, and doubles as the worked example of the actual workflow:
# parent primitives to the Main Camera and they become a viewmodel.
#
# "Gun" itself is an Empty - the parent the weapons script shows and hides.
# Its parts hang off it, so one toggle moves the whole weapon.
add("Gun", prim="empty", parent="MainCamera",
    lpos=[-0.26, -0.25, 0.55], lrot=[0, -3, 0], lscale=[0.88, 0.88, 0.88])
GUNMETAL = [0.16, 0.17, 0.19]
POLYMER  = [0.11, 0.12, 0.13]
add("Gun_Receiver", prim="cube", parent="Gun", lpos=[0, 0, 0.06],
    lscale=[0.035, 0.038, 0.14], color=GUNMETAL)
add("Gun_Barrel",   prim="cylinder", parent="Gun", lpos=[0, 0.012, 0.30],
    lrot=[90, 0, 0], lscale=[0.014, 0.16, 0.014], color=GUNMETAL)
add("Gun_Magazine", prim="cube", parent="Gun", lpos=[0, -0.075, 0.03],
    lrot=[12, 0, 0], lscale=[0.022, 0.055, 0.030], color=POLYMER)
add("Gun_Stock",    prim="cube", parent="Gun", lpos=[0, -0.012, -0.12],
    lscale=[0.026, 0.030, 0.09], color=POLYMER)
add("Gun_Sight",    prim="cube", parent="Gun", lpos=[0, 0.052, 0.10],
    lscale=[0.008, 0.018, 0.010], color=GUNMETAL)
add("Sword", parent="MainCamera", model="sword-11907e.glb",
    lpos=[-0.26, -0.30, 0.46], lrot=[-68, -10, -12], lscale=[0.42, 0.42, 0.42])
add("Axe", parent="MainCamera", model="hand-axe-cee9e7.glb",
    lpos=[-0.26, -0.28, 0.44], lrot=[-66, -8, -10], lscale=[0.50, 0.50, 0.50])
add("Hammer", parent="MainCamera", model="hammer-fa3e51.glb",
    lpos=[-0.25, -0.26, 0.42], lrot=[-64, -6, -10], lscale=[0.62, 0.62, 0.62])
# Parented to the Gun, not the camera, so it tracks the barrel through the
# weapon's own recoil/swing rather than floating at a fixed screen point.
add("gun_muzzle", prim="empty", parent="Gun", tags=["gun_muzzle"],
    lpos=[0, 0.012, 0.46], lscale=[0.1, 0.1, 0.1])

add("Crosshair", parent="MainCamera",
    ui={"kind": "crosshair", "anchor": "center", "offset": [0, 0], "size": [9, 9],
        "color": [1, 1, 1], "opacity": 0.85, "thickness": 2.0, "gap": 5.0})
add("HudTitle", parent="MainCamera",
    ui={"kind": "text", "anchor": "top_left", "offset": [190, 30], "size": [10, 10],
        "color": [1.0, 0.82, 0.30], "opacity": 1.0,
        "text": "WASD move  -  Mouse look  -  LMB attack  -  Wheel or 1-4 switch  -  E interact  -  I inventory",
        "fontSize": 18.0})
add("HudHint", parent="MainCamera",
    ui={"kind": "text", "anchor": "bottom_center", "offset": [0, -30], "size": [10, 10],
        "color": [0.80, 0.88, 1.0], "opacity": 0.9,
        "text": "Brown door needs the Brass Key.  Grey door needs code 1234 on the green keypad.",
        "fontSize": 16.0})

# ---------- doors ----------
add("Door_Key", prim="cube", pos=[-6.2, 1.6, 12], scale=[1.5, 1.6, 0.12],
    color=[0.45, 0.30, 0.18], pivot=[-1, 0, 0], col=True,
    scripts=["Game/Scripts/door_interaction.lua"])
add("Door_Code", prim="cube", pos=[6.2, 1.6, 12], scale=[1.5, 1.6, 0.12],
    color=[0.30, 0.34, 0.42], pivot=[1, 0, 0], col=True,
    scripts=["Game/Scripts/door_interaction.lua"])
add("Door_Open", prim="cube", pos=[0, 1.6, 12], scale=[0.06, 1.6, 0.12],
    color=[0.35, 0.35, 0.35], pivot=[-1, 0, 0],
    scripts=["Game/Scripts/door_interaction.lua"])
add("Keypad", prim="cube", pos=[7.9, 1.5, 11.5], scale=[0.22, 0.32, 0.08],
    color=[0.15, 0.65, 0.35], col=True,
    scripts=["Game/Scripts/keypad_panel.lua"])

# ---------- key + pickups ----------
add("BrassKey", model="compass-d16dde.glb", pos=[-9.0, 0.35, 4.0], scale=[2.2, 2.2, 2.2],
    tags=["Key"], pickup="Brass Key", scripts=["Game/Scripts/key_item.lua"])
add("Potion", model="potion-bottle-b28245.glb", pos=[3.0, 0.25, 3.0], pickup="Potion")
add("Bread", model="bread-loaf-0e0ab5.glb", pos=[3.9, 0.20, 3.6], pickup="Bread")
add("Compass", model="compass-d16dde.glb", pos=[-2.6, 0.25, 5.2], scale=[1.6, 1.6, 1.6],
    pickup="Compass")
add("LootSword", model="arming-sword-401972.glb", pos=[-4.5, 0.30, 18.0], rot=[0, 0, 90],
    pickup="Arming Sword")

# ---------- props ----------
props = [
    ("Barrel_A", "barrel-aadc94.glb", [-12, 0.46, 3], 0, 1.0, True),
    ("Barrel_B", "barrel-aadc94.glb", [-12, 0.46, 4.2], 25, 1.0, True),
    ("Quench", "quench-barrel-0c78fd.glb", [12, 0.35, 4], 0, 1.0, True),
    ("Chest", "treasure-chest-c2ac4f.glb", [0, 0.20, 20], 0, 1.2, True),
    ("Lamp_A", "lamp-post-c9e409.glb", [-6, 0, 6], 0, 1.0, True),
    ("Lamp_B", "lamp-post-c9e409.glb", [6, 0, 6], 0, 1.0, True),
    ("Firewood", "firewood-stack-810f49.glb", [10, 0.1, 18], 40, 1.0, True),
    ("HayBale", "hay-bale-079e8c.glb", [-10, 0.26, 18], 15, 1.0, True),
    ("FirePit", "stone-fire-pit-4136b0.glb", [0, 0.05, 6], 0, 1.0, True),
    ("Crystal", "crystal-cluster-624138.glb", [-3, 0.1, 20], 0, 1.0, False),
    ("Rock_A", "field-rock-5e37c6.glb", [13, 0.1, 16], 30, 1.0, True),
    ("Rock_B", "medium-rock-e08405.glb", [-13, 0.1, 10], 70, 1.0, True),
    ("Boulder", "large-boulder-a29b99.glb", [12, 0.1, 21], 10, 1.0, True),
    ("Grave", "carved-grave-stele-a1359c.glb", [8, 0.1, 20], 0, 1.0, True),
    ("Pine", "pine-tree-3ea075.glb", [-13, 0, 22], 0, 1.4, False),
    ("Oak", "broadleaf-oak-997c22.glb", [14, 0, -5], 0, 0.8, False),
    ("Sailboat", "sailboat-3ee283.glb", [5, 0.9, 20], 0, 1.0, False),
    # Where a tripod-mounted weapon actually belongs - emplaced, not carried.
    ("TripodGun", "tripod-machine-gun-80f906.glb", [11, 0.05, 8], -40, 1.0, True),
]
for n, m, p, ry, sc, col in props:
    add(n, model=m, pos=p, rot=[0, ry, 0], scale=[sc, sc, sc], col=col)

for i in range(7):
    add("Path_%d" % i, model="path-stone-a-aead48.glb",
        pos=[0, 0.03, 1.0 + i * 1.5], rot=[0, (i * 23) % 360, 0])
for i in range(4):
    add("Plank_%d" % i, model="plank-walkway-tile-667ff1.glb",
        pos=[-6.2, 0.03, 13.5 + i * 1.0])
for i in range(6):
    add("Tuft_%d" % i, model="grass-tuft-62e90c.glb",
        pos=[-11 + i * 4.2, 0.02, 16.0 + (i % 3)], scale=[1.5, 1.5, 1.5])

doc = {"format": "GameForgerScene", "version": 8, "entities": E}
with open("Game/Scenes/FPS_controller_scene_demo.gfprod", "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2)
print("entities:", len(E))
