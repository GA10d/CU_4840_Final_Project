#include "fighter_game.h"

#include <string.h>

typedef struct {
  int reach;
  int damage;
  int startup_frames;
  int active_frames;
  int recovery_frames;
  int hit_confirm_frames;
  int block_confirm_frames;
  int hit_stun_frames;
  int block_stun_frames;
} fighter_attack_profile_t;

static int fighter_clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}//用于防止数值越界，如hp， timer等

static int fighter_rects_overlap(int lhs_x,// left hand side upperLeft x axis
                                 int lhs_y,//y axis
                                 int lhs_w,//width
                                 int lhs_h,//height
                                 int rhs_x,
                                 int rhs_y,
                                 int rhs_w,
                                 int rhs_h) {
  return lhs_x < rhs_x + rhs_w && lhs_x + lhs_w > rhs_x &&
         lhs_y < rhs_y + rhs_h && lhs_y + lhs_h > rhs_y;
}//检测角色矩形碰撞箱有无重叠



static void fighter_projectile_reset(fighter_projectile_state_t *projectile) {
  if (!projectile) {
    return;
  }

  memset(projectile, 0, sizeof(*projectile));
  projectile->owner_index = -1;
}



static int fighter_player_ground_y(const fighter_game_t *game) {
  return game->config.floor_y - game->config.player_height;
}//返回正常状态下玩家站在地面上时的头顶y标



static int fighter_player_center_x(const fighter_game_t *game,
                                   const fighter_player_state_t *player) {
  return player->x + game->config.player_width / 2;
}//返回正常位置玩家x坐标



static int fighter_player_is_airborne(const fighter_game_t *game,
                                      const fighter_player_state_t *player) {
  return player->y < fighter_player_ground_y(game) || player->vy != 0;
}//检测是否滞空

static int fighter_player_vertical_overlap(const fighter_game_t *game,
                                           const fighter_player_state_t *lhs,
                                           const fighter_player_state_t *rhs) {
  int top;
  int bottom;
  int lhs_bottom;
  int rhs_bottom;

  if (!game || !lhs || !rhs) {
    return 0;
  }

  top = lhs->y > rhs->y ? lhs->y : rhs->y;
  lhs_bottom = lhs->y + game->config.player_height;
  rhs_bottom = rhs->y + game->config.player_height;
  bottom = lhs_bottom < rhs_bottom ? lhs_bottom : rhs_bottom;
  return bottom - top;
}//计算两个玩家在垂直方向上的重叠高度

static fighter_attack_profile_t fighter_attack_profile(
    fighter_attack_command_t attack) {
  switch (attack) {
    case FIGHTER_ATTACK_NORMAL:
      return (fighter_attack_profile_t){48, 12, 3, 2, 15, 2, 2, 8, 3};
    case FIGHTER_ATTACK_FIREBALL:
      return (fighter_attack_profile_t){96, 18, 5, 1, 35, 2, 2, 10, 4};
    case FIGHTER_ATTACK_DRAGON_PUNCH:
      return (fighter_attack_profile_t){56, 20, 4, 4, 36, 2, 3, 10, 4};
    case FIGHTER_ATTACK_JUMP_ATTACK:
      return (fighter_attack_profile_t){52, 14, 2, 3, 4, 2, 1, 8, 3};
    case FIGHTER_ATTACK_FORWARD_JUMP_ATTACK:
      return (fighter_attack_profile_t){64, 16, 2, 3, 4, 2, 1, 8, 3};
    case FIGHTER_ATTACK_BACK_JUMP_ATTACK:
      return (fighter_attack_profile_t){52, 12, 2, 2, 4, 2, 1, 8, 3};
    case FIGHTER_ATTACK_SWEEP:
      return (fighter_attack_profile_t){58, 15, 4, 3, 18, 2, 2, 9, 4};
    case FIGHTER_ATTACK_NONE:
    default:
      return (fighter_attack_profile_t){0, 0, 0, 0, 0, 0, 0, 0, 0};
  }
}






static int fighter_attack_chip_damage(fighter_attack_profile_t profile) {
  int damage = profile.damage / 3;

  if (damage < 1) {
    damage = 1;
  }
  return damage;
}//计算防御削减的血量取整





static void fighter_game_push_audio(fighter_audio_command_list_t *audio_commands,
                                    fighter_audio_command_type_t type,
                                    fighter_audio_track_t track) {
  if (!audio_commands) {
    return;
  }

  (void)fighter_audio_command_list_push(audio_commands, type, track);
}//把音频播放指令塞入本帧要发出的音频命令列表audio commands中







static void fighter_player_sync_attack_visual_frames(
    fighter_player_state_t *player) {
  if (!player) {
    return;
  }

  if (player->attack_phase == FIGHTER_ATTACK_PHASE_NONE) {
    player->attack_visual_frames = 0;
  } else {
    player->attack_visual_frames =
        player->attack_phase_frames > 0 ? player->attack_phase_frames : 1;
  }
}//同步动画显示用帧数，比如如果不处于攻击状态就把攻击视觉帧数设为0



static void fighter_player_interrupt_attack(fighter_player_state_t *player) {
  if (!player) {
    return;
  }

  player->attack_phase = FIGHTER_ATTACK_PHASE_NONE;
  player->attack_phase_frames = 0;
  player->attack_has_connected = 0;
  fighter_player_sync_attack_visual_frames(player);
}//打断玩家攻击状态




