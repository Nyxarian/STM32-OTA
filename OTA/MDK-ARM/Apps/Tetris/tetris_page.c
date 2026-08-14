/*
 * 俄罗斯方块页面层。
 *
 * 这个文件目前只负责“画界面”，不处理实际游戏规则。
 *
 * 页面布局：
 *
 *  320 x 240 横屏 LCD
 *
 *  ┌──────────────────────────────────────┐
 *  │ TETRIS                         ESC BACK│
 *  ├──────────────┬───────────────────────┤
 *  │              │ SCORE      NEXT        │
 *  │              │ 000000    [下一方块]   │
 *  │   主棋盘     │ LINES                 │
 *  │  10列 x18行  │ 000                   │
 *  │              │                       │
 *  │              │ STATE                 │
 *  │              │ READY                 │
 *  └──────────────┴───────────────────────┘
 *
 * 后续 tetris_game.c 会控制：
 * - 主棋盘内部各个格子显示什么颜色；
 * - 分数、消行数；
 * - 下一方块预览；
 * - READY / RUNNING / PAUSED / GAME OVER 状态。
 */

#include "tetris_page.h"
#include "tetris_game.h"
#include "ui_components.h"
#include "ui_nav.h"
#include "ui_theme.h"

/*
 * 左侧主棋盘的位置与尺寸。
 *
 * 棋盘视觉区域为 108 x 180 像素，
 * 后续内部按 10 列 x 18 行划分。
 *
 * 目前先固定这些宏，后续写游戏逻辑时，
 * tetris_game.c 也会以它们为参考。
 */
#define TETRIS_BOARD_X       20           
#define TETRIS_BOARD_Y       38
#define TETRIS_BOARD_W       108
#define TETRIS_BOARD_H       180

/*
 * 每个棋盘格显示为 9×9 像素，
 * 相邻格子之间保留 1 像素黑色间隙。
 */
#define TETRIS_CELL_SIZE       9
#define TETRIS_CELL_PITCH      10

/* NEXT 预览框中第一个格子的屏幕坐标。 */
#define TETRIS_NEXT_X          231
#define TETRIS_NEXT_Y          111

/*
 * 自动下落周期。
 *
 * 500 ms 下落一格，适合作为第一级速度。
 * 后续可以随 level 提升逐步缩短。
 */
#define TETRIS_DROP_PERIOD_MS  500U

/*
 * 当前页面使用的游戏数据。
 *
 * tetris_game 只保存游戏规则和棋盘数据，
 * 不保存任何 LVGL 控件。
 */
static TetrisGame_t tetris_game;

/*
 * 主棋盘本身只使用一个 LVGL 对象。
 * 棋盘内部的小方块由 LVGL 绘制事件直接画出，
 * 不再为每一个格子创建独立对象。
 */
static lv_obj_t *tetris_board_obj;

/*
 * NEXT 预览框也只使用一个 LVGL 对象。
 */
static lv_obj_t *tetris_next_obj;

/*
 * 需要动态修改文字内容的三个标签。
 */
static lv_obj_t *tetris_score_label;
static lv_obj_t *tetris_lines_label;
static lv_obj_t *tetris_state_label;

/*
 * LVGL 软件定时器。
 *
 * 它不是 STM32 硬件定时器，而是由 UI 任务中的
 * lv_timer_handler() 周期执行。
 */
static lv_timer_t *tetris_drop_timer;

/*
 * 根据游戏格子数值取得显示颜色。
 *
 * 格子值：
 * 0：空白；
 * 1～7：七种不同方块。
 *
 * 当前颜色限制在项目已有的科技风色板中，
 * 不额外加入过多颜色。
 */
static lv_color_t tetris_get_cell_color(uint8_t cell_value)
{
    switch (cell_value)
    {
        case 1U:  /* I */
            return UI_COLOR_LIGHT_BLUE;

        case 2U:  /* O */
            return UI_COLOR_TEXT;

        case 3U:  /* T */
            return UI_COLOR_RED;

        case 4U:  /* S */
            return UI_COLOR_KL;

        case 5U:  /* Z */
            return UI_COLOR_RED;

        case 6U:  /* J */
            return UI_COLOR_AUX_BLUE;

        case 7U:  /* L */
            return UI_COLOR_TEXT;

        default:
            return UI_COLOR_APP_BG;
    }
}

