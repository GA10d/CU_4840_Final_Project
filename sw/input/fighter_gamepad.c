#include "fighter_gamepad.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>

//errno 用于区分非阻塞 read 暂时无事件、被信号打断、真实错误等情况
//fcntl.h 提供 open() 的 O_RDONLY/O_NONBLOCK 标志
//stdio.h 用于 snprintf() 保存设备路径
// string.h 用于 memset() 清空结构体状态
//unistd.h 提供 read()/close()
//Linux input 事件宏定义都在这里，例如 EV_ABS、EV_KEY、ABS_X、BTN_THUMB。



//判断按钮是否从“未按下”变成“按下”，用于生成一帧宽度的 edge 事件
//current=true 且 previous=false，说明这一帧刚刚按下
static int button_pressed(bool current, bool previous) {
  return current && !previous;
}




//根据当前按钮组合决定攻击命令，保持和原键盘“方向 + 攻击”类似的手感
static fighter_attack_command_t attack_command_from_gamepad(
    const fighter_gamepad_buttons_t *buttons) {


  // 如果没有按钮状态，保守地返回普通攻击
  if (!buttons) {
    return FIGHTER_ATTACK_NORMAL;
  }
  // X 键被按住时，A/攻击触发火球
  if (buttons->fireball) {
    return FIGHTER_ATTACK_FIREBALL;
  }
  //Y 键被按住时，A/攻击触发升龙拳
  if (buttons->dragon_punch) {
    return FIGHTER_ATTACK_DRAGON_PUNCH;
  }
  // 上 + 攻击触发跳攻击
  if (buttons->up) {
    return FIGHTER_ATTACK_JUMP_ATTACK;
  }
  //下 + 攻击触发扫腿
  if (buttons->down) {
    return FIGHTER_ATTACK_SWEEP;
  }
  //左 + 攻击沿用键盘逻辑，触发火球
  if (buttons->left) {
    return FIGHTER_ATTACK_FIREBALL;
  }
  //右 + 攻击沿用键盘逻辑，触发升龙拳
  if (buttons->right) {
    return FIGHTER_ATTACK_DRAGON_PUNCH;
  }

  
  // 没有方向修饰时就是普通攻击
  return FIGHTER_ATTACK_NORMAL;
}



//初始化手柄状态，调用者在 open 前先调用它
void fighter_gamepad_init(fighter_gamepad_t *gamepad) {
  //允许传空指针，方便上层清理路径写得简单
  if (!gamepad) {
    return;
  }

  // 清空所有字段，包括按钮状态和 connected 标志
  // fd=-1 表示当前没有打开任何 event 设备
  memset(gamepad, 0, sizeof(*gamepad));
  gamepad->fd = -1;
}





// 打开 Linux input event 设备
int fighter_gamepad_open(fighter_gamepad_t *gamepad, const char *device_path) {
 
  if (!gamepad || !device_path || device_path[0] == '\0') {
    return -1;
  }

  // 如果之前打开过设备，先关闭并清空状态，避免 fd 泄漏
  fighter_gamepad_close(gamepad);

  // 非阻塞打开，每帧 poll 时没有新事件不会卡住游戏主循环
  gamepad->fd = open(device_path, O_RDONLY | O_NONBLOCK);

  if (gamepad->fd < 0) {
    return -1;
  }

  gamepad->connected = 1;//标记手柄已连接

  //保存路径，主要用于启动日志和调试
  snprintf(gamepad->device_path, sizeof(gamepad->device_path), "%s", device_path);

  // 打开新设备后，当前按钮状态清空， 上一帧按钮状态清空，防止启动时出现假边沿
  memset(&gamepad->current_buttons, 0, sizeof(gamepad->current_buttons));
  memset(&gamepad->previous_buttons, 0, sizeof(gamepad->previous_buttons));
  
  return 0;
}





// 关闭手柄设备，并把状态恢复到未连接
void fighter_gamepad_close(fighter_gamepad_t *gamepad) {

  if (!gamepad) {
    return;
  }

  // 在fd有效时关闭
  if (gamepad->fd >= 0) {
    close(gamepad->fd);
  }
  // fd=-1表示没有打开设备
  gamepad->fd = -1;
  //connected=0表示上层不应该再poll它
  gamepad->connected = 0;
  // 清空路径
  gamepad->device_path[0] = '\0';

  //清空当前按钮状态和上一帧按钮状态
  memset(&gamepad->current_buttons, 0, sizeof(gamepad->current_buttons));
  memset(&gamepad->previous_buttons, 0, sizeof(gamepad->previous_buttons));
}





