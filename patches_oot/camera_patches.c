/*
 * Analog ("free") camera for Ocarina of Time, ported from the Majora's Mask
 * implementation in patches/camera_patches.c.
 *
 * The runtime half is already shared and game agnostic: recomp_get_camera_inputs,
 * recomp_get_analog_cam_enabled and recomp_get_analog_inverted_axes all read the
 * right stick and the config menu, and know nothing about which game is running.
 * Only the game-side hooks below had to be ported.
 *
 * The two games' cameras share an ancestor, so the port is close to mechanical:
 * OoT's Camera_Normal1 computes an `eyeAdjustment` VecGeo exactly where MM's
 * computes `spB4`, and both finish by adding it to `at` to get the new eye
 * position. The names differ (Camera_AddVecGeoToVec3f vs OLib_AddVecGeoToVec3f,
 * focusActor vs lockOnActor) but the structure does not.
 *
 * Camera_Normal1 and Play_Main below are verbatim copies of the decomp originals
 * with the lines marked @recomp added, because RECOMP_PATCH replaces the whole
 * function. They are generated rather than transcribed; regenerate with
 * scratchpad/gen_camera_patch.py if the decomp is updated.
 */
#include "patches.h"

// OoT's decomp has no monolithic global.h; these mirror what z_camera.c itself
// includes, trimmed to what the copied function bodies actually reference.
#include "libc64/math64.h"
#include "libc64/qrand.h"
#include "debug_display.h"
#include "save.h"
#include "array_count.h"
#include "attributes.h"
#include "camera.h"
#include "controller.h"
#include "gfx.h"
#include "letterbox.h"
#include "olib.h"
#include "play_state.h"
#include "player.h"
#include "quake.h"
#include "regs.h"
#include "sfx.h"
#include "sys_math3d.h"
#include "ultra64.h"
#include "sys_math.h"
#include "z_lib.h"
#include "z_math.h"

// Play_Main's dependencies. PLAY_LOG is a debug-only macro in z_play.c that
// compiles to nothing with DEBUG_FEATURES=0, which is how the patches build.
#define PLAY_LOG(line) (void)0

extern Input* D_8012D1F8;
void Play_Update(PlayState* this);
void Play_Draw(PlayState* this);
void debug_warp_update(PlayState* play);

// Copied from z_camera_data.inc.c, which is #included into z_camera.c rather than
// being a header, so these types are not reachable any other way.
typedef struct CameraModeValue {
    s16 val;
    s16 dataType;
} CameraModeValue;

typedef struct CameraMode {
    s16 funcIdx;
    s16 valueCnt;
    CameraModeValue* values;
} CameraMode;

typedef struct CameraSetting {
    union {
        u32 unk_00;
        struct {
            u32 unk_bit0 : 1;
            u32 unk_bit1 : 1;
            u32 validModes : 30;
        };
    };
    CameraMode* cameraModes;
} CameraSetting;

// Camera internals that z_camera.c never exposes in a header. Declared here so
// the copied Camera_Normal1 body links against the base game's versions; the
// recompiler resolves them by name.
f32 Camera_LERPCeilF(f32 target, f32 cur, f32 stepScale, f32 minDiff);
s16 Camera_LERPCeilS(s16 target, s16 cur, f32 stepScale, s16 minDiff);
Vec3f Camera_AddVecGeoToVec3f(Vec3f* a, VecGeo* geo);
Vec3f Camera_CalcUpFromPitchYawRoll(s16 pitch, s16 yaw, s16 roll);
void View_LookAt(View* view, Vec3f* eye, Vec3f* at, Vec3f* up);
s32 Camera_BGCheck(Camera* camera, Vec3f* from, Vec3f* to);
s16 Camera_GetPitchAdjFromFloorHeightDiffs(Camera* camera, s16 viewYaw, s16 initAndReturnZero);
f32 Camera_ClampLERPScale(Camera* camera, f32 maxLERPScale);
s32 Camera_CopyPREGToModeValues(Camera* camera);
s32 Camera_CalcAtDefault(Camera* camera, VecGeo* eyeAtDir, f32 yOffset, s16 calcSlopeYAdj);
f32 Camera_ClampDist(Camera* camera, f32 dist, f32 minDist, f32 maxDist, s16 timer);
s16 Camera_CalcDefaultPitch(Camera* camera, s16 arg1, s16 arg2, s16 arg3);
s16 Camera_CalcDefaultYaw(Camera* camera, s16 cur, s16 target, f32 arg3, f32 accel);
s32 func_800458D4(Camera* camera, VecGeo* eyeAtDir, f32 yOffset, f32* arg3, s16 calcSlopeYAdj);
s32 func_80045B08(Camera* camera, VecGeo* eyeAtDir, f32 yOffset, s16 arg3);
void func_80046E20(Camera* camera, VecGeo* eyeAdjustment, f32 minDist, f32 arg3, f32* arg4, SwingAnimation* anim);

