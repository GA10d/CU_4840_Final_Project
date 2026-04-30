#ifndef FIGHTER_GAME_H
#define FIGHTER_GAME_H

/*
 * 游戏核心状态与规则接口。
 *
 * 本头文件只描述“游戏世界”：玩家、攻击、投射物、菜单和胜负。输入设备、
 * 渲染方式、音频后端都在其他模块中处理。
 */

#include <stdint.h>

#include "fighter_audio.h"
#include "fighter_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIGHTER_PLAYER_COUNT 2

typedef enum {
  FIGHTER_GAME_STATE_MENU = 0,
  FIGHTER_GAME_STATE_PLAYING = 1,
  FIGHTER_GAME_STATE_GAME_OVER = 2
} fighter_game_state_t;

typedef enum {
  FIGHTER_CHARACTER_RYU = 0,
  FIGHTER_CHARACTER_KEN = 1
} fighter_character_id_t;

typedef enum {
  FIGHTER_VISUAL_STATE_IDLE = 0,
  FIGHTER_VISUAL_STATE_WALK,
  FIGHTER_VISUAL_STATE_JUMP,
  FIGHTER_VISUAL_STATE_CROUCH,
  FIGHTER_VISUAL_STATE_GUARD,
  FIGHTER_VISUAL_STATE_ATTACK,
  FIGHTER_VISUAL_STATE_HIT,
  FIGHTER_VISUAL_STATE_BLOCK_STUN,
  FIGHTER_VISUAL_STATE_KO,
  FIGHTER_VISUAL_STATE_VICTORY,
  FIGHTER_VISUAL_STATE_CROUCH_GUARD
} fighter_visual_state_t;

typedef enum {
  FIGHTER_ATTACK_PHASE_NONE = 0,
  FIGHTER_ATTACK_PHASE_STARTUP,
  FIGHTER_ATTACK_PHASE_ACTIVE,
  FIGHTER_ATTACK_PHASE_HIT_CONFIRM,
  FIGHTER_ATTACK_PHASE_BLOCK_CONFIRM,
  FIGHTER_ATTACK_PHASE_RECOVERY
} fighter_attack_phase_t;

typedef enum {
  FIGHTER_COMBAT_RESULT_NONE = 0,
  FIGHTER_COMBAT_RESULT_HIT,
  FIGHTER_COMBAT_RESULT_BLOCKED,
  FIGHTER_COMBAT_RESULT_TRADE,
  FIGHTER_COMBAT_RESULT_WHIFF
} fighter_combat_result_t;

typedef enum {
  FIGHTER_WINNER_NONE = 0,
  FIGHTER_WINNER_PLAYER1 = 1,
  FIGHTER_WINNER_PLAYER2 = 2,
  FIGHTER_WINNER_DRAW = 3
} fighter_winner_t;

typedef enum {
  FIGHTER_FINISH_REASON_NONE = 0,
  FIGHTER_FINISH_REASON_KO,
  FIGHTER_FINISH_REASON_TIME_OUT,
  FIGHTER_FINISH_REASON_DOUBLE_KO,
  FIGHTER_FINISH_REASON_EXIT
} fighter_finish_reason_t;

enum {
  FIGHTER_PLAYER_EVENT_NONE         = 0,
  FIGHTER_PLAYER_EVENT_ATTACK_START = 1 << 0,
  FIGHTER_PLAYER_EVENT_HIT          = 1 << 1,
  FIGHTER_PLAYER_EVENT_BLOCK        = 1 << 2,
  FIGHTER_PLAYER_EVENT_LAND         = 1 << 3,
  FIGHTER_PLAYER_EVENT_KO           = 1 << 4
};

typedef struct {
  /* 可调规则参数，单位通常是像素或游戏帧。 */
  int screen_width;                /* 逻辑画布宽度，玩家移动和投射物边界都按它裁剪。 */
  int screen_height;               /* 逻辑画布高度，渲染器按它把坐标缩放到真实屏幕。 */
  int floor_y;                     /* 地面 y 坐标；玩家站立 y = floor_y - player_height。 */
  int player_width;                /* 玩家碰撞盒宽度，也是简易渲染 fallback 的角色宽度。 */
  int player_height;               /* 玩家碰撞盒高度，用于站地、碰撞和受击判定。 */
  int projectile_width;            /* 投射物碰撞盒宽度，用于火球命中/相消/出界判定。 */
  int projectile_height;           /* 投射物碰撞盒高度，用于火球与玩家 hurtbox 的垂直重叠。 */
  int projectile_speed;            /* 投射物每帧水平移动速度，方向由发射者 facing 决定。 */
  int walk_speed;                  /* 玩家地面左右移动速度，单位是逻辑像素/帧。 */
  int jump_velocity;               /* 起跳初速度，通常为负值，因为 y 轴向下增大。 */
  int gravity;                     /* 每帧施加到 vy 的重力加速度，使玩家落回地面。 */
  int max_hp;                      /* 每名玩家回合开始时的最大生命值。 */
  int round_duration_frames;       /* 回合总时长，单位为游戏帧；归零后按剩余血量判胜。 */
  int menu_anim_period_frames;     /* 菜单动画切帧周期，控制 menu frame 0/1 的切换频率。 */
  int game_over_anim_frames;       /* 结算画面等待帧数；达到后才接受确认返回菜单。 */
  int attack_cooldown_frames;      /* 攻击结束后的冷却时间，防止连续无间隔出招。 */
  int dragon_punch_lift_velocity;  /* 升龙拳触发时给玩家的向上速度。 */
  int attack_visual_frames;        /* 攻击视觉状态至少保持的帧数，避免动画过快消失。 */
  int hurt_visual_frames;          /* 受击/硬直视觉状态保持帧数，用于动画和反馈。 */
} fighter_game_config_t;