/*
 * 绘制一个棋盘小方块。
 *
 * layer：
 *   LVGL 当前正在绘制的图层。
 *
 * x/y：
 *   小方块左上角的屏幕坐标。
 *
 * color：
 *   方块颜色。
 */
static void tetris_draw_cell(lv_layer_t *layer, int16_t x, int16_t y, lv_color_t color)
{
    lv_draw_rect_dsc_t draw_dsc;
    lv_area_t area;

    /*
     * 初始化矩形绘制描述。
     */
    lv_draw_rect_dsc_init(&draw_dsc);

    /*
     * 让矩形直接绘制到当前图层。
     */
    draw_dsc.base.layer = layer;

    /* 设置方块颜色和不透明度。 */
    draw_dsc.bg_color = color;
    draw_dsc.bg_opa = LV_OPA_COVER;

    /* 方块采用直角样式，不显示边框。 */
    draw_dsc.radius = 0;
    draw_dsc.border_width = 0;
    draw_dsc.border_opa = LV_OPA_TRANSP;

    /*
     * 计算矩形右下角。
     *
     * TETRIS_CELL_SIZE 为 9，
     * 所以 x 到 x+8 一共 9 个像素。
     */
    area.x1 = x;
    area.y1 = y;
    area.x2 = (int16_t)(x + TETRIS_CELL_SIZE - 1);
    area.y2 = (int16_t)(y + TETRIS_CELL_SIZE - 1);

    /* 将矩形加入 LVGL 当前绘制任务。 */
    lv_draw_rect(layer, &draw_dsc, &area);
}

/*
 * 绘制主棋盘。
 *
 * 此函数由 LVGL 的 DRAW_POST 事件调用。
 * 主棋盘背景已经由普通 LVGL 对象绘制完成，
 * 这里仅绘制有内容的格子。
 */
static void tetris_board_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *object;
    lv_layer_t *layer;
    lv_area_t object_area;
    uint8_t row;
    uint8_t col;
    uint8_t cell_value;
    int16_t x;
    int16_t y;

    object = lv_event_get_current_target(event);
    layer = lv_event_get_layer(event);

    if ((object == 0) || (layer == 0))
    {
        return;
    }

    /*
     * 取得棋盘对象在 LCD 上的绝对坐标。
     * 这样即使以后调整棋盘位置，绘制坐标也会自动跟随。
     */
    lv_obj_get_coords(object, &object_area);

    for (row = 0U; row < TETRIS_BOARD_ROWS; row++)
    {
        for (col = 0U; col < TETRIS_BOARD_COLS; col++)
        {
            cell_value = TetrisGame_GetCell(&tetris_game, row, col);

            /*
             * 空白格不绘制，保留棋盘背景色。
             */
            if (cell_value == TETRIS_CELL_EMPTY)
            {
                continue;
            }

            x = (int16_t)(object_area.x1 + 4 + (int16_t)col * TETRIS_CELL_PITCH);

            y = (int16_t)(object_area.y1 + (int16_t)row * TETRIS_CELL_PITCH);

            tetris_draw_cell(layer, x, y, tetris_get_cell_color(cell_value));
        }
    }
}

/*
 * 绘制 NEXT 预览区域。
 */
static void tetris_next_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *object;
    lv_layer_t *layer;
    lv_area_t object_area;
    uint8_t row;
    uint8_t col;
    uint8_t cell_value;
    int16_t x;
    int16_t y;

    object = lv_event_get_current_target(event);
    layer = lv_event_get_layer(event);

    if ((object == 0) || (layer == 0))
    {
        return;
    }

    lv_obj_get_coords(object, &object_area);

    for (row = 0U; row < TETRIS_SHAPE_SIZE; row++)
    {
        for (col = 0U; col < TETRIS_SHAPE_SIZE; col++)
        {
            cell_value = TetrisGame_GetNextCell(&tetris_game, row, col);

            if (cell_value == TETRIS_CELL_EMPTY)
            {
                continue;
            }

            /*
             * 70×58 的预览框中居中显示 4×4 方块。
             * 4×10=40，所以左右预留约 15 像素。
             */
            x = (int16_t)(object_area.x1 + 15 + (int16_t)col * TETRIS_CELL_PITCH);

            y = (int16_t)(object_area.y1 + 9 + (int16_t)row * TETRIS_CELL_PITCH);

            tetris_draw_cell(layer, x, y, tetris_get_cell_color(cell_value));
        }
    }
}