// 处理EV_ABS轴事件，手柄的Dpad被Linux上报为ABS_X/ABS_Y
static void fighter_gamepad_handle_abs(fighter_gamepad_t *gamepad, unsigned short code,int value) {
  //手柄中立值约为 127，低于64认为是左/上
  const int low_threshold = 64;
  //高于 190 认为是右/下
  const int high_threshold = 190;

  
  if (!gamepad) {
    return;
  }

  //ABS_X表示水平轴：0=L，127=mid，255=R
  if (code == ABS_X) {
    // value低于阈值时认为按住左
    gamepad->current_buttons.left = value < low_threshold;
    //value 高于阈值时认为按住右。 */
    gamepad->current_buttons.right = value > high_threshold;
  } 
  //ABS_Y 表示垂直轴：0=Up，127=mid，255=Down
  else if (code == ABS_Y) {
    //value 低于阈值时认为按住上
    gamepad->current_buttons.up = value < low_threshold;
    //value 高于阈值时认为按住下
    gamepad->current_buttons.down = value > high_threshold;
  }
}




//处理 EV_KEY 按钮事件,把 Linux BTN_*编号映射到游戏按钮语义
static void fighter_gamepad_handle_key(fighter_gamepad_t *gamepad,unsigned short code,int value) {
  bool pressed; //pressed=true 表示按钮当前处于按下状态

  if (!gamepad) {
    return;
  }

  //EV_KEY value=1按下，value=0松开,这里非 0 都按按下处理
  pressed = value != 0;
  //code 是 Linux input-event-codes.h里的BTN_*宏
  switch (code) {
    // A 键上报 BTN_THUMB,用作普通攻击键
    case BTN_THUMB:
      gamepad->current_buttons.attack = pressed;
      break;
    //B键上报 BTN_THUMB2,用作防御键
    case BTN_THUMB2:
      gamepad->current_buttons.guard = pressed;
      break;
    //X 键上报 BTN_TRIGGER，用作发波快捷键
    case BTN_TRIGGER:
      gamepad->current_buttons.fireball = pressed;
      break;
    //Y 键上报 BTN_TOP，用作升龙拳快捷键
    case BTN_TOP:
      gamepad->current_buttons.dragon_punch = pressed;
      break;
    //SELECT 上报 BTN_BASE3，用作退出/返回菜单
    case BTN_BASE3:
      gamepad->current_buttons.exit_game = pressed;
      break;
    // START 上报 BTN_BASE4，用作菜单确认/结算重开
    case BTN_BASE4:
      gamepad->current_buttons.start = pressed;
      break;
    //左肩键上报 BTN_TOP2，作为防御备用键
    case BTN_TOP2:
      gamepad->current_buttons.guard = pressed;
      break;
    //右肩键上报 BTN_PINKIE，作为攻击备用键
    case BTN_PINKIE:
      gamepad->current_buttons.attack = pressed;
      break;
    
    default:
      break;
  }
}




//一次性读完当前 event 设备里积压的所有事件，更新 current_buttons
static int fighter_gamepad_drain_events(fighter_gamepad_t *gamepad) {

  if (!gamepad || gamepad->fd < 0) {
    return -1;
  }

  //非阻塞fd下循环read，直到暂时没有更多事件
  while (1) {
    //Linux input 的原始事件结构，包含 type，code，value
    struct input_event event;
    //从 /dev/input/eventX 读取一个完整事件
    ssize_t bytes_read = read(gamepad->fd, &event, sizeof(event));

    // bytes_read < 0 表示 read 没成功
    if (bytes_read < 0) {
      // 非阻塞模式下，EAGAIN/EWOULDBLOCK 表示事件已经读完
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
      }
      //EINTR 表示被信号打断，重试即可
      if (errno == EINTR) {
        continue;
      }
      
      return -1;
    }
    //正常情况下 read 应该刚好返回一个struct input_event 大小
    if (bytes_read != (ssize_t)sizeof(event)) {
      return -1;
    }

    //返回EV_ABS用于处理方向
    if (event.type == EV_ABS) {
      fighter_gamepad_handle_abs(gamepad, event.code, event.value);
    //返回EV_KEY 用于处理 A/B/X/Y/START/SELECT/肩键
    } else if (event.type == EV_KEY) {
      fighter_gamepad_handle_key(gamepad, event.code, event.value);
    }
  }
}






