/*
 * 俄罗斯方块核心逻辑。
 *
 * 本文件不创建任何 LVGL 控件，只处理游戏数据。
 *
 * 当前阶段完成：
 * 1. 七种俄罗斯方块的形状定义；
 * 2. 每种方块的四种旋转状态；
 * 3. 简易随机数发生器；
 * 4. 七袋随机算法；
 * 5. 游戏数据初始化；
 * 6. 下一方块预览数据读取。
 *
 * 后续阶段继续增加：
 * 1. 碰撞检测；
 * 2. 左右移动；
 * 3. 旋转；
 * 4. 自动下落；
 * 5. 固定方块；
 * 6. 消行与计分。
 */

#include "tetris_game.h"

/*
 * 方块形状位图。
 *
 * 每个方块都使用一个 4×4 网格表示。
 *
 * 16 位数据的排列方式如下：
 *
 * bit  0  bit  1  bit  2  bit  3
 * bit  4  bit  5  bit  6  bit  7
 * bit  8  bit  9  bit 10  bit 11
 * bit 12  bit 13  bit 14  bit 15
 *
 * bit 为 1：这个位置有方块；
 * bit 为 0：这个位置为空。
 *
 * 例如：
 *
 * 0x00F0
 *
 * 二进制对应：
 *
 * 0000
 * 1111
 * 0000
 * 0000
 *
 * 表示横向的 I 形方块。
 *
 * 数组下标：
 *
 * tetris_shapes[方块类型][旋转状态]
 */
static const uint16_t tetris_shapes[TETRIS_PIECE_COUNT][4] =
{
    /* I 形：横、竖、横、竖。 */
    { 0x00F0U, 0x4444U, 0x0F00U, 0x2222U },

    /* O 形：旋转后外观不变。 */
    { 0x0660U, 0x0660U, 0x0660U, 0x0660U },

    /* T 形：四种旋转状态。 */
    { 0x0072U, 0x0262U, 0x0270U, 0x0232U },

    /* S 形：四种旋转状态。 */
    { 0x0036U, 0x0462U, 0x0360U, 0x0231U },

    /* Z 形：四种旋转状态。 */
    { 0x0063U, 0x0264U, 0x0630U, 0x0132U },

    /* J 形：四种旋转状态。 */
    { 0x0071U, 0x0226U, 0x0470U, 0x0322U },

    /* L 形：四种旋转状态。 */
    { 0x0074U, 0x0622U, 0x0170U, 0x0223U }
};

/*
 * 生成一个新的伪随机数。
 *
 * 这里使用线性同余算法。
 * 它不是密码学随机数，但适合产生游戏方块顺序。
 */
static uint32_t tetris_random_next(TetrisGame_t *game)
{
    game->random_state = game->random_state * 1664525UL + 1013904223UL;

    return game->random_state;
}

/*
 * 重新生成并打乱一个“七袋”。
 *
 * 七袋中一定包含七种方块各一个，
 * 洗牌后按顺序取出。
 */
static void tetris_refill_bag(TetrisGame_t *game)
{
    int8_t i;
    uint8_t j;
    uint8_t temp;

    /* 先依次放入 I、O、T、S、Z、J、L。 */
    for (i = 0; i < (int8_t)TETRIS_PIECE_COUNT; i++)
    {
        game->piece_bag[i] = (uint8_t)i;
    }

    /*
     * Fisher-Yates 洗牌。
     *
     * 从最后一个元素开始，
     * 随机选择一个前面的元素与它交换。
     */
    for (i = (int8_t)TETRIS_PIECE_COUNT - 1; i > 0; i--)
    {
        j = (uint8_t)(tetris_random_next(game) % ((uint32_t)i + 1UL));

        temp = game->piece_bag[i];
        game->piece_bag[i] = game->piece_bag[j];
        game->piece_bag[j] = temp;
    }

    /* 新的一袋从第 0 个位置开始读取。 */
    game->bag_index = 0U;
}

/*
 * 从七袋随机系统中取出一个方块。
 *
 * 当前袋子取完后，自动重新生成下一袋。
 */
static TetrisPieceType_t tetris_take_piece_from_bag(TetrisGame_t *game)
{
    TetrisPieceType_t piece;

    if (game->bag_index >= TETRIS_PIECE_COUNT)
    {
        tetris_refill_bag(game);
    }

    piece = (TetrisPieceType_t)game->piece_bag[game->bag_index];
    game->bag_index++;

    return piece;
}