// Defined in z_camera_data.inc.c. Ordinary globals rather than statics, so the
// recompiler resolves these against the base game by name.
extern CameraSetting sCameraSettings[];
extern s32 sCameraInterfaceField;
extern s32 sUpdateCameraDirection;

// Provided by the runtime; see patches_oot/syms.ld.
s32 recomp_get_analog_cam_enabled(void);
void recomp_get_camera_inputs(f32* x, f32* y);
void recomp_get_analog_inverted_axes(s32* x, s32* y);
s32 recomp_get_targeting_mode(void);
void force_camera_skip_interpolation(void);


static VecGeo analog_camera_pos = { .r = 66.0f, .pitch = 0, .yaw = 0 };

// Degrees per frame at full stick deflection, in binang. The vertical axis is
// deliberately slower than the horizontal, matching MM's tuning.
static const f32 analog_camera_x_sensitivity = 1500.0f;
static const f32 analog_camera_y_sensitivity = 500.0f;

// Pitch limits, matching the clamp the vanilla camera applies to eyeAdjustment
// just above the injection point in Camera_Normal1.
#define ANALOG_CAM_PITCH_MAX 0x38A4
#define ANALOG_CAM_PITCH_MIN -0x3C8C

// True while the analog camera is driving. It is not an "is the stick being
// pushed" flag: the camera stays engaged once it takes over, so releasing the
// stick leaves the view where the player put it instead of handing it back to the
// game's auto-follow. It only clears where the game legitimately owns the camera,
// and clearing it is what triggers the resync below.
static bool analog_cam_engaged = false;

// B37, third round: frames the analog camera keeps its hands off after a
// crawlspace exit. The interpolation skip below hides the cut itself, but the
// vanilla camera then spends its first several frames swinging from the tunnel
// mouth back behind Link, and the analog camera latching on during that swing is
// what still read as a lurch in play. Held at full while the scripted crawl move
// is still playing (control is not back yet), then counted down, so the free
// camera only takes over once the player has had control for the whole window
// and the vanilla camera has long since settled.
#define CRAWL_EXIT_HOLDOFF_FRAMES 40 // two seconds at the 20Hz game rate
static s32 crawl_exit_holdoff = 0;