static void fighter_player_enter_attack_phase(
    fighter_player_state_t *player,
    fighter_attack_phase_t phase,
    int frames) {
  if (!player) {
    return;
  }

  player->attack_phase = phase;
  player->attack_phase_frames = frames > 0 ? frames : 0;
  fighter_player_sync_attack_visual_frames(player);
}//玩家进入攻击阶段，用于切换攻击的不同阶段


static fighter_attack_command_t fighter_normalize_attack_command(
    fighter_attack_command_t command) {
  if (command == FIGHTER_ATTACK_NONE) {
    return FIGHTER_ATTACK_NORMAL;
  }
  return command;
}//返回攻击指令

static void fighter_player_begin_attack(fighter_player_state_t *player,
                                        fighter_attack_command_t command) {
  fighter_attack_profile_t profile;

  if (!player) {
    return;
  }

  command = fighter_normalize_attack_command(command);
  profile = fighter_attack_profile(command);
  player->last_attack = command;
  player->attack_has_connected = 0;
  player->combat_result = FIGHTER_COMBAT_RESULT_NONE;
  player->event_flags |= FIGHTER_PLAYER_EVENT_ATTACK_START;
  fighter_player_enter_attack_phase(player, FIGHTER_ATTACK_PHASE_STARTUP,
                                    profile.startup_frames);
}//进入攻击的startup阶段，前摇

static void fighter_player_tick_attack_phase(fighter_player_state_t *player) {
  fighter_attack_profile_t profile;

  if (!player || player->attack_phase == FIGHTER_ATTACK_PHASE_NONE) {
    return;
  }

  if (player->attack_phase_frames > 0) {
    player->attack_phase_frames--;
  }

  if (player->attack_phase_frames > 0) {
    fighter_player_sync_attack_visual_frames(player);
    return;
  }

  profile = fighter_attack_profile(player->last_attack);
  switch (player->attack_phase) {
    case FIGHTER_ATTACK_PHASE_STARTUP:
      fighter_player_enter_attack_phase(player, FIGHTER_ATTACK_PHASE_ACTIVE,
                                        profile.active_frames);
      break;
    case FIGHTER_ATTACK_PHASE_ACTIVE:
      player->combat_result = FIGHTER_COMBAT_RESULT_WHIFF;
      fighter_player_enter_attack_phase(player, FIGHTER_ATTACK_PHASE_RECOVERY,
                                        profile.recovery_frames);
      break;
    case FIGHTER_ATTACK_PHASE_HIT_CONFIRM:
    case FIGHTER_ATTACK_PHASE_BLOCK_CONFIRM:
      fighter_player_enter_attack_phase(player, FIGHTER_ATTACK_PHASE_RECOVERY,
                                        profile.recovery_frames);
      break;
    case FIGHTER_ATTACK_PHASE_RECOVERY:
    case FIGHTER_ATTACK_PHASE_NONE:
    default:
      fighter_player_interrupt_attack(player);
      break;
  }
}//每一帧推进玩家攻击阶段的状态机




static void fighter_player_enter_hit(fighter_player_state_t *player,
                                     int hit_stun_frames,
                                     fighter_combat_result_t result) {
  if (!player) {
    return;
  }

  fighter_player_interrupt_attack(player);
  player->block_stun_frames = 0;
  player->hurt_visual_frames = hit_stun_frames;
  player->combat_result = result;
  player->event_flags |= FIGHTER_PLAYER_EVENT_HIT;
}//玩家进入受击状态

static void fighter_player_enter_block_stun(fighter_player_state_t *player,
                                            int block_stun_frames) {
  if (!player) {
    return;
  }

  player->hurt_visual_frames = 0;
  player->block_stun_frames = block_stun_frames;
  player->combat_result = FIGHTER_COMBAT_RESULT_BLOCKED;
  player->event_flags |= FIGHTER_PLAYER_EVENT_BLOCK;
}//玩家进入防御硬直

static int fighter_player_can_enter_guard_state(
    const fighter_game_t *game,
    const fighter_player_state_t *player,
    const fighter_player_result_t *input) {
  if (!game || !player || !input) {
    return 0;
  }

  if (!input->guard_held) {
    return 0;
  }
  if (player->hp <= 0 || player->hurt_visual_frames > 0 ||
      player->block_stun_frames > 0 ||
      player->attack_phase != FIGHTER_ATTACK_PHASE_NONE) {
    return 0;
  }
  return !fighter_player_is_airborne(game, player);
}//判断是否可以进入防御状态

static int fighter_player_can_crouch_guard(
    const fighter_game_t *game,
    const fighter_player_state_t *player,
    const fighter_player_result_t *input) {
  return fighter_player_can_enter_guard_state(game, player, input) &&
         input->crouch_held;
}

static int fighter_player_can_guard(const fighter_game_t *game,
                                    const fighter_player_state_t *player,
                                    const fighter_player_result_t *input) {
  return fighter_player_can_enter_guard_state(game, player, input);
}//判断是否 可以蹲防

static void fighter_game_spawn_fireball(fighter_game_t *game, int player_index) {
  fighter_projectile_state_t *projectile;
  fighter_player_state_t *player;
  int spawn_x;
  int spawn_y;

  if (!game || player_index < 0 || player_index >= FIGHTER_PLAYER_COUNT) {
    return;
  }

  projectile = &game->projectiles[player_index];
  if (projectile->active) {
    return;
  }

  player = &game->players[player_index];
  spawn_x = player->facing > 0
                ? player->x + game->config.player_width + 4
                : player->x - game->config.projectile_width - 4;
  spawn_y = player->y + game->config.player_height / 3;

  projectile->active = 1;
  projectile->owner_index = player_index;
  projectile->x = spawn_x;
  projectile->y = spawn_y;
  projectile->vx = player->facing * game->config.projectile_speed;
  projectile->character_id = player->character_id;
  projectile->anim_ticks = 0;
}//生成火球