/*
 * 判断某种方块的某个局部格子是否有内容。
 *
 * type：方块类型；
 * rotation：旋转状态，范围为 0～3；
 * row/col：4×4 方块位图中的局部坐标。
 *
 * 返回：
 * 1：该位置有方块；
 * 0：该位置为空，或者参数越界。
 */
static uint8_t tetris_shape_has_cell(TetrisPieceType_t type, uint8_t rotation, uint8_t row, uint8_t col)
{
    uint16_t mask;
    uint8_t bit_index;

    /* 检查方块类型和局部坐标。 */
    if (((uint8_t)type >= TETRIS_PIECE_COUNT) || (row >= TETRIS_SHAPE_SIZE) || (col >= TETRIS_SHAPE_SIZE))
    {
        return 0U;
    }

    /*
     * rotation 只保留最低两位，
     * 这样 0、1、2、3 以外的数值也会回到 0～3。
     */
    rotation &= 0x03U;
    mask = tetris_shapes[(uint8_t)type][rotation];

    /* 将 4×4 二维坐标转换为 0～15 的位编号。 */
    bit_index = (uint8_t)(row * TETRIS_SHAPE_SIZE + col);

    /* 检查对应二进制位是否为 1。 */
    if ((mask & ((uint16_t)1U << bit_index)) != 0U)
    {
        return 1U;
    }

    return 0U;
}

/*
 * 检查一个方块能否放在指定棋盘位置。
 *
 * 这里负责检查：
 * 1. 是否撞到棋盘左边界；
 * 2. 是否撞到棋盘右边界；
 * 3. 是否撞到棋盘底部；
 * 4. 是否与已经固定的方块重叠。
 *
 * 棋盘顶部以上的位置允许存在，
 * 因为方块生成或旋转时可能暂时位于屏幕上方。
 */
static uint8_t tetris_can_place(const TetrisGame_t *game, TetrisPieceType_t type, uint8_t rotation, int8_t x, int8_t y)
{
    uint8_t local_row;
    uint8_t local_col;
    int16_t board_row;
    int16_t board_col;

    if (game == 0)
    {
        return 0U;
    }

    /* 检查方块 4×4 位图中的每一个格子。 */
    for (local_row = 0U; local_row < TETRIS_SHAPE_SIZE; local_row++)
    {
        for (local_col = 0U; local_col < TETRIS_SHAPE_SIZE; local_col++)
        {
            /*
             * 位图中为 0 的位置不属于方块，
             * 不需要进行碰撞判断。
             */
            if (tetris_shape_has_cell(type, rotation, local_row, local_col) == 0U)
            {
                continue;
            }

            /*
             * 将方块内部坐标转换为棋盘坐标。
             */
            board_row = (int16_t)y + (int16_t)local_row;
            board_col = (int16_t)x + (int16_t)local_col;

            /*
             * 左右超出棋盘，说明撞墙。
             */
            if ((board_col < 0) || (board_col >= (int16_t)TETRIS_BOARD_COLS))
            {
                return 0U;
            }

            /*
             * 行坐标超过棋盘底部，说明触底。
             */
            if (board_row >= (int16_t)TETRIS_BOARD_ROWS)
            {
                return 0U;
            }

            /*
             * board_row 小于 0 时，方块位于棋盘顶部上方，
             * 这种情况允许存在，不访问 board 数组。
             */
            if (board_row < 0)
            {
                continue;
            }

            /*
             * 对应棋盘位置不是空白，
             * 说明撞到了已经固定的方块。
             */
            if (game->board[(uint8_t)board_row][(uint8_t)board_col] != TETRIS_CELL_EMPTY)
            {
                return 0U;
            }
        }
    }
    return 1U;
}

/*
 * 生成一个新的当前方块。
 *
 * 生成过程：
 * 1. 把 next_piece 变成 current_piece；
 * 2. 从七袋中取得新的 next_piece；
 * 3. 设置初始旋转角度；
 * 4. 放到棋盘顶部中央；
 * 5. 检查出生位置是否被挡住。
 */