void analog_cam_post_play_update(PlayState* play) {
    Camera* cam = play->cameraPtrs[play->activeCamId];

    if (cam == NULL) {
        return;
    }

    // B37: entering or leaving a crawlspace is a hard cut - the eye teleports
    // through the tunnel mouth on the frame the setting changes - but the
    // heuristic in camera_transform_tagging.c reads it as an orbit: the at stays
    // on Link, so the swing rule (at_dist <= 10, eye_dist <= 300) answers
    // "interpolate" and the cut smears across the frame, sweeping the view
    // through the wall. The transition is identifiable exactly from the setting
    // change, so tell the tagger directly instead of tuning its thresholds.
    // Tracked before the analog-cam gate because the cut is vanilla's, present
    // with the free camera off as well.
    // A window rather than the single transition frame: the new camera function
    // repositions over its first few runs, not in one write, so the frames just
    // after the setting change still carry most of the jump. Play testing showed
    // the one-frame skip shortening the smear without removing it.
    {
        static s16 prev_camera_setting = CAM_SET_NONE;
        static s32 skip_frames = 0;
        s32 is_crawl = (cam->setting == CAM_SET_CRAWLSPACE) || (cam->setting == CAM_SET_PIVOT_CRAWLSPACE);
        s32 was_crawl =
            (prev_camera_setting == CAM_SET_CRAWLSPACE) || (prev_camera_setting == CAM_SET_PIVOT_CRAWLSPACE);

        if (is_crawl != was_crawl) {
            skip_frames = 4;
        }
        if (was_crawl && !is_crawl) {
            crawl_exit_holdoff = CRAWL_EXIT_HOLDOFF_FRAMES;
        }
        if (skip_frames > 0) {
            skip_frames--;
            force_camera_skip_interpolation();
        }
        prev_camera_setting = cam->setting;
    }

    // Runs after the whole camera update for the frame and before anything draws,
    // so overriding here covers every camera function uniformly. That is the point
    // of doing it at this level rather than injecting into individual camera modes:
    // OoT dispatches through roughly thirty of them and switches between them
    // constantly during normal play, so anything less than full coverage leaves the
    // view and the controls disagreeing whenever an unpatched mode takes over.
    if (!recomp_get_analog_cam_enabled()) {
        analog_cam_engaged = false;
        return;
    }

    Player* player = GET_PLAYER(play);
    s32 targeting = (player->stateFlags1 & (PLAYER_STATE1_Z_TARGETING | PLAYER_STATE1_PARALLEL)) ||
                    (player->focusActor != NULL);

    // A room drawn as a prerendered image has no geometry behind it: the
    // background is a single picture shot from one fixed viewpoint, so moving the
    // camera slides the world off the backdrop it is painted on. The vanilla
    // camera never moves in these rooms for that reason. Hyrule Market is the
    // obvious case, but this covers every one of them rather than naming scenes.
    RoomShape* room_shape = play->roomCtx.curRoom.roomShape;
    s32 prerendered_background = (room_shape != NULL) && (room_shape->base.type == ROOM_SHAPE_TYPE_IMAGE);

    // The room shape alone is not enough. A camera setting can be one of the
    // fixed or pivoting kinds while the room is ordinary geometry - crawlspaces,
    // shop browsing, the market balcony, the castle courtyard side-scroll - and
    // in all of them the vanilla camera is pinned deliberately. Letting the
    // analog camera drive there ranges from wrong to unrecoverable, so the whole
    // family hands control back and the map behaves exactly as it does in
    // vanilla.
    s32 fixed_setting;
    s32 aiming_mode;

    switch (cam->setting) {
        case CAM_SET_PREREND_FIXED:
        case CAM_SET_PREREND_PIVOT:
        case CAM_SET_PREREND_SIDE_SCROLL:
        case CAM_SET_MARKET_BALCONY:
        case CAM_SET_CHU_BOWLING:
        case CAM_SET_PIVOT_CRAWLSPACE:
        case CAM_SET_PIVOT_SHOP_BROWSING:
        case CAM_SET_PIVOT_IN_FRONT:
        case CAM_SET_PIVOT_CORNER:
        case CAM_SET_PIVOT_WATER_SURFACE:
        case CAM_SET_CRAWLSPACE:
        // Door transitions and the turn-around shot keep their setting for a few
        // frames after the scripted move ends, so cover the tail here as well as
        // the move itself (which the scripted_player check below handles).
        case CAM_SET_DOORC:
        case CAM_SET_TURN_AROUND:
            fixed_setting = true;
            break;

        default:
            fixed_setting = false;
            break;
    }

    /*
     * First-person and weapon aiming are complete camera modes, not normal
     * third-person views with a different distance. In particular Player_Draw
     * chooses its head-hiding limb callback from the view-projection matrix that
     * vanilla built during Play_Update. Moving the eye here afterwards makes the
     * rendered camera enter Link's head while that test still sees the old view,
     * exposing his hair around C-Up first person. It also makes the view ray and
     * the child Bow's launch setup disagree. Leave every deliberate aiming mode
     * entirely to vanilla; the free camera resumes after the mode ends.
     */
    switch (cam->mode) {
        case CAM_MODE_FIRST_PERSON:
        case CAM_MODE_AIM_ADULT:
        case CAM_MODE_Z_AIM:
        case CAM_MODE_AIM_BOOMERANG:
        case CAM_MODE_AIM_CHILD:
            aiming_mode = true;
            break;

        default:
            aiming_mode = false;
            break;
    }

    // Scripted moves that take control of Link - opening a chest, walking
    // through a door, receiving an item - never go through csCtx, so the
    // cutscene-state check below does not see them. Player_InCsMode is the
    // game's own notion of "Link is being puppeted right now", which covers the
    // whole family at once instead of naming sequences. The crawl flag is the
    // one scripted move that predates csAction: crawlspace entry starts a few
    // frames before the camera setting flips to CAM_SET_CRAWLSPACE, and
    // PLAYER_STATE2_CRAWLING spans the entire move, entry included.
    s32 scripted_player = Player_InCsMode(play) || (player->stateFlags2 & PLAYER_STATE2_CRAWLING);

    // OnePoint cutscenes - chest lids, door unlocks, switch reveals - run on a
    // sub-camera: activeCamId moves off the main camera, and the sub-camera's
    // own status is ACTIVE, so the status check below passes on exactly the
    // frames the game is most deliberately framing something. Any frame the
    // main camera is not the one driving is a frame the game owns.
    s32 sub_camera = (play->activeCamId != CAM_ID_MAIN);

    // Hand the camera back wherever the game is deliberately framing something:
    // cutscenes (scripted or player-held), sub-cameras, the pause background,
    // lock-on, prerendered rooms, and any camera the game has not marked active.
    // Everywhere else the analog camera stays in control.
    if ((play->pauseCtx.state != PAUSE_STATE_OFF) || (R_PAUSE_BG_PRERENDER_STATE != PAUSE_BG_PRERENDER_OFF) ||
        (play->csCtx.state != CS_STATE_IDLE) || (cam->status != CAM_STAT_ACTIVE) || targeting ||
        prerendered_background || fixed_setting || aiming_mode || scripted_player || sub_camera) {
        analog_cam_engaged = false;
        return;
    }

    // B37: the crawlspace-exit hold-off. Reaching this point means the camera
    // setting is back to normal, but the vanilla camera is still flying home and
    // the exit animation may still be running. Restart the window while the
    // scripted crawl move holds the player (PLAYER_STATE2_CRAWLING covers the
    // whole move, exit animation included), so the countdown only runs on frames
    // the player actually has control.
    if (crawl_exit_holdoff > 0) {
        if (player->stateFlags2 & PLAYER_STATE2_CRAWLING) {
            crawl_exit_holdoff = CRAWL_EXIT_HOLDOFF_FRAMES;
        } else {
            crawl_exit_holdoff--;
        }
        analog_cam_engaged = false;
        return;
    }

    // Resync on the frame control is regained, never afterwards. Re-deriving the
    // angles every frame would fight the player's input, and only resyncing on a
    // stick threshold is what made the camera visibly lurch the first time the
    // stick was touched.
    //
    // This must be OLib_Vec3fDiffToVecGeo rather than a hand-rolled Math_Atan2S
    // plus Math_Vec3f_Pitch: it is the exact inverse of Camera_AddVecGeoToVec3f
    // below, and is what the vanilla camera uses to build eyeAdjustment. Deriving
    // the angles any other way puts the branch cut somewhere else, so the round
    // trip is inexact and the yaw snaps as it crosses the seam - most visibly at
    // half a turn, where the two conventions disagree by a full revolution.
    if (!analog_cam_engaged) {
        analog_camera_pos = OLib_Vec3fDiffToVecGeo(&cam->at, &cam->eye);
        analog_cam_engaged = true;
    }

    f32 input_x;
    f32 input_y;

    recomp_get_camera_inputs(&input_x, &input_y);

    s32 inverted_x;
    s32 inverted_y;

    recomp_get_analog_inverted_axes(&inverted_x, &inverted_y);

    if (inverted_x) {
        input_x = -input_x;
    }

    if (inverted_y) {
        input_y = -input_y;
    }

    analog_camera_pos.yaw += (s16)(-input_x * analog_camera_x_sensitivity);
    analog_camera_pos.pitch += (s16)(input_y * analog_camera_y_sensitivity);

    if (analog_camera_pos.pitch > ANALOG_CAM_PITCH_MAX) {
        analog_camera_pos.pitch = ANALOG_CAM_PITCH_MAX;
    }

    if (analog_camera_pos.pitch < ANALOG_CAM_PITCH_MIN) {
        analog_camera_pos.pitch = ANALOG_CAM_PITCH_MIN;
    }

    // Follow the distance the active mode wants, so zoom, boss framing and lock-on
    // still come from the game. Stepped rather than assigned: cam->dist is a LERP
    // target that can sit some way from the camera's actual radius, and snapping
    // straight to it lurches on the frame the analog camera takes over.
    analog_camera_pos.r += (cam->dist - analog_camera_pos.r) * 0.25f;

    Vec3f new_eye = Camera_AddVecGeoToVec3f(&cam->at, &analog_camera_pos);

    // Same wall handling the vanilla camera uses; Camera_BGCheck writes the
    // corrected position back through its `to` argument.
    Camera_BGCheck(cam, &cam->at, &new_eye);

    cam->eye = new_eye;
    cam->eyeNext = new_eye;

    // inputDir is what the game reads to decide which way "forward" is, so it has
    // to agree with the view or movement stays relative to the old camera. Vanilla
    // builds it as the eye->at direction, either directly as here or equivalently
    // as (-pitch, yaw - 0x7FFF) from the at->eye geo - see the two branches in
    // Camera_Normal1. Deriving it the same way rather than flipping
    // analog_camera_pos by hand keeps the convention identical to the game's, and
    // avoids the half-turn discontinuity that flipping the yaw without also
    // negating the pitch produced.
    //
    // Built from new_eye rather than analog_camera_pos so it describes where the
    // camera actually ended up: Camera_BGCheck may have pulled the eye in against a
    // wall, and the pre-collision angle would disagree with the rendered view.
    VecGeo eye_to_at = OLib_Vec3fDiffToVecGeo(&new_eye, &cam->at);

    cam->inputDir.x = eye_to_at.pitch;
    cam->inputDir.y = eye_to_at.yaw;
    cam->inputDir.z = 0;

    // Rebuild the view from the position just written.
    //
    // Camera_Update ends with its own View_LookAt, and it runs inside Play_Update -
    // before this function. Without the call below the frame is drawn from the
    // camera the vanilla code produced, and the angle set here only reaches the
    // screen a frame later, by way of eyeNext. In that gap Camera_Normal1 LERPs the
    // yaw towards swingYawTarget, which is the position behind the player, so what
    // is actually drawn is a blend of the player's input and the auto-follow rather
    // than the input alone.
    //
    // That is what made the camera unable to hold half a turn: half a turn is
    // exactly opposite the auto-follow target, the antipode of an angular LERP,
    // where the shortest path between the two is ambiguous and flips sign on tiny
    // changes. The result is a yaw that snaps between roughly +170 and -170 and
    // never settles at 180. Drawing from this position instead of the blended one
    // removes the fight entirely, rather than trying to damp it.
    //
    // Mirrors the tail of Camera_Update: the up vector comes from the same
    // eye->at angles and the camera's roll, and camDir carries that direction for
    // everything downstream that reads it.
    Vec3f view_up = Camera_CalcUpFromPitchYawRoll(eye_to_at.pitch, eye_to_at.yaw, cam->roll);

    cam->up = view_up;

    cam->camDir.x = eye_to_at.pitch;
    cam->camDir.y = eye_to_at.yaw;
    cam->camDir.z = 0;

    // quakeOffset is what Camera_Update added to the eye for screen shake, kept
    // here so earthquakes and impacts still register while the analog camera drives.
    Vec3f view_eye;

    view_eye.x = new_eye.x + cam->quakeOffset.x;
    view_eye.y = new_eye.y + cam->quakeOffset.y;
    view_eye.z = new_eye.z + cam->quakeOffset.z;

    View_LookAt(&play->view, &view_eye, &cam->at, &view_up);
}