// 每帧把真实手柄输入转换成游戏已有的 fighter_player_result_t
int fighter_gamepad_update(fighter_gamepad_t *gamepad,fighter_player_result_t *result) {
  // current 指向当前帧按钮状态
  fighter_gamepad_buttons_t *current;
  //previous 指向上一帧按钮状态，用于检测边沿
  fighter_gamepad_buttons_t *previous;
  
  int attack_edge;  // A/攻击键是否刚按
  int fireball_edge;  //X/火球键是否刚按
  int dragon_punch_edge;  //Y/升龙拳键是否刚按
  int start_edge; //START 键是否刚按
  int guard_edge;  // B/防御键是否刚按下


  if (!gamepad || !result || !gamepad->connected) {
    return -1;
  }

  //每帧先清空输出，避免上一帧结果残留
  memset(result, 0, sizeof(*result));

  //读取所有积压的 Linux input 事件，并更新 current_buttons
  if (fighter_gamepad_drain_events(gamepad) != 0) {
    return -1;
  }

  //取当前帧按钮状态
  current = &gamepad->current_buttons;
  //取上一帧按钮状态
  previous = &gamepad->previous_buttons;
  //计算普通攻击键边沿
  attack_edge = button_pressed(current->attack, previous->attack);
  //计算火球快捷键边沿
  fireball_edge = button_pressed(current->fireball, previous->fireball);
  // 计算升龙拳快捷键边沿
  dragon_punch_edge = button_pressed(current->dragon_punch, previous->dragon_punch);
  //计算 START 边沿
  start_edge = button_pressed(current->start, previous->start);
  //计算防御键边沿
  guard_edge = button_pressed(current->guard, previous->guard);






  //左右同时按时互相抵消
  result->move_left = current->left && !current->right;
  result->move_right = current->right && !current->left;

 
  result->move_left_pressed =button_pressed(result->move_left, previous->left);  //只在刚进入左方向时置位一帧
  result->move_right_pressed =button_pressed(result->move_right, previous->right); //只在刚进入右方向时置位一帧

  
  result->jump_held = current->up;  //上方向持续按住，对游戏来说是 jump held
  
  result->jump_pressed = button_pressed(current->up, previous->up); //上方向刚按下，对游戏来说是 jump pressed
  
  result->crouch_held = current->down;    //下方向持续按住，对游戏来说是 crouch held。 
  result->crouch_pressed = button_pressed(current->down, previous->down);  // 下方向刚按下，对游戏来说是 crouch pressed
  
  result->guard_held = current->guard;  //B/左肩持续按住，对游戏来说是 guard held
  result->guard_pressed = guard_edge || start_edge;   // B/左肩刚按下触发 guard_pressed；START 也复用这个字段做菜单/结算确认
  
 
  result->exit_requested = button_pressed(current->exit_game, previous->exit_game);   // SELECT 刚按下时，请求退出/返回菜单




  // 任意攻击类按钮刚按下，都生成一帧 attack_pressed
  result->attack_pressed = attack_edge || fireball_edge || dragon_punch_edge;

  // any_input_active 表示当前至少有一个手柄输入保持按下
  result->any_input_active =
      current->up || current->down || current->left || current->right ||
      current->attack || current->guard || current->fireball ||
      current->dragon_punch || current->start || current->exit_game;


  // any_input_pressed 表示本帧至少出现一个新的按下边沿，菜单启动会用它。
  result->any_input_pressed =
      button_pressed(current->up, previous->up) ||
      button_pressed(current->down, previous->down) ||
      button_pressed(current->left, previous->left) ||
      button_pressed(current->right, previous->right) ||
      attack_edge ||
      guard_edge ||
      fireball_edge || dragon_punch_edge ||
      start_edge ||
      button_pressed(current->exit_game, previous->exit_game);



  // Y 快捷键优先生成升龙拳
  if (dragon_punch_edge) {
    result->attack_command = FIGHTER_ATTACK_DRAGON_PUNCH;
  // X 快捷键其次生成火球
  } else if (fireball_edge) {
    result->attack_command = FIGHTER_ATTACK_FIREBALL;
  // A/右肩攻击键根据当前方向组合决定具体招式
  } else if (attack_edge) {
    result->attack_command = attack_command_from_gamepad(current);
  // 没有攻击边沿时，不发出新攻击命令
  } else {
    result->attack_command = FIGHTER_ATTACK_NONE;
  }

  
  gamepad->previous_buttons = gamepad->current_buttons;     // 当前帧处理完后，保存为下一帧的 previous，用于下一次检测边沿
  return 0;
}