typedef struct {
  /* 单个玩家的完整逻辑状态，渲染器和动画系统都从这里取当前动作。 */
  int x;                           /* 玩家碰撞盒左上角 x 坐标，逻辑像素单位。 */
  int y;                           /* 玩家碰撞盒左上角 y 坐标，逻辑像素单位。 */
  int vx;                          /* 当前水平速度；动画层用它区分原地跳/前跳/后跳。 */
  int vy;                          /* 当前垂直速度；负值向上，正值向下。 */

  int hp;                          /* 当前生命值；降到 0 后触发 KO/结算。 */
  int facing;                      /* 朝向：通常 1 表示面向右，-1 表示面向左。 */

  int attack_cooldown_frames;      /* 攻击冷却剩余帧数，大于 0 时不能开始新攻击。 */
  int attack_visual_frames;        /* 攻击动画剩余保持帧数，用于 visual_state 选择。 */
  int hurt_visual_frames;          /* 受击动画剩余保持帧数，用于 visual_state 选择。 */
  int attack_phase_frames;         /* 当前攻击阶段剩余帧数，驱动攻击状态机推进。 */
  int attack_has_connected;        /* 当前这次攻击是否已经命中过，防止同一招多次扣血。 */
  int block_stun_frames;           /* 防御硬直剩余帧数，大于 0 时玩家控制被锁。 */

  fighter_attack_command_t last_attack;      /* 最近一次攻击类型，用来选择攻击动画和判定参数。 */
  fighter_attack_phase_t attack_phase;       /* 当前攻击阶段：前摇、活跃、确认、后摇等。 */
  fighter_combat_result_t combat_result;     /* 本帧/最近攻击结果：命中、防御、相打或挥空。 */
  fighter_visual_state_t visual_state;       /* 当前视觉状态，动画系统据此选择 clip。 */
  fighter_character_id_t character_id;       /* 当前角色：Ryu 或 Ken，用于选择素材和投射物动画。 */

  uint32_t event_flags;            /* 本帧事件位集合，如攻击开始、命中、格挡、落地、KO。 */
  uint32_t state_frame;            /* 当前 visual_state 已持续帧数，辅助动画/调试。 */
} fighter_player_state_t;

typedef struct {
  /* 投射物状态；active=0 时其他字段可以视为无效。 */
  int active;                      /* 是否存在有效投射物；0 表示该槽空闲。 */
  int owner_index;                 /* 发射者玩家索引，避免火球打到自己。 */
  int x;                           /* 投射物碰撞盒左上角 x 坐标。 */
  int y;                           /* 投射物碰撞盒左上角 y 坐标。 */
  int vx;                          /* 投射物水平速度，正负表示飞行方向。 */
  fighter_character_id_t character_id; /* 投射物所属角色，用于选择对应火球 sprite。 */
  uint32_t anim_ticks;             /* 投射物动画计时器，用于循环播放火球帧。 */
} fighter_projectile_state_t;

typedef struct {
  /* 整局游戏状态：菜单、两名玩家、计时器、胜负和投射物。 */
  fighter_game_config_t config;    /* 当前使用的规则配置；初始化后被 tick 逻辑读取。 */
  fighter_game_state_t state;      /* 顶层状态：菜单、对战中或结算画面。 */
  uint32_t frame_counter;          /* 全局帧计数，每 tick 增加，用于调试/节奏控制。 */
  uint32_t state_frames;           /* 当前顶层 state 已持续帧数，用于菜单/结算动画计时。 */
  uint32_t round_timer_frames;     /* 对战剩余帧数，归零后触发 time out 判定。 */
  fighter_winner_t winner;         /* 当前胜者；对战中通常为 NONE。 */
  fighter_finish_reason_t finish_reason; /* 回合结束原因：KO、时间到、双 KO 或退出。 */
  int menu_bgm_active;             /* 菜单 BGM 是否已经发出循环播放命令，避免重复触发。 */
  fighter_player_state_t players[FIGHTER_PLAYER_COUNT]; /* 两名玩家的完整状态数组。 */
  fighter_projectile_state_t projectiles[FIGHTER_PLAYER_COUNT]; /* 每名玩家最多一个投射物槽。 */
} fighter_game_t;

void fighter_game_config_default(fighter_game_config_t *config);
void fighter_game_init(fighter_game_t *game, const fighter_game_config_t *config);
void fighter_game_tick(fighter_game_t *game,
                       const fighter_player_result_t inputs[FIGHTER_PLAYER_COUNT],
                       fighter_audio_command_list_t *audio_commands);

int fighter_game_menu_animation_frame(const fighter_game_t *game);
int fighter_game_game_over_ready(const fighter_game_t *game);
int fighter_game_round_seconds_remaining(const fighter_game_t *game);

#ifdef __cplusplus
}
#endif

#endif
