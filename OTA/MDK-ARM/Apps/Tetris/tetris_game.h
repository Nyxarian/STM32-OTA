#ifndef __TETRIS_GAME_H__
#define __TETRIS_GAME_H__

/*
 * 俄罗斯方块核心逻辑层。
 *
 * 本文件不包含任何 LVGL 控件，也不负责绘制画面。
 * 它只定义：
 * - 游戏棋盘；
 * - 当前方块；
 * - 下一个方块；
 * - 游戏状态；
 * - 移动、旋转、下落和读取棋盘等接口。
 *
 * 这样设计后，即使以后更换 LCD 或 UI 框架，
 * 俄罗斯方块的核心规则也不需要重新编写。
 */

#include <stdint.h>

/* 标准俄罗斯方块棋盘为 10 列。 */
#define TETRIS_BOARD_COLS       10U

/*
 * LCD 高度有限，这里使用 18 行。
 * 每个格子计划显示为 9 x 9 像素。
 */
#define TETRIS_BOARD_ROWS       18U

/*
 * 每一种俄罗斯方块都可以放入一个 4 x 4 点阵中。
 */
#define TETRIS_SHAPE_SIZE       4U

/* 标准俄罗斯方块共有 7 种。 */
#define TETRIS_PIECE_COUNT      7U

/*
 * 棋盘格子的数值定义。
 *
 * 0：空白格子；
 * 1～7：已经固定在棋盘上的不同方块。
 */
#define TETRIS_CELL_EMPTY       0U

/*
 * 七种标准俄罗斯方块类型。
 *
 * 数值从 0 开始，存入棋盘时会加 1，
 * 因为棋盘中的 0 已经表示空白。
 */
typedef enum
{
    TETRIS_PIECE_I = 0,  /* 长条形 */
    TETRIS_PIECE_O,      /* 正方形 */
    TETRIS_PIECE_T,      /* T 形 */
    TETRIS_PIECE_S,      /* S 形 */
    TETRIS_PIECE_Z,      /* Z 形 */
    TETRIS_PIECE_J,      /* J 形 */
    TETRIS_PIECE_L       /* L 形 */
} TetrisPieceType_t;

/*
 * 游戏当前运行状态。
 */
typedef enum
{
    TETRIS_STATE_READY = 0,  /* 等待玩家开始 */
    TETRIS_STATE_RUNNING,    /* 游戏正在运行 */
    TETRIS_STATE_PAUSED,     /* 游戏暂停 */
    TETRIS_STATE_GAME_OVER   /* 游戏结束 */
} TetrisGameState_t;

/*
 * 一次自动下落产生的结果。
 *
 * 页面层可以根据返回结果决定是否刷新分数、
 * 播放提示效果或显示 GAME OVER。
 */
typedef enum
{
    TETRIS_EVENT_NONE = 0,       /* 本次没有发生变化 */
    TETRIS_EVENT_MOVED,          /* 方块成功向下移动 */
    TETRIS_EVENT_PIECE_LOCKED,   /* 方块触底并固定 */
    TETRIS_EVENT_LINES_CLEARED,  /* 本次固定后发生了消行 */
    TETRIS_EVENT_GAME_OVER       /* 新方块无法生成，游戏结束 */
} TetrisGameEvent_t;

/*
 * 当前正在下落的方块。
 */
typedef struct
{
    TetrisPieceType_t type;  /* 方块类型 */
    uint8_t rotation;        /* 当前旋转状态：0～3 */

    /*
     * 方块左上角在棋盘中的位置。
     *
     * 使用有符号类型是因为方块生成或旋转过程中，
     * 部分位置有可能暂时小于 0。
     */
    int8_t x;
    int8_t y;
} TetrisPiece_t;

/*
 * 一局俄罗斯方块的全部运行数据。
 */