static void fighter_game_apply_projectile_hit(fighter_game_t *game,
                                              int attacker_index) {
  fighter_player_state_t *attacker;
  fighter_player_state_t *target;
  fighter_attack_profile_t profile;

  if (!game || attacker_index < 0 || attacker_index >= FIGHTER_PLAYER_COUNT) {
    return;
  }

  attacker = &game->players[attacker_index];
  target = &game->players[1 - attacker_index];
  profile = fighter_attack_profile(FIGHTER_ATTACK_FIREBALL);

  target->hp -= profile.damage;
  if (target->hp < 0) {
    target->hp = 0;
  }

  attacker->combat_result = FIGHTER_COMBAT_RESULT_HIT;
  attacker->event_flags |= FIGHTER_PLAYER_EVENT_HIT;

  if (target->hp == 0) {
    fighter_player_interrupt_attack(target);
    target->hurt_visual_frames = 0;
    target->block_stun_frames = 0;
    target->combat_result = FIGHTER_COMBAT_RESULT_HIT;
    target->event_flags |=
        FIGHTER_PLAYER_EVENT_HIT | FIGHTER_PLAYER_EVENT_KO;
  } else {
    fighter_player_enter_hit(target, profile.hit_stun_frames,
                             FIGHTER_COMBAT_RESULT_HIT);
  }
}//火球受击逻辑

static void fighter_game_apply_projectile_block(fighter_game_t *game,
                                                int attacker_index) {
  fighter_player_state_t *attacker;
  fighter_player_state_t *target;
  fighter_attack_profile_t profile;
  int chip_damage;

  if (!game || attacker_index < 0 || attacker_index >= FIGHTER_PLAYER_COUNT) {
    return;
  }

  attacker = &game->players[attacker_index];
  target = &game->players[1 - attacker_index];
  profile = fighter_attack_profile(FIGHTER_ATTACK_FIREBALL);
  chip_damage = fighter_attack_chip_damage(profile);

  target->hp -= chip_damage;
  if (target->hp < 0) {
    target->hp = 0;
  }//如果防御之后血空了还是会死

  attacker->combat_result = FIGHTER_COMBAT_RESULT_BLOCKED;
  attacker->event_flags |= FIGHTER_PLAYER_EVENT_BLOCK;

  if (target->hp == 0) {
    fighter_player_interrupt_attack(target);
    target->hurt_visual_frames = 0;
    target->block_stun_frames = 0;
    target->combat_result = FIGHTER_COMBAT_RESULT_BLOCKED;
    target->event_flags |=
        FIGHTER_PLAYER_EVENT_BLOCK | FIGHTER_PLAYER_EVENT_KO;
  } else {
    fighter_player_enter_block_stun(target, profile.block_stun_frames);
  }
}//防御火球

static void fighter_game_update_projectiles(
    fighter_game_t *game,
    const fighter_player_result_t inputs[FIGHTER_PLAYER_COUNT]) {
  int i;

  if (!game || !inputs) {
    return;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    fighter_projectile_state_t *projectile = &game->projectiles[i];//遍历两个玩家发的波

    if (!projectile->active) {
      continue;
    }

    projectile->x += projectile->vx;//更新火球位置，移动
    projectile->anim_ticks++;//更新动画计数

    if (projectile->x + game->config.projectile_width < 0 ||
        projectile->x >= game->config.screen_width) {
      fighter_projectile_reset(projectile);
    }//波要是飞出屏幕就删除
  }

  if (game->projectiles[0].active && game->projectiles[1].active &&
      fighter_rects_overlap(game->projectiles[0].x, game->projectiles[0].y,
                            game->config.projectile_width,
                            game->config.projectile_height, game->projectiles[1].x,
                            game->projectiles[1].y,
                            game->config.projectile_width,
                            game->config.projectile_height)) {
    fighter_projectile_reset(&game->projectiles[0]);
    fighter_projectile_reset(&game->projectiles[1]);//火球之间相互抵消
    return;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    fighter_projectile_state_t *projectile = &game->projectiles[i];//再次遍历
    fighter_player_state_t *target = &game->players[1 - i];
    const fighter_player_result_t *target_input = &inputs[1 - i];

    if (!projectile->active) {
      continue;
    }

    if (!fighter_rects_overlap(projectile->x, projectile->y,
                               game->config.projectile_width,
                               game->config.projectile_height, target->x, target->y,
                               game->config.player_width,
                               game->config.player_height)) {
      continue;
    }//检测碰撞箱是否重叠

    if (fighter_player_can_guard(game, target, target_input)) {
      fighter_game_apply_projectile_block(game, i);
    } else {
      fighter_game_apply_projectile_hit(game, i);
    }
    fighter_projectile_reset(projectile);
  }
}//负责更新所有火球状态





static int fighter_player_controls_locked(const fighter_player_state_t *player) {
  if (!player) {
    return 1;
  }

  return player->hp <= 0 || player->hurt_visual_frames > 0 ||
         player->block_stun_frames > 0 ||
         player->attack_phase != FIGHTER_ATTACK_PHASE_NONE;
}//锁玩家操作