static uint8_t tetris_spawn_piece(TetrisGame_t *game)
{
    if (game == 0)
    {
        return 0U;
    }

    /* NEXT 方块正式进入主棋盘。 */
    game->current_piece.type = game->next_piece;

    /* 再取一个方块，作为新的 NEXT。 */
    game->next_piece = tetris_take_piece_from_bag(game);

    /* 新方块从旋转状态 0 开始。 */
    game->current_piece.rotation = 0U;

    /*
     * 10 列棋盘中，4 格宽方块放在 x=3 附近，
     * 视觉上位于棋盘顶部中央。
     */
    game->current_piece.x = 3;
    game->current_piece.y = 0;

    /*
     * 如果出生位置已经被固定方块挡住，
     * 说明棋盘堆满，游戏结束。
     */
    if (tetris_can_place(game, game->current_piece.type, game->current_piece.rotation, game->current_piece.x, game->current_piece.y) == 0U)
    {
        game->state = TETRIS_STATE_GAME_OVER;
        return 0U;
    }
    return 1U;
}

/*
 * 尝试移动当前方块。
 *
 * dx：水平方向偏移；
 * dy：垂直方向偏移。
 *
 * 示例：
 * dx=-1、dy=0：向左移动；
 * dx= 1、dy=0：向右移动；
 * dx= 0、dy=1：向下移动。
 */
static uint8_t tetris_try_move(TetrisGame_t *game, int8_t dx, int8_t dy)
{
    int8_t new_x;
    int8_t new_y;

    /* 只有 RUNNING 状态允许移动。 */
    if ((game == 0) || (game->state != TETRIS_STATE_RUNNING))
    {
        return 0U;
    }

    new_x = (int8_t)(game->current_piece.x + dx);
    new_y = (int8_t)(game->current_piece.y + dy);

    /*
     * 先检查目标位置，
     * 确认没有碰撞后才能真正修改坐标。
     */
    if (tetris_can_place(game, game->current_piece.type, game->current_piece.rotation, new_x, new_y) == 0U)
    {
        return 0U;
    }

    game->current_piece.x = new_x;
    game->current_piece.y = new_y;

    return 1U;
}

/*
 * 将当前正在下落的方块固定到棋盘。
 *
 * 固定后，方块的格子会正式写入 board[][]，
 * 从此成为碰撞检测的一部分。
 */
static void tetris_lock_current_piece(TetrisGame_t *game)
{
    uint8_t local_row;
    uint8_t local_col;
    int16_t board_row;
    int16_t board_col;
    uint8_t cell_value;

    if (game == 0)
    {
        return;
    }

    /* 方块类型加 1，转换为棋盘中保存的格子数值。 */
    cell_value = (uint8_t)game->current_piece.type + 1U;

    for (local_row = 0U; local_row < TETRIS_SHAPE_SIZE; local_row++)
    {
        for (local_col = 0U; local_col < TETRIS_SHAPE_SIZE; local_col++)
        {
            /* 位图中的空格不写入棋盘。 */
            if (tetris_shape_has_cell(game->current_piece.type, game->current_piece.rotation, local_row, local_col) == 0U)
            {
                continue;
            }

            board_row = (int16_t)game->current_piece.y + local_row;
            board_col = (int16_t)game->current_piece.x + local_col;

            /*
             * 只写入棋盘有效范围。
             * 正常运行时，碰撞检测已经保证不会超出左右和底部。
             */
            if ((board_row >= 0) && (board_row < (int16_t)TETRIS_BOARD_ROWS) && (board_col >= 0) && (board_col < (int16_t)TETRIS_BOARD_COLS))
            {
                game->board[(uint8_t)board_row][(uint8_t)board_col] = cell_value;
            }
        }
    }
}

/*
 * 检查并删除已经填满的行。
 *
 * 算法从棋盘底部向顶部扫描：
 * - 满行直接跳过，相当于删除；
 * - 非满行复制到当前最低空位；
 * - 最后把顶部剩余位置清空。
 *
 * 返回本次消除的总行数，范围为 0～4。
 */
static uint8_t tetris_clear_full_lines(TetrisGame_t *game)
{
    int16_t source_row;
    int16_t target_row;
    uint8_t col;
    uint8_t row_is_full;
    uint8_t cleared_lines = 0U;

    if (game == 0)
    {
        return 0U;
    }

    target_row = (int16_t)TETRIS_BOARD_ROWS - 1;

    for (source_row = (int16_t)TETRIS_BOARD_ROWS - 1; source_row >= 0; source_row--)
    {
        row_is_full = 1U;

        /* 只要发现一个空格，这一行就没有填满。 */
        for (col = 0U; col < TETRIS_BOARD_COLS; col++)
        {
            if (game->board[(uint8_t)source_row][col] == TETRIS_CELL_EMPTY)
            {
                row_is_full = 0U;
                break;
            }
        }

        /*
         * 满行不复制到新位置，
         * 相当于从棋盘中删除。
         */
        if (row_is_full != 0U)
        {
            cleared_lines++;
            continue;
        }

        /*
         * 将非满行移动到当前目标行。
         * 没有发生消行时 source_row 与 target_row 相同，
         * 不需要重复复制。
         */
        if (target_row != source_row)
        {
            for (col = 0U; col < TETRIS_BOARD_COLS; col++)
            {
                game->board[(uint8_t)target_row][col] = game->board[(uint8_t)source_row][col];
            }
        }

        target_row--;
    }

    /* 清空棋盘顶部因为下移产生的空行。 */
    while (target_row >= 0)
    {
        for (col = 0U; col < TETRIS_BOARD_COLS; col++)
        {
            game->board[(uint8_t)target_row][col] = TETRIS_CELL_EMPTY;
        }
        target_row--;
    }
    return cleared_lines;
}

