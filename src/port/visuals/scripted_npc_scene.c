#include "port/visuals/scripted_npc_scene.h"

#include <nitro.h>
#include <string.h>

#include "field/field_system.h"
#include "overlay005/ov5_021ECC20.h"
#include "overlay005/map_object_anim_cmd.h"
#include "overlay005/land_data.h"
#include "overlay005/object_event_gfx_data.h"

#include "heap.h"
#include "map_object.h"
#include "map_object_move.h"
#include "unk_020655F4.h"
#include "script_manager.h"
#include "sys_task.h"
#include "terrain_collision_manager.h"

// Only ordinary 32x32 billboards are supported by this prototype.
// This volume includes their billboard offset, shadow and reflection, plus
// an eight-pixel guard band in clip space. Larger/special models use 4:3.
#define NPC_SCENE_MAX_TILES     32
#define NPC_SCENE_MARGIN_PIXELS 8

typedef struct {
    FieldSystem *fieldSystem;
    MapObject *object;
    SysTask *animation;
    MapObjectAnimCmd commands[2];
    int dir;
    int remaining;
    BOOL exiting;
    BOOL nativeView;
    u32 nativeFrame;
    VecFx32 anchor;
    u8 path[NPC_SCENE_MAX_TILES];
    int pathCount;
    int pathIndex;
    BOOL keepObject;
    int minimumSteps;
    const MapObjectAnimCmd *prefix;
    int prefixCount;
    int prefixIndex;
    BOOL prefixActive;
    BOOL nativeWalking;
} NpcSceneTask;

static BOOL NpcScene_OrdinaryBillboard(const MapObject *object)
{
    const ObjectEventGfxModelAnimEntry *entry = gObjectEventGfxModelAnimsTable;
    while (entry->graphicsID != OBJ_EVENT_GFX_SENTINEL_ID) {
        if (entry->graphicsID == MapObject_GetGraphicsID(object)) {
            return entry->modelID == BILLBOARD_MODEL_GENERIC_32x32;
        }
        entry++;
    }
    return FALSE;
}

// Conservative homogeneous frustum test: a volume is outside only when every
// corner lies outside the same plane. Never divide by a near-zero clip w.
static BOOL NpcScene_PositionVisible(const FieldSystem *fieldSystem, const VecFx32 *pos)
{
    if (fieldSystem->npcSceneViewWidth == 0) {
        return TRUE;
    }
    const MtxFx43 *view = &fieldSystem->npcSceneViewMatrix;
    const MtxFx44 *projection = &fieldSystem->npcSceneProjectionMatrix;
    double marginX = 2.0 * NPC_SCENE_MARGIN_PIXELS / fieldSystem->npcSceneViewWidth;
    double marginY = 2.0 * NPC_SCENE_MARGIN_PIXELS / HW_LCD_HEIGHT;
    unsigned outside = 0x3f;
    for (int i = 0; i < 8; i++) {
        double x = (double)pos->x / FX32_ONE + ((i & 1) ? 24 : -24);
        double y = (double)pos->y / FX32_ONE + ((i & 2) ? 48 : -48);
        double z = (double)pos->z / FX32_ONE + ((i & 4) ? 24 : -24);
        double vx = (x * view->_00 + y * view->_10 + z * view->_20 + view->_30) / FX32_ONE;
        double vy = (x * view->_01 + y * view->_11 + z * view->_21 + view->_31) / FX32_ONE;
        double vz = (x * view->_02 + y * view->_12 + z * view->_22 + view->_32) / FX32_ONE;
        double cx = (vx * projection->_00 + vy * projection->_10 + vz * projection->_20 + projection->_30) / FX32_ONE;
        double cy = (vx * projection->_01 + vy * projection->_11 + vz * projection->_21 + projection->_31) / FX32_ONE;
        double cz = (vx * projection->_02 + vy * projection->_12 + vz * projection->_22 + projection->_32) / FX32_ONE;
        double cw = (vx * projection->_03 + vy * projection->_13 + vz * projection->_23 + projection->_33) / FX32_ONE;
        if (cw <= 0) {
            return TRUE;
        }
        unsigned planes = 0;
        if (cx < -cw * (1 + marginX)) {
            planes |= 1;
        }
        if (cx > cw * (1 + marginX)) {
            planes |= 2;
        }
        if (cy < -cw * (1 + marginY)) {
            planes |= 4;
        }
        if (cy > cw * (1 + marginY)) {
            planes |= 8;
        }
        if (cz < -cw) {
            planes |= 16;
        }
        if (cz > cw) {
            planes |= 32;
        }
        outside &= planes;
    }
    return outside == 0;
}

