/*
 * 俄罗斯方块页面对外接口。
 *
 * 当前文件只声明“页面层”需要的函数：
 * - 创建俄罗斯方块界面；
 * - 处理该页面接收到的按键。
 *
 */

#ifndef __TETRIS_PAGE_H__
#define __TETRIS_PAGE_H__

#include "lvgl.h"
#include "BSP_Key.h"

/*
 * 创建俄罗斯方块页面。
 *
 * parent：
 *   页面父容器，由 ui_nav.c 传入。
 *   所有俄罗斯方块页面控件都创建在这个容器内。
 */
void ui_page_tetris_create(lv_obj_t *parent);

/*
 * 处理俄罗斯方块页面的物理按键。
 *
 * 当前阶段只实现 ESC 返回桌面；
 * 后续会继续扩展为：
 * - 上键：旋转
 * - 左键：左移
 * - 右键：右移
 * - 下键：加速下落
 * - 确认键：开始或暂停
 */
void ui_page_tetris_on_key(BSP_Key_t key);

/*
 * 离开俄罗斯方块页面前释放页面定时器。
 *
 * 该函数由导航器统一调用，既覆盖 ESC 返回，
 * 也覆盖系统自动锁屏等非按键页面切换。
 */
void ui_page_tetris_leave(void);




#endif /* __TETRIS_PAGE_H__ */