/*
 * 请求 LVGL 重绘棋盘和 NEXT 区域。
 *
 * 游戏数据变化后只需要调用这个函数，
 * 不需要重新创建任何控件。
 */
static void tetris_refresh_cells(void)
{
    if (tetris_board_obj != 0)
    {
        lv_obj_invalidate(tetris_board_obj);
    }

    if (tetris_next_obj != 0)
    {
        lv_obj_invalidate(tetris_next_obj);
    }
}

/*
 * 将游戏状态枚举转换为屏幕显示的英文文本。
 */
static const char *tetris_get_state_text(TetrisGameState_t state)
{
    switch (state)
    {
        case TETRIS_STATE_READY:
            return "\xe5\x87\x86\xe5\xa4\x87";

        case TETRIS_STATE_RUNNING:
            return "\xe8\xbf\x9b\xe8\xa1\x8c\xe4\xb8\xad";

        case TETRIS_STATE_PAUSED:
            return "\xe6\x9a\x82\xe5\x81\x9c";

        case TETRIS_STATE_GAME_OVER:
            return "\xe6\xb8\xb8\xe6\x88\x8f\xe7\xbb\x93\xe6\x9d\x9f";

        default:
            return "\xe6\x9c\xaa\xe7\x9f\xa5";
    }
}

/*
 * 根据游戏状态选择状态文字颜色。
 */
static lv_color_t tetris_get_state_color(TetrisGameState_t state)
{
    switch (state)
    {
        case TETRIS_STATE_READY:
            return UI_COLOR_LIGHT_BLUE;

        case TETRIS_STATE_RUNNING:
            return UI_COLOR_TEXT;

        case TETRIS_STATE_PAUSED:
            return UI_COLOR_LIGHT_BLUE;

        case TETRIS_STATE_GAME_OVER:
            return UI_COLOR_RED;

        default:
            return UI_COLOR_TEXT_DIM;
    }
}

/*
 * 刷新右侧的分数、消行数和游戏状态。
 */
static void tetris_refresh_info(void)
{
    if (tetris_score_label != 0)
    {
        lv_label_set_text_fmt(tetris_score_label, "%06lu", (unsigned long)tetris_game.score);
    }

    if (tetris_lines_label != 0)
    {
        lv_label_set_text_fmt(tetris_lines_label, "%03u", (unsigned int)tetris_game.lines);
    }

    if (tetris_state_label != 0)
    {
        lv_label_set_text(tetris_state_label, tetris_get_state_text(tetris_game.state));

        lv_obj_set_style_text_color( tetris_state_label, tetris_get_state_color(tetris_game.state), LV_PART_MAIN);
    }
}

/*
 * 游戏数据发生变化后的统一刷新入口。
 *
 * 棋盘与 NEXT 通过重绘更新；
 * 分数、消行数和状态通过标签文字更新。
 */
static void tetris_refresh_ui(void)
{
    tetris_refresh_cells();
    tetris_refresh_info();
}

/*
 * 自动下落定时器回调。
 *
 * 只有 RUNNING 状态才执行下落。
 * READY、PAUSED、GAME OVER 状态下不做任何事。
 */
static void tetris_drop_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (tetris_game.state != TETRIS_STATE_RUNNING)
    {
        return;
    }

    /*
     * 执行一次下落。
     *
     * 如果触底，TetrisGame_Tick() 会负责：
     * 固定方块、消行、更新分数、生成下一方块，
     * 或将状态改为 GAME OVER。
     */
    (void)TetrisGame_Tick(&tetris_game);

    tetris_refresh_ui();
}

/*
 * 创建自动下落定时器。
 */
static void tetris_start_drop_timer(void)
{
    /*
     * 防止重复进入页面时残留旧定时器。
     */
    if (tetris_drop_timer != 0)
    {
        lv_timer_delete(tetris_drop_timer);
        tetris_drop_timer = 0;
    }

    tetris_drop_timer = lv_timer_create(tetris_drop_timer_cb, TETRIS_DROP_PERIOD_MS, NULL);
}

/*
 * 删除自动下落定时器。
 *
 * 退出俄罗斯方块页面前必须调用，
 * 避免定时器继续操作已经被导航器删除的页面对象。
 */