/*
 * 根据本次消行数量更新分数、总行数和等级。
 *
 * 基础计分：
 * 1 行：100 分；
 * 2 行：300 分；
 * 3 行：500 分；
 * 4 行：800 分。
 *
 * 实际得分还会乘以当前等级。
 */
static void tetris_update_score(TetrisGame_t *game, uint8_t cleared_lines)
{
    uint32_t base_score = 0UL;

    if ((game == 0) || (cleared_lines == 0U))
    {
        return;
    }

    switch (cleared_lines)
    {
        case 1U:
            base_score = 100UL;
            break;

        case 2U:
            base_score = 300UL;
            break;

        case 3U:
            base_score = 500UL;
            break;

        default:
            /* 四行以及异常的大于四行情况统一按 800 分处理。 */
            base_score = 800UL;
            break;
    }

    game->score += base_score * game->level;
    game->lines = (uint16_t)(game->lines + cleared_lines);

    /*
     * 每累计消除 10 行提升一级。
     * 等级最低为 1。
     */
    game->level = (uint8_t)(game->lines / 10U + 1U);
}

/*
 * 初始化俄罗斯方块游戏对象。
 *
 * 初始化后状态为 READY，
 * 此时页面可以显示开始提示，但不会自动下落。
 */
void TetrisGame_Init(TetrisGame_t *game)
{
    uint8_t row;
    uint8_t col;

    /* 防止传入无效指针。 */
    if (game == 0)
    {
        return;
    }

    /* 清空 18×10 棋盘。 */
    for (row = 0U; row < TETRIS_BOARD_ROWS; row++)
    {
        for (col = 0U; col < TETRIS_BOARD_COLS; col++)
        {
            game->board[row][col] = TETRIS_CELL_EMPTY;
        }
    }

    /*
     * 设置当前方块初始数据。
     *
     * READY 状态下它暂时不会显示，
     * 开始游戏时会重新设置。
     */
    game->current_piece.type = TETRIS_PIECE_I;
    game->current_piece.rotation = 0U;
    game->current_piece.x = 3;
    game->current_piece.y = 0;

    /* 清零游戏统计数据。 */
    game->score = 0UL;
    game->lines = 0U;
    game->level = 1U;

    /* 初始状态为等待开始。 */
    game->state = TETRIS_STATE_READY;

    /*
     * 设置默认随机种子。
     *
     * 后续页面层可以使用 HAL_GetTick()
     * 重新设置更有变化的种子。
     */
    game->random_state = 0x55002026UL;

    /*
     * 将 bag_index 设置为袋子长度，
     * 让第一次取方块时自动生成并洗牌。
     */
    game->bag_index = TETRIS_PIECE_COUNT;

    /* 预先取得下一方块，用于右侧 NEXT 区域显示。 */
    game->next_piece = tetris_take_piece_from_bag(game);
}

/*
 * 设置随机种子。
 *
 * 一般在开始游戏前调用一次。
 */
void TetrisGame_SetRandomSeed(TetrisGame_t *game, uint32_t seed)
{
    if (game == 0)
    {
        return;
    }

    /*
     * 如果传入 0，则使用固定种子。
     * 这样可以避免随机算法出现不符合预期的情况。
     */
    if (seed == 0UL)
    {
        seed = 0x55002026UL;
    }

    game->random_state = seed;

    /*
     * 强制下一次取方块时重新生成七袋，
     * 使新的种子立即生效。
     */
    game->bag_index = TETRIS_PIECE_COUNT;
    game->next_piece = tetris_take_piece_from_bag(game);
}

/*
 * 读取 NEXT 预览框中的一个格子。
 *
 * row 和 col 范围均为 0～3。
 *
 * 返回：
 * 0：该格为空；
 * 1～7：该位置属于对应类型的方块。
 */