#if PLATFORM_N64
#define CAMERA_CHECK_BTN(input, btn) PadUtils_CheckPressed((input), (btn))
#else
#define CAMERA_CHECK_BTN(input, btn) CHECK_BTN_ALL((input)->press.button, (btn))
#endif

#if DEBUG_FEATURES
s32 Camera_QRegInit(void);
#endif

#if DEBUG_FEATURES
#define CAM_DEBUG_RELOAD_PREG(camera)        \
    if (R_RELOAD_CAM_PARAMS) {               \
        Camera_CopyPREGToModeValues(camera); \
    }                                        \
    (void)0
#else
#define CAM_DEBUG_RELOAD_PREG(camera) (void)0
#endif

// Camera will reload its paramData. Usually that means setting the read-only data from what is stored in
// CameraModeValue arrays. Although sometimes some read-write data is reset as well
#define RELOAD_PARAMS(camera) (camera->animState == 0 || camera->animState == 10 || camera->animState == 20)

#if DEBUG_FEATURES
#define CAM_DEBUG_RELOAD_PARAMS R_RELOAD_CAM_PARAMS
#else
#define CAM_DEBUG_RELOAD_PARAMS true
#endif

/**
 * Camera data is stored in both read-only data and OREG as s16, and then converted to the appropriate type during
 * runtime. If a small f32 is being stored as an s16, it is common to store that value 100 times larger than the
 * original value. This is then scaled back down during runtime with the CAM_DATA_SCALED macro.
 */