static BOOL NpcScene_ObjectVisible(const NpcSceneTask *scene)
{
    VecFx32 pos;
    ov5_021ECDA0(scene->object, &pos);
    return NpcScene_PositionVisible(scene->fieldSystem, &pos);
}

static int NpcScene_WalkDirection(u16 action)
{
    // Do not accept jumps, emotes, on-the-spot actions or arbitrary bytecode.
    if (action >= MOVEMENT_ACTION_WALK_SLOW_NORTH && action <= MOVEMENT_ACTION_WALK_SLOW_EAST) {
        return action - MOVEMENT_ACTION_WALK_SLOW_NORTH;
    }
    if (action >= MOVEMENT_ACTION_WALK_NORMAL_NORTH && action <= MOVEMENT_ACTION_WALK_NORMAL_EAST) {
        return action - MOVEMENT_ACTION_WALK_NORMAL_NORTH;
    }
    if (action >= MOVEMENT_ACTION_WALK_FAST_NORTH && action <= MOVEMENT_ACTION_WALK_FAST_EAST) {
        return action - MOVEMENT_ACTION_WALK_FAST_NORTH;
    }
    return DIR_NONE;
}

static BOOL NpcScene_SafeStep(NpcSceneTask *scene, const VecFx32 *source, int dir, VecFx32 *dest)
{
    MapObject *object = scene->object;
    FieldSystem *fieldSystem = scene->fieldSystem;
    int x = source->x / MAP_OBJECT_TILE_SIZE;
    int z = source->z / MAP_OBJECT_TILE_SIZE;
    int offsetX = LandDataManager_GetOffsetTileX(fieldSystem->landDataMan);
    int offsetZ = LandDataManager_GetOffsetTileZ(fieldSystem->landDataMan);
    int nextX = x + MapObject_GetDxFromDir(dir);
    int nextZ = z + MapObject_GetDzFromDir(dir);
    if (nextX < offsetX || nextZ < offsetZ || nextX >= offsetX + MapMatrix_GetWidth(fieldSystem->mapMatrix) * 32 || nextZ >= offsetZ + MapMatrix_GetHeight(fieldSystem->mapMatrix) * 32) {
        return FALSE;
    }
    u16 attributes;
    if (!fieldSystem->terrainCollisionMan->getTileAttributes(fieldSystem, nextX, nextZ, &attributes)) {
        return FALSE; // Unloaded map tiles are not walkable staging space.
    }
    // Collision queries read the origin tile from the object. Change only its
    // tile coordinates during this synchronous query, then restore them.
    int originalX = MapObject_GetX(object);
    int originalZ = MapObject_GetZ(object);
    u32 originalBehavior = MapObject_GetCurrTileBehavior(object);
    MapObject_SetX(object, x);
    MapObject_SetZ(object, z);
    MapObject_SetCurrTileBehavior(object, TerrainCollisionManager_GetTileBehavior(fieldSystem, x, z));
    u32 collision = sub_02063E18(object, source, nextX, source->y / (8 * FX32_ONE), nextZ, dir);
    MapObject_SetX(object, originalX);
    MapObject_SetZ(object, originalZ);
    MapObject_SetCurrTileBehavior(object, originalBehavior);
    if (collision & ~MAP_OBJ_COLLISION_OUT_OF_RANGE) {
        return FALSE;
    }
    *dest = *source;
    VecFx32_StepDirection(dir, dest, MAP_OBJECT_TILE_SIZE);
    u8 heightSource;
    dest->y = TerrainCollisionManager_GetHeight(fieldSystem, source->y, dest->x, dest->z, &heightSource);
    // This first version supports flat routes; stairs/bridges need authored paths.
    return heightSource != CALCULATED_HEIGHT_SOURCE_NONE && dest->y == source->y;
}

typedef struct {
    VecFx32 pos;
    s16 parent;
    u8 dir;
    u8 depth;
} NpcExitNode;