static void fighter_game_reset_round(fighter_game_t *game) {
  int ground_y;
  int i;

  ground_y = fighter_player_ground_y(game);

  memset(game->players, 0, sizeof(game->players));
  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    fighter_projectile_reset(&game->projectiles[i]);
  }

  game->players[0].x = game->config.screen_width / 4 - game->config.player_width / 2;
  game->players[1].x =
      (game->config.screen_width * 3) / 4 - game->config.player_width / 2;

  game->players[0].y = ground_y;
  game->players[1].y = ground_y;

  game->players[0].vx = 0;
  game->players[1].vx = 0;
  game->players[0].vy = 0;
  game->players[1].vy = 0;

  game->players[0].hp = game->config.max_hp;
  game->players[1].hp = game->config.max_hp;

  game->players[0].facing = 1;
  game->players[1].facing = -1;

  game->players[0].last_attack = FIGHTER_ATTACK_NONE;
  game->players[1].last_attack = FIGHTER_ATTACK_NONE;

  game->players[0].attack_phase = FIGHTER_ATTACK_PHASE_NONE;
  game->players[1].attack_phase = FIGHTER_ATTACK_PHASE_NONE;

  game->players[0].visual_state = FIGHTER_VISUAL_STATE_IDLE;
  game->players[1].visual_state = FIGHTER_VISUAL_STATE_IDLE;

  game->players[0].character_id = FIGHTER_CHARACTER_RYU;
  game->players[1].character_id = FIGHTER_CHARACTER_KEN;

  game->players[0].state_frame = 0;
  game->players[1].state_frame = 0;

  game->round_timer_frames = (uint32_t)game->config.round_duration_frames;
  game->winner = FIGHTER_WINNER_NONE;
  game->finish_reason = FIGHTER_FINISH_REASON_NONE;
}//重置对局

static void fighter_game_clear_frame_outputs(fighter_game_t *game) {
  int i;

  if (!game) {
    return;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    game->players[i].combat_result = FIGHTER_COMBAT_RESULT_NONE;
    game->players[i].event_flags = FIGHTER_PLAYER_EVENT_NONE;
  }//清空每一帧的临时输出状态
}

static void fighter_game_enter_menu(fighter_game_t *game,
                                    fighter_audio_command_list_t *audio_commands) {
  fighter_game_reset_round(game);
  game->state = FIGHTER_GAME_STATE_MENU;
  game->state_frames = 0;
  game->finish_reason = FIGHTER_FINISH_REASON_EXIT;
  if (!game->menu_bgm_active) {
    fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_START_LOOP,
                            FIGHTER_AUDIO_TRACK_MENU_BGM);
    game->menu_bgm_active = 1;
  }
}//进入菜单页面

static void fighter_game_start_round(fighter_game_t *game,
                                     fighter_audio_command_list_t *audio_commands) {
  fighter_game_reset_round(game);
  game->state = FIGHTER_GAME_STATE_PLAYING;
  game->state_frames = 0;
  if (game->menu_bgm_active) {
    fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_STOP_LOOP,
                            FIGHTER_AUDIO_TRACK_MENU_BGM);
    game->menu_bgm_active = 0;
  }
  fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_PLAY_ONCE,
                          FIGHTER_AUDIO_TRACK_MENU_CONFIRM);
}//开始对局

static void fighter_game_enter_game_over(fighter_game_t *game,
                                         fighter_winner_t winner,
                                         fighter_finish_reason_t reason,
                                         fighter_audio_command_list_t *audio_commands) {
  int i;

  if (!game) {
    return;
  }

  game->state = FIGHTER_GAME_STATE_GAME_OVER;
  game->state_frames = 0;
  game->winner = winner;
  game->finish_reason = reason;
  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    fighter_projectile_reset(&game->projectiles[i]);
  }

  if (winner == FIGHTER_WINNER_PLAYER1) {
    game->players[0].visual_state = FIGHTER_VISUAL_STATE_VICTORY;
    game->players[0].state_frame = 0;
    game->players[1].visual_state = FIGHTER_VISUAL_STATE_KO;
    game->players[1].state_frame = 0;
  } else if (winner == FIGHTER_WINNER_PLAYER2) {
    game->players[1].visual_state = FIGHTER_VISUAL_STATE_VICTORY;
    game->players[1].state_frame = 0;
    game->players[0].visual_state = FIGHTER_VISUAL_STATE_KO;
    game->players[0].state_frame = 0;
  } else if (winner == FIGHTER_WINNER_DRAW) {
    game->players[0].visual_state = FIGHTER_VISUAL_STATE_KO;
    game->players[1].visual_state = FIGHTER_VISUAL_STATE_KO;
    game->players[0].state_frame = 0;
    game->players[1].state_frame = 0;
  }

  fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_PLAY_ONCE,FIGHTER_AUDIO_TRACK_GAME_OVER);
}//游戏结束

static void fighter_game_update_facing(fighter_game_t *game) {
  int p1_center;
  int p2_center;

  p1_center = fighter_player_center_x(game, &game->players[0]);
  p2_center = fighter_player_center_x(game, &game->players[1]);

  if (p1_center <= p2_center) {
    game->players[0].facing = 1;
    game->players[1].facing = -1;
  } else {
    game->players[0].facing = -1;
    game->players[1].facing = 1;
  }
}//更新两个玩家的朝向