typedef struct
{
    /*
     * 已经固定的棋盘数据。
     *
     * board[行][列]
     *
     * 0 表示空格；
     * 1～7 表示不同类型的固定方块。
     *
     * 当前正在下落的方块不会立即写入该数组，
     * 只有触底后才正式合并进棋盘。
     */
    uint8_t board[TETRIS_BOARD_ROWS][TETRIS_BOARD_COLS];

    /* 当前正在下落的方块。 */
    TetrisPiece_t current_piece;

    /* 下一次将要生成的方块类型。 */
    TetrisPieceType_t next_piece;

    /* 当前游戏状态。 */
    TetrisGameState_t state;

    /* 当前得分。 */
    uint32_t score;

    /* 当前累计消除的行数。 */
    uint16_t lines;

    /* 当前等级，后续用于控制自动下落速度。 */
    uint8_t level;

    /*
     * 简易随机数发生器使用的内部状态。
     * 后续可以使用 HAL_GetTick() 作为随机种子。
     */
    uint32_t random_state;

    /*
     * 七袋随机算法的数据。
     *
     * 每一袋都包含 I、O、T、S、Z、J、L 七种方块，
     * 洗牌后依次取出，可以避免玩家长时间等不到某一种方块。
     */
    uint8_t piece_bag[TETRIS_PIECE_COUNT];

    /* 当前已经取到七袋中的第几个方块。 */
    uint8_t bag_index;

} TetrisGame_t;

/*
 * 初始化游戏对象。
 *
 * 功能：
 * - 清空棋盘；
 * - 清零分数和消行数；
 * - 设置初始等级；
 * - 设置状态为 READY；
 * - 初始化随机方块系统。
 */
void TetrisGame_Init(TetrisGame_t *game);

/*
 * 设置随机种子。
 *
 * 页面层可以在开始游戏前传入 HAL_GetTick()，
 * 避免每次开机后的方块顺序都完全相同。
 */
void TetrisGame_SetRandomSeed(TetrisGame_t *game, uint32_t seed);

/*
 * 开始一局新游戏。
 *
 * 功能：
 * - 清空旧棋盘；
 * - 清空旧分数；
 * - 生成第一个方块；
 * - 将游戏状态改为 RUNNING。
 */
void TetrisGame_Start(TetrisGame_t *game);

/*
 * 暂停或继续游戏。
 *
 * RUNNING 状态调用后进入 PAUSED；
 * PAUSED 状态调用后恢复 RUNNING。
 */
void TetrisGame_TogglePause(TetrisGame_t *game);

/*
 * 尝试让当前方块向左移动一格。
 *
 * 返回 1：移动成功；
 * 返回 0：左侧存在墙壁或其他方块，无法移动。
 */
uint8_t TetrisGame_MoveLeft(TetrisGame_t *game);

/*
 * 尝试让当前方块向右移动一格。
 */
uint8_t TetrisGame_MoveRight(TetrisGame_t *game);

/*
 * 尝试让当前方块旋转 90 度。
 *
 * 当前先实现基本旋转；
 * 后续可以继续加入贴墙旋转补偿。
 */
uint8_t TetrisGame_Rotate(TetrisGame_t *game);

/*
 * 让当前方块直接落到最低位置。
 *
 * 返回值表示本次一共下降了多少格。
 */
uint8_t TetrisGame_HardDrop(TetrisGame_t *game);

/*
 * 执行一次自动下落。
 *
 * 正常情况下由 LVGL 定时器周期调用：
 * - 如果下方为空，方块下降一格；
 * - 如果下方被挡住，方块固定到棋盘；
 * - 固定后检查消行；
 * - 然后生成下一个方块。
 */
TetrisGameEvent_t TetrisGame_Tick(TetrisGame_t *game);

/*
 * 读取页面上应该显示的某个棋盘格。
 *
 * 该函数会同时考虑：
 * - 已经固定在 board[][] 中的方块；
 * - 当前正在下落的方块。
 *
 * 因此 tetris_page.c 不需要理解方块形状，
 * 只需要循环调用本函数，然后按照返回值画颜色。
 */
uint8_t TetrisGame_GetCell(const TetrisGame_t *game,
                           uint8_t row,
                           uint8_t col);

/*
 * 读取“下一方块”4 x 4 预览区域中的某个格子。
 *
 * 返回 0：该位置为空；
 * 返回 1～7：该位置需要显示对应颜色。
 */
uint8_t TetrisGame_GetNextCell(const TetrisGame_t *game,
                               uint8_t row,
                               uint8_t col);

#endif /* __TETRIS_GAME_H__ */