static void tetris_stop_drop_timer(void)
{
    if (tetris_drop_timer != 0)
    {
        lv_timer_delete(tetris_drop_timer);
        tetris_drop_timer = 0;
    }
}

/*
 * 创建俄罗斯方块完整页面。
 *
 * 此函数由 ui_nav.c 调用。
 * 每次从桌面进入俄罗斯方块时，导航管理器会清空旧页面，
 * 然后重新调用本函数绘制新页面。
 */
void ui_page_tetris_create(lv_obj_t *parent)
{
    lv_obj_t *board;
    lv_obj_t *next_box;

    /*
     * 每次进入俄罗斯方块页面时重新初始化游戏。
     *
     * 初始化后状态为 READY：
     * - 主棋盘为空；
     * - NEXT 中已经准备好下一个方块；
     * - 当前不会自动下落。
     */
    TetrisGame_Init(&tetris_game);

    /* 统一页头由公共组件创建，坐标和样式与其他应用保持一致。 */
    ui_create_page_header(parent, "\xe4\xbf\x84\xe7\xbd\x97\xe6\x96\xaf\xe6\x96\xb9\xe5\x9d\x97", "\xe6\xb8\xb8\xe6\x88\x8f / 01");

    /* ==================== 左侧主棋盘区域 ==================== */

    /*
    * 创建主棋盘背景对象。
    *
    * 后续棋盘内部所有方块都通过 DRAW_POST 事件绘制，
    * 不再创建 180 个子对象。
    */
    board = ui_create_shape(parent, TETRIS_BOARD_X, TETRIS_BOARD_Y, TETRIS_BOARD_W, TETRIS_BOARD_H, UI_COLOR_APP_BG);

    tetris_board_obj = board;

    /*
    * DRAW_POST 确保小方块绘制在棋盘背景和边框之上。
    */
    lv_obj_add_event_cb(tetris_board_obj, tetris_board_draw_event_cb, LV_EVENT_DRAW_POST, NULL);

    /* 主棋盘使用浅蓝色细边框。 */
    lv_obj_set_style_border_width(board, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(board, UI_COLOR_LIGHT_BLUE, LV_PART_MAIN);

    /* ==================== 右侧得分信息区域 ==================== */

    /* 分数标题与初始数值。 */
    ui_create_text(parent, "\xe5\x88\x86\xe6\x95\xb0", 150, 46, &ui_font_12, UI_COLOR_TEXT_DIM);

    tetris_score_label = ui_create_text(parent, "000000", 150, 62, &ui_font_14, UI_COLOR_TEXT);

    /* 消行数标题与初始数值。 */
    ui_create_text(parent, "\xe6\xb6\x88\xe8\xa1\x8c", 150, 91, &ui_font_12, UI_COLOR_TEXT_DIM);

    tetris_lines_label = ui_create_text(parent, "000", 150, 107, &ui_font_14, UI_COLOR_TEXT);

    /* ==================== 下一方块预览区域 ==================== */

    /* 下一方块标题。 */
    ui_create_text(parent, "\xe4\xb8\x8b\xe4\xb8\x80\xe5\x9d\x97", 216, 86, &ui_font_12, UI_COLOR_TEXT_DIM);

    /* 创建下一方块预览框。 */
    next_box = ui_create_shape(parent, 216, 102, 70, 58, UI_COLOR_APP_BG);

    /*
    * 保存 NEXT 预览对象指针。
    */
    tetris_next_obj = next_box;

    /*
    * NEXT 方块同样通过绘制事件完成，
    * 不再创建 16 个独立 LVGL 对象。
    */
    lv_obj_add_event_cb(tetris_next_obj, tetris_next_draw_event_cb, LV_EVENT_DRAW_POST, NULL);
        

    /* 预览框使用辅助蓝色边框，弱于主棋盘边框。 */
    lv_obj_set_style_border_width(next_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(next_box, UI_COLOR_AUX_BLUE, LV_PART_MAIN);


    /* ==================== 游戏状态区域 ==================== */

    /* 状态区上方的分割线。 */
    ui_create_shape(parent, 150, 177, 136, 1, UI_COLOR_AUX_BLUE);

    /* 状态标题。 */
    ui_create_text(parent, "\xe7\x8a\xb6\xe6\x80\x81", 150, 187, &ui_font_12, UI_COLOR_TEXT_DIM);

    /*
     * 当前静态状态为 READY。
     * 后续可能显示：
     * READY、RUNNING、PAUSED、GAME OVER。
     */
    tetris_state_label = ui_create_text(parent, "\xe5\x87\x86\xe5\xa4\x87", 150, 203, &ui_font_14, UI_COLOR_LIGHT_BLUE);

     /* ==================== 底部操作提示 ==================== */

    /*
     * 当前仅显示未来操作说明。
     * 实际按键控制等接入 tetris_game.c 后再实现。
     */
    ui_create_text(parent, "\xe7\xa1\xae\xe8\xae\xa4\xe5\xbc\x80\xe5\xa7\x8b / \xe5\xb7\xa6\xe5\x8f\xb3\xe7\xa7\xbb\xe5\x8a\xa8 / \xe4\xb8\x8a\xe9\x94\xae\xe6\x97\x8b\xe8\xbd\xac", 20, 222, &ui_font_12, UI_COLOR_TEXT_DIM);

    /*
    * 进行第一次完整刷新。
    */
    tetris_refresh_ui();

    /*
    * 创建自动下落定时器。
    *
    * READY 状态下定时器不会下落；
    * 按 ENTER 开始游戏后才真正生效。
    */
    tetris_start_drop_timer();
}

/*
 * 处理俄罗斯方块页面的实体按键。
 *
 * 按键事件由 freertos.c 中的 UI 任务检测，
 * 然后通过 ui_nav_handle_key() 分发到当前页面。
 *
 * 当前页面收到按键后，根据 key 的类型调用游戏逻辑层，
 * 页面层本身不直接修改棋盘数组。
 */
void ui_page_tetris_on_key(BSP_Key_t key)
{
    if (key == BSP_KEY_ESC)
    {
        ui_nav_pop(); // 导航器切页前会调用 ui_page_tetris_leave() 清理定时器
        return; // 返回桌面后结束本次按键处理
    }

    /*
     * 确认键的作用：
     *
     * READY：
     *   开始一局新游戏。
     *
     * RUNNING：
     *   暂停游戏。
     *
     * PAUSED：
     *   继续游戏。
     *
     * GAME_OVER：
     *   重新开始一局。
     */
    if (key == BSP_KEY_ENTER)
    {
        if ((tetris_game.state == TETRIS_STATE_READY) ||
            (tetris_game.state == TETRIS_STATE_GAME_OVER))
        {
            TetrisGame_Start(&tetris_game);
        }
        else
        {
            TetrisGame_TogglePause(&tetris_game);
        }

        /*
         * 游戏状态改变后，请求棋盘和 NEXT 区域重新绘制。
         */
        tetris_refresh_ui();
        return;
    }

    /*
     * 暂停状态下不允许移动方块。
     * ESC 和 ENTER 不受此限制，
     * 因为暂停时仍然需要能够返回或继续游戏。
     */
    if (tetris_game.state != TETRIS_STATE_RUNNING)
    {
        return;
    }

    /*
     * 上键：顺时针旋转当前方块。
     */
    if (key == BSP_KEY_UP)
    {
        (void)TetrisGame_Rotate(&tetris_game);
        tetris_refresh_ui();
        return;
    }

    /*
     * 左键：向左移动一格。
     */
    if (key == BSP_KEY_LEFT)
    {
        (void)TetrisGame_MoveLeft(&tetris_game);
        tetris_refresh_ui();
        return;
    }

    /*
     * 右键：向右移动一格。
     */
    if (key == BSP_KEY_RIGHT)
    {
        (void)TetrisGame_MoveRight(&tetris_game);
        tetris_refresh_ui();
        return;
    }

    /*
     * 下键：执行一次下落。
     *
     * 当前先下落一格；
     * 后续加入 LVGL 定时器后，下键还可以设计为加速下落。
     */
    if (key == BSP_KEY_DOWN)
    {
        (void)TetrisGame_Tick(&tetris_game);
        tetris_refresh_ui();
        return;
    }
}

/*
 * 离开俄罗斯方块页面前释放页面私有资源。
 *
 * 导航器会在删除页面控件之前调用本函数，
 * 避免下落定时器继续访问已经销毁的棋盘和标签对象。
 */
void ui_page_tetris_leave(void)
{
    tetris_stop_drop_timer(); // 停止并删除自动下落定时器
}