static void fighter_game_resolve_overlap(fighter_game_t *game) {
  fighter_player_state_t *left_player;
  fighter_player_state_t *right_player;
  int overlap;
  int vertical_overlap;
  int push;
  int max_x;

  max_x = game->config.screen_width - game->config.player_width;
  if (game->players[0].x <= game->players[1].x) {
    left_player = &game->players[0];
    right_player = &game->players[1];
  } else {
    left_player = &game->players[1];
    right_player = &game->players[0];
  }

  overlap = left_player->x + game->config.player_width - right_player->x;
  if (overlap <= 0) {
    return;
  }
  vertical_overlap =
      fighter_player_vertical_overlap(game, left_player, right_player);
  if (vertical_overlap <= 0) {
    return;
  }

  push = overlap / 2 + 1;
  left_player->x = fighter_clamp_int(left_player->x - push, 0, max_x);
  right_player->x = fighter_clamp_int(right_player->x + push, 0, max_x);
}//防止角色重叠/站在一起

static fighter_visual_state_t fighter_game_choose_visual_state(
    const fighter_game_t *game,
    const fighter_player_state_t *player,
    const fighter_player_result_t *input) {
  if (!game || !player) {
    return FIGHTER_VISUAL_STATE_IDLE;
  }

  if (player->visual_state == FIGHTER_VISUAL_STATE_VICTORY) {
    return FIGHTER_VISUAL_STATE_VICTORY;
  }
  if (player->hp <= 0) {
    return FIGHTER_VISUAL_STATE_KO;
  }
  if (player->hurt_visual_frames > 0) {
    return FIGHTER_VISUAL_STATE_HIT;
  }
  if (player->block_stun_frames > 0) {
    return FIGHTER_VISUAL_STATE_BLOCK_STUN;
  }
  if (player->attack_phase != FIGHTER_ATTACK_PHASE_NONE) {
    return FIGHTER_VISUAL_STATE_ATTACK;
  }
  if (fighter_player_is_airborne(game, player)) {
    return FIGHTER_VISUAL_STATE_JUMP;
  }
  if (input && fighter_player_can_crouch_guard(game, player, input)) {
    return FIGHTER_VISUAL_STATE_CROUCH_GUARD;
  }
  if (input && fighter_player_can_guard(game, player, input)) {
    return FIGHTER_VISUAL_STATE_GUARD;
  }
  if (input && input->crouch_held) {
    return FIGHTER_VISUAL_STATE_CROUCH;
  }
  if (input && (input->move_left ^ input->move_right)) {
    return FIGHTER_VISUAL_STATE_WALK;
  }
  return FIGHTER_VISUAL_STATE_IDLE;
}

static void fighter_game_update_visual_state(
    const fighter_game_t *game,
    fighter_player_state_t *player,
    const fighter_player_result_t *input) {
  fighter_visual_state_t next_state;

  if (!game || !player) {
    return;
  }

  next_state = fighter_game_choose_visual_state(game, player, input);
  if (player->visual_state != next_state) {
    player->visual_state = next_state;
    player->state_frame = 0;
  } else {
    player->state_frame++;
  }
}//负责决定玩家当前帧显示什么动画，更新动画状态计数



static fighter_combat_result_t fighter_game_evaluate_contact(
    const fighter_game_t *game,
    int attacker_index,
    const fighter_player_result_t inputs[2]) {
  const fighter_player_state_t *attacker;
  const fighter_player_state_t *target;
  const fighter_player_result_t *target_input;
  fighter_attack_profile_t profile;
  int front_x;
  int target_center;
  int distance;

  if (!game || !inputs || attacker_index < 0 ||
      attacker_index >= FIGHTER_PLAYER_COUNT) {
    return FIGHTER_COMBAT_RESULT_NONE;
  }

  attacker = &game->players[attacker_index];
  target = &game->players[1 - attacker_index];
  target_input = &inputs[1 - attacker_index];
  if (attacker->hp <= 0 || target->hp <= 0 ||
      attacker->attack_phase != FIGHTER_ATTACK_PHASE_ACTIVE ||
      attacker->attack_has_connected) {
    return FIGHTER_COMBAT_RESULT_NONE;
  }

  profile = fighter_attack_profile(attacker->last_attack);
  if (attacker->last_attack == FIGHTER_ATTACK_FIREBALL || profile.damage == 0) {
    return FIGHTER_COMBAT_RESULT_NONE;
  }//如果是远程攻击的话不考虑

  front_x = attacker->facing > 0 ? attacker->x + game->config.player_width : attacker->x;
  target_center = target->x + game->config.player_width / 2;
  distance = attacker->facing > 0 ? target_center - front_x : front_x - target_center;

  if (distance < -game->config.player_width / 2 || distance > profile.reach) {
    return FIGHTER_COMBAT_RESULT_NONE;
  }
  if (target->y - attacker->y > game->config.player_height / 2 ||
      attacker->y - target->y > game->config.player_height / 2) {
    return FIGHTER_COMBAT_RESULT_NONE;
  }
  if (fighter_player_can_guard(game, target, target_input)) {
    return FIGHTER_COMBAT_RESULT_BLOCKED;
  }
  return FIGHTER_COMBAT_RESULT_HIT;
}//判定这一帧的近战攻击有没有碰到对手以及有没有防住