#define CAM_DATA_SCALED(x) ((x)*0.01f)

// Load the next value from camera read-only data stored in CameraModeValue
#define GET_NEXT_RO_DATA(values) ((values++)->val)
// Load the next value and scale down from camera read-only data stored in CameraModeValue
#define GET_NEXT_SCALED_RO_DATA(values) CAM_DATA_SCALED(GET_NEXT_RO_DATA(values))

#if DEBUG_FEATURES

#define CAM_GLOBAL_0 OREG(0)
#define CAM_GLOBAL_1 OREG(1)
#define CAM_XZ_OFFSET_UPDATE_RATE CAM_DATA_SCALED(R_CAM_XZ_OFFSET_UPDATE_RATE)
#define CAM_Y_OFFSET_UPDATE_RATE CAM_DATA_SCALED(R_CAM_Y_OFFSET_UPDATE_RATE)
#define CAM_FOV_UPDATE_RATE CAM_DATA_SCALED(R_CAM_FOV_UPDATE_RATE)
#define CAM_MAX_PITCH R_CAM_MAX_PITCH
#define CAM_R_UPDATE_RATE_INV R_CAM_R_UPDATE_RATE_INV
#define CAM_PITCH_UPDATE_RATE_INV R_CAM_PITCH_UPDATE_RATE_INV
#define CAM_GLOBAL_8 CAM_DATA_SCALED(OREG(8))
#define CAM_SLOPE_Y_ADJ_AMOUNT R_CAM_SLOPE_Y_ADJ_AMOUNT
#define CAM_GLOBAL_10 CAM_DATA_SCALED(OREG(10))
#define CAM_GLOBAL_11 CAM_DATA_SCALED(OREG(11))
#define CAM_GLOBAL_12 CAM_DATA_SCALED(OREG(12))
#define CAM_GLOBAL_13 OREG(13)
#define CAM_GLOBAL_14 OREG(14)
#define CAM_GLOBAL_15 OREG(15)
#define CAM_GLOBAL_16 OREG(16)
#define CAM_PITCH_FLOOR_CHECK_NEAR_DIST_FAC CAM_DATA_SCALED(R_CAM_PITCH_FLOOR_CHECK_NEAR_DIST_FAC)
#define CAM_PITCH_FLOOR_CHECK_FAR_DIST_FAC CAM_DATA_SCALED(R_CAM_PITCH_FLOOR_CHECK_FAR_DIST_FAC)
#define CAM_PITCH_FLOOR_CHECK_OFFSET_Y_FAC CAM_DATA_SCALED(R_CAM_PITCH_FLOOR_CHECK_OFFSET_Y_FAC)
#define CAM_PITCH_FLOOR_CHECK_NEAR_WEIGHT CAM_DATA_SCALED(R_CAM_PITCH_FLOOR_CHECK_NEAR_WEIGHT)
#define CAM_GLOBAL_21 OREG(21)
#define CAM_GLOBAL_22 CAM_DATA_SCALED(OREG(22))
#define CAM_DEFAULT_ANIM_TIME R_CAM_DEFAULT_ANIM_TIME
#define CAM_GLOBAL_24 OREG(24)
#define CAM_UPDATE_RATE_STEP_SCALE_XZ CAM_DATA_SCALED(R_CAM_UPDATE_RATE_STEP_SCALE_XZ)
#define CAM_UPDATE_RATE_STEP_SCALE_Y CAM_DATA_SCALED(R_CAM_UPDATE_RATE_STEP_SCALE_Y)
#define CAM_GLOBAL_27 OREG(27)
#define CAM_GLOBAL_28 CAM_DATA_SCALED(OREG(28))
#define CAM_GLOBAL_29 CAM_DATA_SCALED(OREG(29))
#define CAM_GLOBAL_30 CAM_DATA_SCALED(OREG(30))
#define CAM_JUMP1_EYE_Y_STEP_SCALE CAM_DATA_SCALED(R_CAM_JUMP1_EYE_Y_STEP_SCALE)
#define CAM_GLOBAL_32 OREG(32)
#define CAM_GLOBAL_33 OREG(33)
#define CAM_MIN_PITCH_1 R_CAM_MIN_PITCH_1
#define CAM_MIN_PITCH_2 R_CAM_MIN_PITCH_2
#define CAM_BATTLE1_ROLL_TARGET_BASE R_CAM_BATTLE1_ROLL_TARGET_BASE
#define CAM_BATTLE1_ROLL_STEP_SCALE CAM_DATA_SCALED(R_CAM_BATTLE1_ROLL_STEP_SCALE)
#define CAM_GLOBAL_38 CAM_DATA_SCALED(OREG(38))
#define CAM_GLOBAL_39 CAM_DATA_SCALED(OREG(39))
#define CAM_BATTLE1_XYZ_OFFSET_UPDATE_RATE_TARGET CAM_DATA_SCALED(R_CAM_BATTLE1_XYZ_OFFSET_UPDATE_RATE_TARGET)
#define CAM_AT_LERP_STEP_SCALE_MIN CAM_DATA_SCALED(R_CAM_AT_LERP_STEP_SCALE_MIN)
#define CAM_AT_LERP_STEP_SCALE_FAC CAM_DATA_SCALED(R_CAM_AT_LERP_STEP_SCALE_FAC)
#define CAM_GLOBAL_43 CAM_DATA_SCALED(OREG(43))
#define CAM_GLOBAL_44 OREG(44)
#define CAM_GLOBAL_45 OREG(45)
#define CAM_YOFFSET_NORM CAM_DATA_SCALED(R_CAM_YOFFSET_NORM)
#define CAM_GLOBAL_47 OREG(47)
#define CAM_GLOBAL_48 CAM_DATA_SCALED(OREG(48))
#define CAM_GLOBAL_49 CAM_DATA_SCALED(OREG(49))
#define CAM_GLOBAL_50 OREG(50)
#define CAM_GLOBAL_51 OREG(51)
#define CAM_GLOBAL_52 OREG(52)