// Prefer the authored direction, but allow turns around walls and back onto
// a road. Entrances plan outward from the anchor and validate the return route
// in both directions before moving the object to its invisible starting tile.
static BOOL NpcScene_FindRoute(NpcSceneTask *scene, BOOL entrance)
{
    enum {
        SIDE = NPC_SCENE_MAX_TILES * 2 + 1,
        VISITED_COUNT = SIDE * SIDE,
        NODE_COUNT = 2 * NPC_SCENE_MAX_TILES * (NPC_SCENE_MAX_TILES + 1) + 1
    };
    u8 *visited = Heap_Alloc(HEAP_ID_FIELD1, VISITED_COUNT);
    GF_ASSERT(visited != NULL);
    memset(visited, 0, VISITED_COUNT);
    NpcExitNode *nodes = Heap_Alloc(HEAP_ID_FIELD1, sizeof(*nodes) * NODE_COUNT);
    GF_ASSERT(nodes != NULL);
    VecFx32 origin = *MapObject_GetPos(scene->object);
    VecFx32 renderOffset;
    ov5_021ECDA0(scene->object, &renderOffset);
    VEC_Subtract(&renderOffset, &origin, &renderOffset);
    int preferred = entrance ? Direction_GetOpposite(scene->dir) : scene->dir;
    int dirs[4] = { preferred, DIR_NORTH, DIR_SOUTH, Direction_GetOpposite(preferred) };
    if (preferred == DIR_NORTH || preferred == DIR_SOUTH) {
        dirs[1] = DIR_WEST;
        dirs[2] = DIR_EAST;
    }
    nodes[0] = (NpcExitNode) { .pos = origin, .parent = -1 };
    visited[NPC_SCENE_MAX_TILES * SIDE + NPC_SCENE_MAX_TILES] = TRUE;
    int count = 1;
    BOOL found = FALSE;
    for (int head = 0; head < count && !found; head++) {
        if (nodes[head].depth >= scene->remaining || nodes[head].depth >= NPC_SCENE_MAX_TILES) {
            continue;
        }
        for (int i = 0; i < 4; i++) {
            VecFx32 dest = nodes[head].pos;
            VecFx32_StepDirection(dirs[i], &dest, MAP_OBJECT_TILE_SIZE);
            int dx = (dest.x - origin.x) / MAP_OBJECT_TILE_SIZE;
            int dz = (dest.z - origin.z) / MAP_OBJECT_TILE_SIZE;
            int index = (dz + NPC_SCENE_MAX_TILES) * SIDE + dx + NPC_SCENE_MAX_TILES;
            if (visited[index] || !NpcScene_SafeStep(scene, &nodes[head].pos, dirs[i], &dest)) {
                continue;
            }
            VecFx32 reverse;
            if (entrance && !NpcScene_SafeStep(scene, &dest, Direction_GetOpposite(dirs[i]), &reverse)) {
                continue;
            }
            visited[index] = TRUE;
            GF_ASSERT(count < NODE_COUNT);
            int next = count++;
            nodes[next] = (NpcExitNode) { .pos = dest, .parent = head, .dir = dirs[i], .depth = nodes[head].depth + 1 };
            VecFx32 renderPos;
            VEC_Add(&dest, &renderOffset, &renderPos);
            int forward = dx * MapObject_GetDxFromDir(scene->dir) + dz * MapObject_GetDzFromDir(scene->dir);
            int sideways = dx * MapObject_GetDzFromDir(scene->dir) - dz * MapObject_GetDxFromDir(scene->dir);
            BOOL departureWaypoint = !entrance && scene->minimumSteps > 0 && forward >= 2 && sideways == 0;
            if (!NpcScene_PositionVisible(scene->fieldSystem, &renderPos) || departureWaypoint) {
                scene->pathCount = nodes[next].depth;
                scene->pathIndex = 0;
                for (int step = scene->pathCount - 1; step >= 0; step--) {
                    scene->path[entrance ? scene->pathCount - 1 - step : step] = entrance ? Direction_GetOpposite(nodes[next].dir) : nodes[next].dir;
                    next = nodes[next].parent;
                }
                if (entrance) {
                    scene->remaining = scene->pathCount;
                    MapObject_SetPosDirFromVec(scene->object, &dest, scene->path[0]);
                    sub_02062B68(scene->object);
                }
                found = TRUE;
                break;
            }
        }
    }
    Heap_Free(nodes);
    Heap_Free(visited);
    return found;
}

static void NpcScene_UseNativeView(NpcSceneTask *scene)
{
    if (!scene->nativeView) {
        scene->nativeView = TRUE;
        scene->nativeFrame = scene->fieldSystem->npcSceneRenderFrame;
        ScriptManager_UseNativeNpcView(scene->fieldSystem);
    }
}

static void NpcScene_Finish(SysTask *task, NpcSceneTask *scene)
{
    if (scene->exiting && !scene->keepObject) {
        MapObject_SetFlagAndDeleteObject(scene->object);
    }
    u8 *movementCount = FieldSystem_GetScriptMemberPtr(scene->fieldSystem, SCRIPT_MANAGER_MOVEMENT_COUNT);
    GF_ASSERT(*movementCount > 0);
    (*movementCount)--;
    Heap_Free(scene);
    SysTask_Done(task);
}