uint8_t TetrisGame_GetNextCell(const TetrisGame_t *game, uint8_t row, uint8_t col)
{
    uint16_t mask;
    uint8_t bit_index;

    /* 检查游戏对象指针。 */
    if (game == 0)
    {
        return TETRIS_CELL_EMPTY;
    }

    /* 检查预览坐标是否超出 4×4 范围。 */
    if ((row >= TETRIS_SHAPE_SIZE) ||
        (col >= TETRIS_SHAPE_SIZE))
    {
        return TETRIS_CELL_EMPTY;
    }

    /* 检查方块类型是否有效。 */
    if ((uint8_t)game->next_piece >= TETRIS_PIECE_COUNT)
    {
        return TETRIS_CELL_EMPTY;
    }

    /*
     * NEXT 预览统一显示方块的第 0 种旋转状态。
     */
    mask = tetris_shapes[game->next_piece][0];

    /*
     * 将二维坐标换算为位编号：
     *
     * bit_index = 行 × 4 + 列
     */
    bit_index = (uint8_t)(row * TETRIS_SHAPE_SIZE + col);

    /*
     * 判断对应位是否为 1。
     */
    if ((mask & ((uint16_t)1U << bit_index)) == 0U)
    {
        return TETRIS_CELL_EMPTY;
    }

    /*
     * 棋盘值 0 表示空白，
     * 所以方块类型需要加 1 后返回。
     */
    return (uint8_t)game->next_piece + 1U;
}

/*
 * 开始一局新游戏。
 *
 * 此函数会清除上一局的棋盘和统计数据，
 * 然后在顶部生成第一个方块。
 */
void TetrisGame_Start(TetrisGame_t *game)
{
    uint8_t row;
    uint8_t col;

    if (game == 0)
    {
        return;
    }

    /* 清空所有已经固定的棋盘格。 */
    for (row = 0U; row < TETRIS_BOARD_ROWS; row++)
    {
        for (col = 0U; col < TETRIS_BOARD_COLS; col++)
        {
            game->board[row][col] = TETRIS_CELL_EMPTY;
        }
    }

    /* 清零本局统计数据。 */
    game->score = 0UL;
    game->lines = 0U;
    game->level = 1U;

    /* 先进入运行状态，移动和下落函数才会生效。 */
    game->state = TETRIS_STATE_RUNNING;

    /* 生成本局第一个方块。 */
    (void)tetris_spawn_piece(game);
}

/*
 * 尝试让当前方块向左移动一格。
 *
 * 返回 1：移动成功；
 * 返回 0：无法移动或游戏没有运行。
 */
uint8_t TetrisGame_MoveLeft(TetrisGame_t *game)
{
    return tetris_try_move(game, -1, 0);
}

/*
 * 尝试让当前方块向右移动一格。
 */
uint8_t TetrisGame_MoveRight(TetrisGame_t *game)
{
    return tetris_try_move(game, 1, 0);
}

/*
 * 读取主棋盘上某一个格子最终应该显示的内容。
 *
 * 此函数会合并两部分数据：
 * 1. board[][] 中已经固定的方块；
 * 2. current_piece 当前正在移动的方块。
 *
 * 页面层只需要循环读取每个格子，
 * 不需要自己计算当前方块位置。
 */
uint8_t TetrisGame_GetCell(const TetrisGame_t *game, uint8_t row, uint8_t col)
{
    int16_t local_row;
    int16_t local_col;

    if (game == 0)
    {
        return TETRIS_CELL_EMPTY;
    }

    /* 防止棋盘坐标越界。 */
    if ((row >= TETRIS_BOARD_ROWS) || (col >= TETRIS_BOARD_COLS))
    {
        return TETRIS_CELL_EMPTY;
    }

    /*
     * RUNNING 和 PAUSED 状态都需要显示当前方块。
     * 暂停时方块虽然不移动，但不能从画面中消失。
     */
    if ((game->state == TETRIS_STATE_RUNNING) || (game->state == TETRIS_STATE_PAUSED))
    {
        /*
         * 将棋盘坐标转换为当前方块内部的局部坐标。
         */
        local_row = (int16_t)row - (int16_t)game->current_piece.y;
        local_col = (int16_t)col - (int16_t)game->current_piece.x;

        /*
         * 只有落在当前方块的 4×4 区域内，
         * 才需要查询方块位图。
         */
        if ((local_row >= 0) && (local_row < (int16_t)TETRIS_SHAPE_SIZE) && (local_col >= 0) && (local_col < (int16_t)TETRIS_SHAPE_SIZE))
        {
            if (tetris_shape_has_cell( game->current_piece.type, game->current_piece.rotation, (uint8_t)local_row, (uint8_t)local_col) != 0U)
            {
                /*
                 * 方块枚举从 0 开始，
                 * 棋盘显示值从 1 开始，因此加 1。
                 */
                return (uint8_t)game->current_piece.type + 1U;
            }
        }
    }

    /*
     * 当前移动方块没有覆盖该位置，
     * 返回已经固定的棋盘格。
     */
    return game->board[row][col];
}