#else

#define CAM_GLOBAL_0 0
#define CAM_GLOBAL_1 1
#define CAM_XZ_OFFSET_UPDATE_RATE .05f
#define CAM_Y_OFFSET_UPDATE_RATE .05f
#define CAM_FOV_UPDATE_RATE .05f
#define CAM_MAX_PITCH 14500
#define CAM_R_UPDATE_RATE_INV 20
#define CAM_PITCH_UPDATE_RATE_INV 16
#define CAM_GLOBAL_8 1.5f
#define CAM_SLOPE_Y_ADJ_AMOUNT 25
#define CAM_GLOBAL_10 1.5f
#define CAM_GLOBAL_11 0.06f
#define CAM_GLOBAL_12 .10f
#define CAM_GLOBAL_13 10
#define CAM_GLOBAL_14 0
#define CAM_GLOBAL_15 0
#define CAM_GLOBAL_16 1
#define CAM_PITCH_FLOOR_CHECK_NEAR_DIST_FAC 1.0f
#define CAM_PITCH_FLOOR_CHECK_FAR_DIST_FAC 2.5f
#define CAM_PITCH_FLOOR_CHECK_OFFSET_Y_FAC 1.2f
#define CAM_PITCH_FLOOR_CHECK_NEAR_WEIGHT 0.8f
#define CAM_GLOBAL_21 30
#define CAM_GLOBAL_22 1.2f
#define CAM_DEFAULT_ANIM_TIME 4
#define CAM_GLOBAL_24 1
#define CAM_UPDATE_RATE_STEP_SCALE_XZ .50f
#define CAM_UPDATE_RATE_STEP_SCALE_Y .20f
#define CAM_GLOBAL_27 1800
#define CAM_GLOBAL_28 0.5f
#define CAM_GLOBAL_29 0.5f
#define CAM_GLOBAL_30 0.5f
#define CAM_JUMP1_EYE_Y_STEP_SCALE 0.2f
#define CAM_GLOBAL_32 20
#define CAM_GLOBAL_33 -10
#define CAM_MIN_PITCH_1 -5460
#define CAM_MIN_PITCH_2 -9100
#define CAM_BATTLE1_ROLL_TARGET_BASE -6
#define CAM_BATTLE1_ROLL_STEP_SCALE 0.08f
#define CAM_GLOBAL_38 0.15f
#define CAM_GLOBAL_39 0.75f
#define CAM_BATTLE1_XYZ_OFFSET_UPDATE_RATE_TARGET 0.6f
#define CAM_AT_LERP_STEP_SCALE_MIN 0.12f
#define CAM_AT_LERP_STEP_SCALE_FAC 1.1f
#define CAM_GLOBAL_43 0.4f
#define CAM_GLOBAL_44 50
#define CAM_GLOBAL_45 250
#define CAM_YOFFSET_NORM -0.1f
#define CAM_GLOBAL_47 30
#define CAM_GLOBAL_48 0.3f
#define CAM_GLOBAL_49 0.7f
#define CAM_GLOBAL_50 20
#define CAM_GLOBAL_51 20
#define CAM_GLOBAL_52 20