static void NpcScene_Update(SysTask *task, void *param)
{
    NpcSceneTask *scene = param;
    if (scene->animation != NULL) {
        if (!MapObject_HasAnimationEnded(scene->animation)) {
            return;
        }
        MapObject_FinishAnimation(scene->animation);
        scene->animation = NULL;
        if (scene->prefixActive) {
            scene->prefixActive = FALSE;
            scene->prefixIndex++;
        } else if (scene->nativeWalking) {
            scene->minimumSteps = 0;
            scene->remaining = 0;
        } else {
            scene->remaining--;
            if (scene->minimumSteps > 0) {
                scene->minimumSteps--;
            }
            if (scene->pathCount != 0) {
                scene->pathIndex++;
            }
        }
    }
    if (scene->nativeView) {
        // Render the narrow view before changing object lifetime/position.
        // ScriptManager holds that view until the whole script ends.
        if (scene->fieldSystem->npcSceneRenderFrame == scene->nativeFrame) {
            return;
        }
        if (scene->keepObject && scene->minimumSteps > 0) {
            // Complete the remaining authored leg with native script behavior
            // only if no safe bounded route exists in the widescreen view.
            scene->commands[0].movementAction = MovementAction_TurnActionTowardsDir(scene->dir, scene->commands[0].movementAction);
            scene->commands[0].count = scene->minimumSteps;
            scene->nativeWalking = TRUE;
            scene->animation = MapObject_StartAnimation(scene->object, scene->commands);
            return;
        }
        if (!scene->exiting) {
            // A route invalidated during extra travel must still hand the
            // original script its original anchor, never a partial approach.
            MapObject_SetPosDirFromVec(scene->object, &scene->anchor, MapObject_GetInitialDir(scene->object));
            sub_02062B68(scene->object);
        }
        NpcScene_Finish(task, scene);
        return;
    }
    if (scene->prefixIndex < scene->prefixCount) {
        scene->commands[0] = scene->prefix[scene->prefixIndex];
        scene->prefixActive = TRUE;
        scene->animation = MapObject_StartAnimation(scene->object, scene->commands);
        return;
    }
    if (scene->prefix != NULL) {
        scene->commands[0] = scene->prefix[scene->prefixCount];
        scene->commands[0].count = 1;
    }
    if (!scene->exiting && scene->remaining == 0) {
        MapObject_SetPosDirFromVec(scene->object, &scene->anchor, scene->dir);
        sub_02062B68(scene->object);
        NpcScene_Finish(task, scene);
        return;
    }
    if (scene->exiting && scene->minimumSteps == 0 && !NpcScene_ObjectVisible(scene)) {
        NpcScene_Finish(task, scene);
        return;
    }
    if (scene->remaining == 0) {
        NpcScene_UseNativeView(scene);
        return;
    }
    VecFx32 dest;
    int dir = scene->pathIndex < scene->pathCount ? scene->path[scene->pathIndex] : scene->dir;
    if (!NpcScene_SafeStep(scene, MapObject_GetPos(scene->object), dir, &dest)) {
        if (!scene->exiting || !NpcScene_FindRoute(scene, FALSE)) {
            NpcScene_UseNativeView(scene);
            return;
        }
        dir = scene->path[scene->pathIndex];
    }
    scene->commands[0].movementAction = MovementAction_TurnActionTowardsDir(dir, scene->commands[0].movementAction);
    scene->animation = MapObject_StartAnimation(scene->object, scene->commands);
}

