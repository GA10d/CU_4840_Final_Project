#ifndef FIGHTER_GAMEPAD_H
#define FIGHTER_GAMEPAD_H
#include <stdbool.h>
#include "fighter_input.h"

#ifdef __cplusplus
extern "C" {
#endif

//这一层是“真实手柄输入”的抽象：只记录按钮/方向当前是否按下
typedef struct {
  bool up;              // D-pad 上；来自 Linux ABS_Y 低值
  bool down;            // D-pad 下；来自 Linux ABS_Y 高值
  bool left;            // D-pad 左；来自 Linux ABS_X 低值
  bool right;           // D-pad 右；来自 Linux ABS_X 高值
  bool attack;          // 普通攻击键；当前映射为 A/右肩备用
  bool guard;           // 防御键；当前映射为 B/左肩备用
  bool fireball;        // 火球快捷键；当前映射为 X
  bool dragon_punch;    // 升龙拳快捷键；当前映射为 Y
  bool start;           // START 键；菜单确认/结算重开用
  bool exit_game;       // SELECT 键；退出对战/返回菜单用
} fighter_gamepad_buttons_t;

// 单个 Linux event 手柄设备的运行时状态
typedef struct {
  int fd;                                     // /dev/input/eventX 的文件描述符
  int connected;                              // 是否成功打开设备
  char device_path[128];                      // 保存设备路径，方便日志/调试
  fighter_gamepad_buttons_t current_buttons;  // 当前帧读到的按钮状态
  fighter_gamepad_buttons_t previous_buttons; // 上一帧按钮状态，用于检测“刚按下”
} fighter_gamepad_t;

// 初始化结构体，主要把 fd 设成 -1，表示还没打开设备
void fighter_gamepad_init(fighter_gamepad_t *gamepad);

// 打开一个 Linux input event 设备，例如 /dev/input/event1
int fighter_gamepad_open(fighter_gamepad_t *gamepad, const char *device_path);

// 关闭设备并清空按钮状态
void fighter_gamepad_close(fighter_gamepad_t *gamepad);

// 每帧调用：读取 event 设备，并输出游戏已有的 fighter_player_result_t
int fighter_gamepad_update(fighter_gamepad_t *gamepad,fighter_player_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