static void fighter_game_apply_trade(fighter_game_t *game) {
  fighter_player_state_t *p1;
  fighter_player_state_t *p2;
  fighter_attack_profile_t p1_profile;
  fighter_attack_profile_t p2_profile;

  p1 = &game->players[0];
  p2 = &game->players[1];
  p1_profile = fighter_attack_profile(p1->last_attack);
  p2_profile = fighter_attack_profile(p2->last_attack);

  p1->hp -= p2_profile.damage;
  p2->hp -= p1_profile.damage;
  if (p1->hp < 0) {
    p1->hp = 0;
  }
  if (p2->hp < 0) {
    p2->hp = 0;
  }

  p1->attack_has_connected = 1;
  p2->attack_has_connected = 1;
  fighter_player_interrupt_attack(p1);
  fighter_player_interrupt_attack(p2);
  p1->combat_result = FIGHTER_COMBAT_RESULT_TRADE;
  p2->combat_result = FIGHTER_COMBAT_RESULT_TRADE;
  p1->event_flags |= FIGHTER_PLAYER_EVENT_HIT;
  p2->event_flags |= FIGHTER_PLAYER_EVENT_HIT;

  if (p1->hp > 0) {
    p1->hurt_visual_frames = p2_profile.hit_stun_frames;
  } else {
    p1->event_flags |= FIGHTER_PLAYER_EVENT_KO;
  }
  if (p2->hp > 0) {
    p2->hurt_visual_frames = p1_profile.hit_stun_frames;
  } else {
    p2->event_flags |= FIGHTER_PLAYER_EVENT_KO;
  }
}//处理玩家相杀--trade

static void fighter_game_apply_hit(fighter_game_t *game, int attacker_index) {
  fighter_player_state_t *attacker;
  fighter_player_state_t *target;
  fighter_attack_profile_t profile;

  attacker = &game->players[attacker_index];
  target = &game->players[1 - attacker_index];
  profile = fighter_attack_profile(attacker->last_attack);

  target->hp -= profile.damage;
  if (target->hp < 0) {
    target->hp = 0;
  }

  attacker->attack_has_connected = 1;
  fighter_player_enter_attack_phase(attacker, FIGHTER_ATTACK_PHASE_HIT_CONFIRM,profile.hit_confirm_frames);
  attacker->combat_result = FIGHTER_COMBAT_RESULT_HIT;
  attacker->event_flags |= FIGHTER_PLAYER_EVENT_HIT;

  if (target->hp == 0) {
    fighter_player_interrupt_attack(target);
    target->hurt_visual_frames = 0;
    target->block_stun_frames = 0;
    target->combat_result = FIGHTER_COMBAT_RESULT_HIT;
    target->event_flags |=
        FIGHTER_PLAYER_EVENT_HIT | FIGHTER_PLAYER_EVENT_KO;
  } else {
    fighter_player_enter_hit(target, profile.hit_stun_frames, FIGHTER_COMBAT_RESULT_HIT);
  }
}//近战攻击命中之后

static void fighter_game_apply_block(fighter_game_t *game, int attacker_index) {
  fighter_player_state_t *attacker;
  fighter_player_state_t *target;
  fighter_attack_profile_t profile;
  int chip_damage;

  attacker = &game->players[attacker_index];
  target = &game->players[1 - attacker_index];
  profile = fighter_attack_profile(attacker->last_attack);
  chip_damage = fighter_attack_chip_damage(profile);

  target->hp -= chip_damage;
  if (target->hp < 0) {
    target->hp = 0;
  }

  attacker->attack_has_connected = 1;
  fighter_player_enter_attack_phase(attacker,
                                    FIGHTER_ATTACK_PHASE_BLOCK_CONFIRM,
                                    profile.block_confirm_frames);
  attacker->combat_result = FIGHTER_COMBAT_RESULT_BLOCKED;
  attacker->event_flags |= FIGHTER_PLAYER_EVENT_BLOCK;

  if (target->hp == 0) {
    fighter_player_interrupt_attack(target);
    target->hurt_visual_frames = 0;
    target->block_stun_frames = 0;
    target->combat_result = FIGHTER_COMBAT_RESULT_BLOCKED;
    target->event_flags |=
        FIGHTER_PLAYER_EVENT_BLOCK | FIGHTER_PLAYER_EVENT_KO;
  } else {
    fighter_player_enter_block_stun(target, profile.block_stun_frames);
  }
}//处理近战攻击被防住



static void fighter_game_resolve_attacks(
    fighter_game_t *game,
    const fighter_player_result_t inputs[2]) {
  fighter_combat_result_t p1_result;
  fighter_combat_result_t p2_result;

  if (!game || !inputs) {
    return;
  }

  p1_result = fighter_game_evaluate_contact(game, 0, inputs);
  p2_result = fighter_game_evaluate_contact(game, 1, inputs);

  if (p1_result == FIGHTER_COMBAT_RESULT_HIT &&
      p2_result == FIGHTER_COMBAT_RESULT_HIT) {
    fighter_game_apply_trade(game);
    return;
  }

  if (p1_result == FIGHTER_COMBAT_RESULT_HIT) {
    fighter_game_apply_hit(game, 0);
  } else if (p1_result == FIGHTER_COMBAT_RESULT_BLOCKED) {
    fighter_game_apply_block(game, 0);
  }

  if (game->players[1].hp <= 0) {
    return;
  }

  if (p2_result == FIGHTER_COMBAT_RESULT_HIT &&
      game->players[1].attack_phase == FIGHTER_ATTACK_PHASE_ACTIVE) {
    fighter_game_apply_hit(game, 1);
  } else if (p2_result == FIGHTER_COMBAT_RESULT_BLOCKED &&
             game->players[1].attack_phase == FIGHTER_ATTACK_PHASE_ACTIVE) {
    fighter_game_apply_block(game, 1);
  }
}//处理近战攻击结果，如果同时命中处理trade， 否则先处理p1再根据情况处理p2