static BOOL NpcScene_Start(FieldSystem *fieldSystem, MapObject *object, u16 action, u16 maxTiles, BOOL exiting, const MapObjectAnimCmd *departure, int prefixCount, BOOL retain)
{
    if (fieldSystem->npcSceneViewWidth == HW_LCD_WIDTH && fieldSystem->npcSceneNativeViewLocks == 0) {
        return FALSE; // Preserve native-width movement and timing exactly.
    }
    NpcSceneTask *scene = Heap_Alloc(HEAP_ID_FIELD1, sizeof(*scene));
    GF_ASSERT(scene != NULL);
    memset(scene, 0, sizeof(*scene));
    scene->fieldSystem = fieldSystem;
    scene->object = object;
    scene->dir = NpcScene_WalkDirection(action);
    scene->exiting = exiting;
    scene->keepObject = retain;
    scene->anchor = *MapObject_GetPos(object);
    scene->remaining = maxTiles < NPC_SCENE_MAX_TILES ? maxTiles : NPC_SCENE_MAX_TILES;
    if (departure != NULL) {
        scene->keepObject = TRUE;
        scene->prefix = departure;
        scene->prefixCount = prefixCount;
        scene->minimumSteps = departure[prefixCount].count;
        scene->remaining += scene->minimumSteps;
    }
    scene->commands[0].movementAction = action;
    scene->commands[0].count = 1;
    scene->commands[1].movementAction = MOVEMENT_ACTION_END;
    if (scene->dir == DIR_NONE || !NpcScene_OrdinaryBillboard(object) || fieldSystem->npcSceneViewWidth == 0 || fieldSystem->npcSceneNativeViewLocks != 0) {
        NpcScene_UseNativeView(scene);
    } else if (!exiting && NpcScene_ObjectVisible(scene)) {
        // Keep a straight approach where possible to preserve its choreography.
        VecFx32 anchor = *MapObject_GetPos(object);
        VecFx32 spawn = anchor;
        VecFx32 renderOffset;
        ov5_021ECDA0(object, &renderOffset);
        VEC_Subtract(&renderOffset, &anchor, &renderOffset);
        int steps = 0;
        BOOL straight = FALSE;
        while (TRUE) {
            VecFx32 renderPos;
            VEC_Add(&spawn, &renderOffset, &renderPos);
            if (!NpcScene_PositionVisible(fieldSystem, &renderPos)) {
                straight = TRUE;
                break;
            }
            VecFx32 next, reverse;
            int opposite = Direction_GetOpposite(scene->dir);
            if (steps == scene->remaining || !NpcScene_SafeStep(scene, &spawn, opposite, &next) || !NpcScene_SafeStep(scene, &next, scene->dir, &reverse)) {
                break;
            }
            spawn = next;
            steps++;
        }
        if (straight) {
            scene->remaining = steps;
            MapObject_SetPosDirFromVec(object, &spawn, scene->dir);
            sub_02062B68(object);
        } else if (!NpcScene_FindRoute(scene, TRUE)) {
            NpcScene_UseNativeView(scene);
        }
    } else if (!exiting) {
        scene->remaining = 0;
    }
    if (!scene->nativeView && !NpcScene_ObjectVisible(scene) && exiting && !scene->keepObject) {
        Heap_Free(scene);
        return FALSE;
    }
    if (!scene->nativeView && !exiting && scene->remaining == 0) {
        Heap_Free(scene);
        return FALSE;
    }
    u8 *movementCount = FieldSystem_GetScriptMemberPtr(fieldSystem, SCRIPT_MANAGER_MOVEMENT_COUNT);
    (*movementCount)++;
    SysTask_Start(NpcScene_Update, scene, 0);
    return TRUE;
}

BOOL ScriptedNpcScene_Start(FieldSystem *fieldSystem, MapObject *object, u16 action, u16 maxTiles, BOOL exiting)
{
    return NpcScene_Start(fieldSystem, object, action, maxTiles, exiting, NULL, 0, FALSE);
}

BOOL ScriptedNpcScene_StartDeparture(FieldSystem *fieldSystem, MapObject *object, const MapObjectAnimCmd *commands)
{
    if (fieldSystem->npcSceneViewWidth <= HW_LCD_WIDTH || fieldSystem->npcSceneNativeViewLocks != 0 || !NpcScene_OrdinaryBillboard(object)) {
        return FALSE;
    }
    // This opt-in command protects the final walking leg. Keep the original
    // bytecode for unsupported lists, and bound scanning independently of it.
    for (int i = 0; i < 64; i++) {
        if (commands[i].movementAction == MOVEMENT_ACTION_END) {
            if (i == 0 || commands[i - 1].count == 0 || NpcScene_WalkDirection(commands[i - 1].movementAction) == DIR_NONE) {
                return FALSE;
            }
            return NpcScene_Start(fieldSystem, object, commands[i - 1].movementAction, NPC_SCENE_MAX_TILES, TRUE, commands, i - 1, TRUE);
        }
    }
    return FALSE;
}

BOOL ScriptedNpcScene_StartRetainedExit(FieldSystem *fieldSystem, MapObject *object, u16 action, u16 maxTiles)
{
    return NpcScene_Start(fieldSystem, object, action, maxTiles, TRUE, NULL, 0, TRUE);
}

BOOL ScriptedNpcScene_ObjectVisible(FieldSystem *fieldSystem, MapObject *object)
{
    if (fieldSystem->npcSceneNativeViewLocks != 0) {
        return TRUE;
    }
    VecFx32 pos;
    ov5_021ECDA0(object, &pos);
    return NpcScene_PositionVisible(fieldSystem, &pos);
}