/*
 * 暂停或继续游戏。
 *
 * READY 和 GAME_OVER 状态调用本函数不会发生变化。
 */
void TetrisGame_TogglePause(TetrisGame_t *game)
{
    if (game == 0)
    {
        return;
    }

    if (game->state == TETRIS_STATE_RUNNING)
    {
        game->state = TETRIS_STATE_PAUSED;
    }
    else if (game->state == TETRIS_STATE_PAUSED)
    {
        game->state = TETRIS_STATE_RUNNING;
    }
}

/*
 * 将当前方块顺时针旋转 90 度。
 *
 * 当正常旋转会撞墙时，依次尝试向左或向右补偿，
 * 这种处理称为简化版“踢墙”。
 */
uint8_t TetrisGame_Rotate(TetrisGame_t *game)
{
    static const int8_t wall_kicks[] = { 0, -1, 1, -2, 2 };

    uint8_t new_rotation;
    uint8_t i;
    int8_t new_x;

    if ((game == 0) || (game->state != TETRIS_STATE_RUNNING))
    {
        return 0U;
    }

    /* 四种旋转状态按照 0→1→2→3→0 循环。 */
    new_rotation = (uint8_t)((game->current_piece.rotation + 1U) & 0x03U);

    for (i = 0U; i < (uint8_t)(sizeof(wall_kicks) / sizeof(wall_kicks[0])); i++)
    {
        new_x = (int8_t)(game->current_piece.x + wall_kicks[i]);

        if (tetris_can_place(game, game->current_piece.type, new_rotation, new_x, game->current_piece.y) != 0U)
        {
            game->current_piece.rotation = new_rotation;
            game->current_piece.x = new_x;
            return 1U;
        }
    }

    /* 所有补偿位置都发生碰撞，本次旋转失败。 */
    return 0U;
}

/*
 * 执行一次自动下落。
 *
 * 正常情况下由页面定时器周期调用。
 */
TetrisGameEvent_t TetrisGame_Tick(TetrisGame_t *game)
{
    uint8_t cleared_lines;

    if ((game == 0) || (game->state != TETRIS_STATE_RUNNING))
    {
        return TETRIS_EVENT_NONE;
    }

    /*
     * 下方没有障碍时，只下降一格。
     */
    if (tetris_try_move(game, 0, 1) != 0U)
    {
        return TETRIS_EVENT_MOVED;
    }

    /*
     * 无法继续下降，说明已经触底或撞到其他方块。
     */
    tetris_lock_current_piece(game);

    /* 固定后检查完整行。 */
    cleared_lines = tetris_clear_full_lines(game);
    tetris_update_score(game, cleared_lines);

    /*
     * 生成下一个方块。
     * 如果顶部已经放不下新方块，则游戏结束。
     */
    if (tetris_spawn_piece(game) == 0U)
    {
        return TETRIS_EVENT_GAME_OVER;
    }

    if (cleared_lines != 0U)
    {
        return TETRIS_EVENT_LINES_CLEARED;
    }

    return TETRIS_EVENT_PIECE_LOCKED;
}

/*
 * 让当前方块直接落到底部并固定。
 *
 * 返回方块一共下降了多少格。
 */
uint8_t TetrisGame_HardDrop(TetrisGame_t *game)
{
    uint8_t moved_rows = 0U;

    if ((game == 0) || (game->state != TETRIS_STATE_RUNNING))
    {
        return 0U;
    }

    /*
     * 持续向下移动，直到碰撞检测不允许继续下降。
     */
    while (tetris_try_move(game, 0, 1) != 0U)
    {
        moved_rows++;
    }

    /*
     * 此时方块已经到达最低位置。
     * 再执行一次 Tick，让它固定、消行并生成新方块。
     */
    (void)TetrisGame_Tick(game);

    return moved_rows;
}