static void fighter_game_handle_round_end_from_hp(
    fighter_game_t *game,
    fighter_audio_command_list_t *audio_commands) {
  int p1_dead;
  int p2_dead;

  if (!game || game->state != FIGHTER_GAME_STATE_PLAYING) {
    return;
  }

  p1_dead = game->players[0].hp <= 0;
  p2_dead = game->players[1].hp <= 0;
  if (!p1_dead && !p2_dead) {
    return;
  }

  if (p1_dead) {
    game->players[0].event_flags |= FIGHTER_PLAYER_EVENT_KO;
  }
  if (p2_dead) {
    game->players[1].event_flags |= FIGHTER_PLAYER_EVENT_KO;
  }

  if (p1_dead && p2_dead) {
    fighter_game_enter_game_over(game, FIGHTER_WINNER_DRAW,
                                 FIGHTER_FINISH_REASON_DOUBLE_KO,
                                 audio_commands);
  } else if (p1_dead) {
    fighter_game_enter_game_over(game, FIGHTER_WINNER_PLAYER2,
                                 FIGHTER_FINISH_REASON_KO, audio_commands);
  } else {
    fighter_game_enter_game_over(game, FIGHTER_WINNER_PLAYER1,
                                 FIGHTER_FINISH_REASON_KO, audio_commands);
  }
}//根据hp来判断一轮是否结束



static void fighter_game_handle_player(fighter_game_t *game,
                                       int player_index,
                                       const fighter_player_result_t inputs[2]) {
  fighter_player_state_t *player;
  const fighter_player_result_t *input;
  int ground_y;
  int max_x; //最大x坐标
  int was_airborne;
  int is_airborne;
  int controls_locked;
  fighter_attack_phase_t previous_attack_phase;

  player = &game->players[player_index];
  input = &inputs[player_index];

  ground_y = fighter_player_ground_y(game);

  max_x = game->config.screen_width - game->config.player_width;
  was_airborne = fighter_player_is_airborne(game, player);//记录这一帧开始时是否滞空

  if (!was_airborne) {
    player->vx = 0;
  }//玩家在地面，清零水平速度

  if (player->attack_cooldown_frames > 0) {
    player->attack_cooldown_frames--;//更新倒计时
  }
  if (player->hurt_visual_frames > 0) {
    player->hurt_visual_frames--;//受击动画倒计时
  }
  if (player->block_stun_frames > 0) {
    player->block_stun_frames--;//防御硬直计时
  }

  previous_attack_phase = player->attack_phase;
  fighter_player_tick_attack_phase(player);//推进攻击状态机


  if (previous_attack_phase != FIGHTER_ATTACK_PHASE_ACTIVE &&
      player->attack_phase == FIGHTER_ATTACK_PHASE_ACTIVE) {
    if (player->last_attack == FIGHTER_ATTACK_DRAGON_PUNCH && player->vy >= 0) {
      player->vy = game->config.dragon_punch_lift_velocity;
    } else if (player->last_attack == FIGHTER_ATTACK_FIREBALL) {
      fighter_game_spawn_fireball(game, player_index);
    }
  }
  controls_locked = fighter_player_controls_locked(player);

  if (player->hp > 0 && !controls_locked) {
    if (input->jump_pressed && !was_airborne) {
      player->vy = game->config.jump_velocity;
      if (input->move_left && !input->move_right) {
        player->vx = -game->config.walk_speed;
      } else if (input->move_right && !input->move_left) {
        player->vx = game->config.walk_speed;
      } else {
        player->vx = 0;
      }
    }//如果玩家还活着就处理输入

    is_airborne = fighter_player_is_airborne(game, player);
    if (!is_airborne && !input->jump_held && !input->guard_held && !input->crouch_held) {
      if (input->move_left && !input->move_right) {
        player->vx = -game->config.walk_speed;
      } else if (input->move_right && !input->move_left) {
        player->vx = game->config.walk_speed;
      }
    }//非滞空才能地面移动

    if (input->attack_pressed && player->attack_cooldown_frames == 0) {
      fighter_attack_command_t command = input->attack_command;
      int allow_attack = 0;

      if (was_airborne) {
        allow_attack = input->jump_held && command == FIGHTER_ATTACK_JUMP_ATTACK;//只有滞空才能跳跃攻击
        if (allow_attack) {
          command = FIGHTER_ATTACK_JUMP_ATTACK;
        }
      } else if (!is_airborne) {
        allow_attack = command != FIGHTER_ATTACK_JUMP_ATTACK &&
                       command != FIGHTER_ATTACK_FORWARD_JUMP_ATTACK &&
                       command != FIGHTER_ATTACK_BACK_JUMP_ATTACK;
      }

      if (allow_attack) {
        player->attack_cooldown_frames = game->config.attack_cooldown_frames;
        fighter_player_begin_attack(player, command);
      }
    }
  }

  player->x += player->vx;
  player->x = fighter_clamp_int(player->x, 0, max_x);

  player->y += player->vy;
  if (player->y < ground_y) {
    player->vy += game->config.gravity;
  }
  if (player->y >= ground_y) {
    player->y = ground_y;
    if (was_airborne) {
      player->event_flags |= FIGHTER_PLAYER_EVENT_LAND;
    }
    player->vy = 0;
    player->vx = 0;
  }
}//每帧更新单个玩家状态