#endif

#define DISTORTION_HOT_ROOM (1 << 0)
#define DISTORTION_UNDERWATER_WEAK (1 << 1)
#define DISTORTION_UNDERWATER_MEDIUM (1 << 2)
#define DISTORTION_UNDERWATER_STRONG (1 << 3)
#define DISTORTION_UNDERWATER_FISHING (1 << 4)

#define CAM_REQUEST_SETTING_FORCE_CHANGE (1 << 0)
// If set, then any other setting requests on the same frame will skip a priority check
// and overwrite the request
#define CAM_REQUEST_SETTING_IGNORE_PRIORITY (1 << 1)
#define CAM_REQUEST_SETTING_PRESERVE_BG_CAM_INDEX (1 << 2)
#define CAM_REQUEST_SETTING_RESTORE_PREV_BG_CAM_INDEX (1 << 3)




RECOMP_PATCH void Play_Main(GameState* thisx) {
    PlayState* this = (PlayState*)thisx;

    D_8012D1F8 = &this->state.input[0];

    DebugDisplay_Init();

    PLAY_LOG(4556);

    if (DEBUG_FEATURES && (R_HREG_MODE == HREG_MODE_PLAY) && (R_PLAY_INIT != HREG_MODE_PLAY)) {
        R_PLAY_RUN_UPDATE = true;
        R_PLAY_RUN_DRAW = true;
        R_PLAY_DRAW_SKYBOX = true;
        R_PLAY_DRAW_ROOM_FLAGS = (ROOM_DRAW_OPA | ROOM_DRAW_XLU);
        R_PLAY_DRAW_ACTORS = true;
        R_PLAY_DRAW_LENS_FLARES = true;
        R_PLAY_DRAW_SCREEN_FILLS = true;
        R_PLAY_DRAW_SANDSTORM = true;
        R_PLAY_DRAW_OVERLAY_ELEMENTS = true;
        R_PLAY_DRAW_ENV_FLAGS = (PLAY_ENV_DRAW_SKYBOX_FILTERS | PLAY_ENV_DRAW_SUN_AND_MOON | PLAY_ENV_DRAW_LIGHTNING |
                                 PLAY_ENV_DRAW_LIGHTS);
        HREG(91) = 1; // reg is not used in this mode
        R_PLAY_DRAW_COVER_ELEMENTS = true;
        R_PLAY_DRAW_DEBUG_OBJECTS = true;
        R_PLAY_INIT = HREG_MODE_PLAY;
    }

    if (!DEBUG_FEATURES || (R_HREG_MODE != HREG_MODE_PLAY) || R_PLAY_RUN_UPDATE) {
        // @recomp F46 - the recomp menu's Targeting Mode, written into the save
        // option the game's own targeting reads, every frame like the Majora's
        // Mask tree's controls_play_update. Without it a save file's stored
        // setting (Switch, on a fresh file) is all this game ever obeys.
        gSaveContext.zTargetSetting = recomp_get_targeting_mode();

        Play_Update(this);

        // @recomp Runs once per frame after the camera has been updated, which is
        // where the analog camera latches onto the game camera's current
        // orientation while it is inactive.
        analog_cam_post_play_update(this);

        // @recomp Last, because it is the one hook here that can end this play
        // state: the debug menu's warp re-enters Play_Init from the top, so
        // anything after it would be acting on a frame that is being thrown away.
        debug_warp_update(this);
    }

    PLAY_LOG(4583);

    Play_Draw(this);

    PLAY_LOG(4587);
}