static void fighter_game_tick_menu(fighter_game_t *game,
                                   const fighter_player_result_t inputs[2],
                                   fighter_audio_command_list_t *audio_commands) {
  int i;

  if (!game->menu_bgm_active) {
    fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_START_LOOP,
                            FIGHTER_AUDIO_TRACK_MENU_BGM);
    game->menu_bgm_active = 1;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    if (inputs[i].any_input_pressed) {
      fighter_game_start_round(game, audio_commands);
      return;
    }
  }
}//菜单界面的每帧逻辑

static void fighter_game_tick_playing(fighter_game_t *game,
                                      const fighter_player_result_t inputs[2],
                                      fighter_audio_command_list_t *audio_commands) {
  int i;

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    if (inputs[i].exit_requested) {
      fighter_game_enter_menu(game, audio_commands);
      return;
    }
  }

  fighter_game_update_facing(game);
  fighter_game_handle_player(game, 0, inputs);
  fighter_game_handle_player(game, 1, inputs);
  fighter_game_resolve_overlap(game);
  fighter_game_update_facing(game);
  fighter_game_resolve_attacks(game, inputs);
  fighter_game_update_projectiles(game, inputs);

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    fighter_game_update_visual_state(game, &game->players[i], &inputs[i]);
  }

  fighter_game_handle_round_end_from_hp(game, audio_commands);
  if (game->state != FIGHTER_GAME_STATE_PLAYING) {
    return;
  }

  if (game->round_timer_frames > 0) {
    game->round_timer_frames--;
  }

  if (game->round_timer_frames == 0) {
    fighter_winner_t winner = FIGHTER_WINNER_DRAW;

    if (game->players[0].hp > game->players[1].hp) {
      winner = FIGHTER_WINNER_PLAYER1;
    } else if (game->players[1].hp > game->players[0].hp) {
      winner = FIGHTER_WINNER_PLAYER2;
    }
    fighter_game_enter_game_over(game, winner, FIGHTER_FINISH_REASON_TIME_OUT,
                                 audio_commands);
  }
}//对战中每帧逻辑

static void fighter_game_tick_game_over(fighter_game_t *game,
                                        const fighter_player_result_t inputs[2],
                                        fighter_audio_command_list_t *audio_commands) {
  int i;

  if (!fighter_game_game_over_ready(game)) {
    return;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    if (inputs[i].exit_requested) {
      fighter_game_enter_menu(game, audio_commands);
      return;
    }
    if (inputs[i].attack_pressed || inputs[i].guard_pressed) {
      fighter_game_start_round(game, audio_commands);
      return;
    }
  }
}//gameover下每帧逻辑

void fighter_game_config_default(fighter_game_config_t *config) {
  if (!config) {
    return;
  }

  memset(config, 0, sizeof(*config));
  config->screen_width = 640;
  config->screen_height = 480;
  config->floor_y = 400;
  config->player_width = 48;
  config->player_height = 96;
  config->projectile_width = 28;
  config->projectile_height = 20;
  config->projectile_speed = 6;
  config->walk_speed = 3;
  config->jump_velocity = -14;
  config->gravity = 1;
  config->max_hp = 100;
  config->round_duration_frames = 99 * 60;
  config->menu_anim_period_frames = 20;
  config->game_over_anim_frames = 120;
  config->attack_cooldown_frames = 14;
  config->dragon_punch_lift_velocity = -9;
  config->attack_visual_frames = 6;
  config->hurt_visual_frames = 8;
}//游戏基础设置

void fighter_game_init(fighter_game_t *game, const fighter_game_config_t *config) {
  if (!game) {
    return;
  }

  memset(game, 0, sizeof(*game));
  if (config) {
    game->config = *config;
  } else {
    fighter_game_config_default(&game->config);
  }

  fighter_game_reset_round(game);
  game->state = FIGHTER_GAME_STATE_MENU;
}

void fighter_game_tick(fighter_game_t *game,
                       const fighter_player_result_t inputs[FIGHTER_PLAYER_COUNT],
                       fighter_audio_command_list_t *audio_commands) {
  fighter_game_state_t previous_state;

  if (!game || !inputs) {
    return;
  }

  if (audio_commands) {
    fighter_audio_command_list_clear(audio_commands);
  }

  fighter_game_clear_frame_outputs(game);
  previous_state = game->state;
  game->frame_counter++;

  switch (game->state) {
    case FIGHTER_GAME_STATE_MENU:
      fighter_game_tick_menu(game, inputs, audio_commands);
      break;
    case FIGHTER_GAME_STATE_PLAYING:
      fighter_game_tick_playing(game, inputs, audio_commands);
      break;
    case FIGHTER_GAME_STATE_GAME_OVER:
      fighter_game_tick_game_over(game, inputs, audio_commands);
      break;
    default:
      break;
  }

  if (game->state == previous_state) {
    game->state_frames++;
  }
}//帧逻辑入口函数

int fighter_game_menu_animation_frame(const fighter_game_t *game) {
  if (!game || game->config.menu_anim_period_frames <= 0) {
    return 0;
  }

  return (int)((game->frame_counter /(uint32_t)game->config.menu_anim_period_frames) & 1U);

}//菜单动画当前应该显示第几帧

int fighter_game_game_over_ready(const fighter_game_t *game) {
  if (!game || game->state != FIGHTER_GAME_STATE_GAME_OVER) {
    return 0;
  }

  return game->state_frames >= (uint32_t)game->config.game_over_anim_frames;
}//判断gameover界面是否接受玩家操作

int fighter_game_round_seconds_remaining(const fighter_game_t *game) {
  if (!game) {
    return 0;
  }

  return (int)((game->round_timer_frames + 59U) / 60U);
}//游戏内剩余时间
