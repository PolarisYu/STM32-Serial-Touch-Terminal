#include "app_terminal.h"
#include "stm32f4xx_hal.h" // 需要包含以获取 USB_OTG_FS_PERIPH_BASE
#include <string.h>
#include <stdio.h>


// --- 屏幕尺寸 (从 lcddev 获取, 但可用于静态数组) ---
#define SCREEN_WIDTH        800
#define SCREEN_HEIGHT       480

// --- 颜色定义 ---
#define COLOR_BG            WHITE       // 背景色 (画纸)
#define COLOR_LOG_BG        LGRAY       // 日志区域背景
#define COLOR_LOG           BLACK       // 日志文字
#define COLOR_LOG_TEXT      YELLOW       // 日志文字
#define COLOR_KEY_BG        DARKBLUE    // 按键背景
#define COLOR_KEY_TEXT      WHITE       // 按键文字
#define COLOR_KEY_SPECIAL   RED         // 特殊按键 (Send/Clear)
#define COLOR_KEY_PRESSED   GREEN       // 按键按下
#define COLOR_BORDER        BLACK       // 边框
#define COLOR_TITLE         WHITE       // 标题文字

// --- 区域定义 ---
#define ZONE_TITLE_H        30         // 顶部标题栏高度
#define ZONE_RX_LOG_Y       ZONE_TITLE_H  // 接收日志Y坐标
#define ZONE_RX_LOG_H       120        // 接收日志高度
#define ZONE_TX_LOG_Y       (ZONE_RX_LOG_Y + ZONE_RX_LOG_H + 5) // 发送日志Y坐标 (155)
#define ZONE_TX_LOG_H       120        // 发送日志高度
#define ZONE_KEYBOARD_Y     (ZONE_TX_LOG_Y + ZONE_TX_LOG_H + 5) // 键盘Y坐标 (280)
#define ZONE_KEYBOARD_H     (SCREEN_HEIGHT - ZONE_KEYBOARD_Y)   // 键盘高度 (200)

#define KEYBOARD_INPUT_Y    (ZONE_KEYBOARD_Y + 5) // 键盘输入框Y
#define KEYBOARD_GRID_Y     (KEYBOARD_INPUT_Y + 25) // 键盘按键Y

// --- 存储和日志定义 ---
#define MAX_LOG_LINES       6       // 日志区显示行数 (RX 和 TX 各自)
#define MAX_LOG_WIDTH       98      // 日志每行最大字符数
#define MAX_STORAGE_SLOTS   4       // 数据存储槽数量
#define MAX_STORAGE_WIDTH   100     // 存储槽最大字符数
#define MAX_KEY_BUFFER_LEN  90      // 键盘输入框最大长度
#define MAX_CHUNK_BUFFER_LEN 256    // 长数据重组缓冲区长度

// --- 触控按键结构体 ---
typedef struct {
    uint16_t x, y, w, h;
    const char* label;
    uint32_t color;
    bool pressed; // 按键状态
} TouchKey_t;

/**
 * @brief 触摸状态枚举
 */
typedef enum {
    TOUCH_STATE_IDLE = 0,       // 无触摸
    TOUCH_STATE_PRESSED,        // 刚按下（仅触发一次）
    TOUCH_STATE_HOLDING,        // 保持按下
    TOUCH_STATE_RELEASED        // 刚抬起（仅触发一次）
} TouchState_t;

/**
 * @brief 触摸上下文结构体
 */
typedef struct {
    TouchState_t state;         // 当前状态
    uint8_t last_point_count;   // 上一次的触摸点数量
    uint16_t press_x;           // 按下时的 X 坐标
    uint16_t press_y;           // 按下时的 Y 坐标
    uint16_t current_x;         // 当前 X 坐标
    uint16_t current_y;         // 当前 Y 坐标
    uint32_t press_time;        // 按下时的时间戳（用于长按检测）
    TouchKey_t* pressed_key;    // 当前按下的按键
} TouchContext_t;

// 全局触摸上下文
static TouchContext_t g_touch_ctx = {
    .state = TOUCH_STATE_IDLE,
    .last_point_count = 0,
    .press_x = 0,
    .press_y = 0,
    .current_x = 0,
    .current_y = 0,
    .press_time = 0,
    .pressed_key = NULL
};

#define RELEASE_DEBOUNCE_FRAMES 40 // 连续 40 帧检测不到触摸才确认抬起
static uint8_t g_release_debounce_count = 0; // 抬起去抖计数器


// --- USB Bus ID ---
static uint8_t g_busid = 0;

// --- 触控按键定义 ---
#define KEY_W 40
#define KEY_H 35
#define KEY_M 5 // Margin

// 键盘按键布局 (QWERTY)
static TouchKey_t keyboard_layout[] = {
    {KEY_M*1+KEY_W*0, KEYBOARD_GRID_Y, KEY_W, KEY_H, "1", COLOR_KEY_BG, false},
    {KEY_M*2+KEY_W*1, KEYBOARD_GRID_Y, KEY_W, KEY_H, "2", COLOR_KEY_BG, false},
    {KEY_M*3+KEY_W*2, KEYBOARD_GRID_Y, KEY_W, KEY_H, "3", COLOR_KEY_BG, false},
    {KEY_M*4+KEY_W*3, KEYBOARD_GRID_Y, KEY_W, KEY_H, "4", COLOR_KEY_BG, false},
    {KEY_M*5+KEY_W*4, KEYBOARD_GRID_Y, KEY_W, KEY_H, "5", COLOR_KEY_BG, false},
    {KEY_M*6+KEY_W*5, KEYBOARD_GRID_Y, KEY_W, KEY_H, "6", COLOR_KEY_BG, false},
    {KEY_M*7+KEY_W*6, KEYBOARD_GRID_Y, KEY_W, KEY_H, "7", COLOR_KEY_BG, false},
    {KEY_M*8+KEY_W*7, KEYBOARD_GRID_Y, KEY_W, KEY_H, "8", COLOR_KEY_BG, false},
    {KEY_M*9+KEY_W*8, KEYBOARD_GRID_Y, KEY_W, KEY_H, "9", COLOR_KEY_BG, false},
    {KEY_M*10+KEY_W*9, KEYBOARD_GRID_Y, KEY_W, KEY_H, "0", COLOR_KEY_BG, false},

    {KEY_M*1+KEY_W*0, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "Q", COLOR_KEY_BG, false},
    {KEY_M*2+KEY_W*1, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "W", COLOR_KEY_BG, false},
    {KEY_M*3+KEY_W*2, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "E", COLOR_KEY_BG, false},
    {KEY_M*4+KEY_W*3, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "R", COLOR_KEY_BG, false},
    {KEY_M*5+KEY_W*4, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "T", COLOR_KEY_BG, false},
    {KEY_M*6+KEY_W*5, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "Y", COLOR_KEY_BG, false},
    {KEY_M*7+KEY_W*6, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "U", COLOR_KEY_BG, false},
    {KEY_M*8+KEY_W*7, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "I", COLOR_KEY_BG, false},
    {KEY_M*9+KEY_W*8, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "O", COLOR_KEY_BG, false},
    {KEY_M*10+KEY_W*9, KEYBOARD_GRID_Y+KEY_H+KEY_M, KEY_W, KEY_H, "P", COLOR_KEY_BG, false},

    {KEY_M*2+KEY_W*1, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "A", COLOR_KEY_BG, false},
    {KEY_M*3+KEY_W*2, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "S", COLOR_KEY_BG, false},
    {KEY_M*4+KEY_W*3, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "D", COLOR_KEY_BG, false},
    {KEY_M*5+KEY_W*4, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "F", COLOR_KEY_BG, false},
    {KEY_M*6+KEY_W*5, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "G", COLOR_KEY_BG, false},
    {KEY_M*7+KEY_W*6, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "H", COLOR_KEY_BG, false},
    {KEY_M*8+KEY_W*7, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "J", COLOR_KEY_BG, false},
    {KEY_M*9+KEY_W*8, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "K", COLOR_KEY_BG, false},
    {KEY_M*10+KEY_W*9, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*2, KEY_W, KEY_H, "L", COLOR_KEY_BG, false},

    {KEY_M*1 + KEY_W*0, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, (KEY_W*2 + KEY_M*1), KEY_H, "SEND", COLOR_KEY_SPECIAL, false},

    {KEY_M*3+KEY_W*2, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, KEY_W, KEY_H, "Z", COLOR_KEY_BG, false},
    {KEY_M*4+KEY_W*3, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, KEY_W, KEY_H, "X", COLOR_KEY_BG, false},
    {KEY_M*5+KEY_W*4, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, KEY_W, KEY_H, "C", COLOR_KEY_BG, false},
    {KEY_M*6+KEY_W*5, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, KEY_W, KEY_H, "V", COLOR_KEY_BG, false},
    {KEY_M*7+KEY_W*6, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, KEY_W, KEY_H, "B", COLOR_KEY_BG, false},
    {KEY_M*8+KEY_W*7, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, KEY_W, KEY_H, "N", COLOR_KEY_BG, false},
    {KEY_M*9+KEY_W*8, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, KEY_W, KEY_H, "M", COLOR_KEY_BG, false},
    {KEY_M*10+KEY_W*9, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*3, KEY_W, KEY_H, "Bksp", COLOR_KEY_SPECIAL, false},

    // {KEY_M*3, KEYBOARD_GRID_Y+(KEY_H+KEY_M)*4, (KEY_W+KEY_M)*8-KEY_M, KEY_H, "SEND", COLOR_KEY_SPECIAL, false},
};
#define KEYBOARD_KEY_COUNT (sizeof(keyboard_layout) / sizeof(TouchKey_t))

// 控制按钮布局 (在键盘右侧)
#define C_KEY_W 100
#define C_KEY_H 28
#define C_KEY_M 8
#define C_KEY_X (KEY_M*11+KEY_W*10 + 20) // 键盘右侧
#define C_KEY_Y KEYBOARD_GRID_Y

static TouchKey_t control_buttons[] = {
    {C_KEY_X, C_KEY_Y+(C_KEY_H+C_KEY_M)*0, C_KEY_W, C_KEY_H, "Store TX", COLOR_KEY_BG, false},
    {C_KEY_X, C_KEY_Y+(C_KEY_H+C_KEY_M)*1, C_KEY_W, C_KEY_H, "Query TX", COLOR_KEY_BG, false},
    {C_KEY_X, C_KEY_Y+(C_KEY_H+C_KEY_M)*2, C_KEY_W, C_KEY_H, "Clear TX", COLOR_KEY_BG, false},
    {C_KEY_X + C_KEY_W + C_KEY_M, C_KEY_Y+(C_KEY_H+C_KEY_M)*0, C_KEY_W, C_KEY_H, "Store RX", COLOR_KEY_BG, false},
    {C_KEY_X + C_KEY_W + C_KEY_M, C_KEY_Y+(C_KEY_H+C_KEY_M)*1, C_KEY_W, C_KEY_H, "Query RX", COLOR_KEY_BG, false},
    {C_KEY_X + C_KEY_W + C_KEY_M, C_KEY_Y+(C_KEY_H+C_KEY_M)*2, C_KEY_W, C_KEY_H, "Clear RX", COLOR_KEY_BG, false},
    
    {C_KEY_X, C_KEY_Y+(C_KEY_H+C_KEY_M)*3.5, C_KEY_W*2 + C_KEY_M, C_KEY_H, "Send 8-Byte Chunks", COLOR_KEY_SPECIAL, false},
};
#define CONTROL_KEY_COUNT (sizeof(control_buttons) / sizeof(TouchKey_t))


// --- 全局缓冲区和状态 ---
// 日志
static char rx_log_lines[MAX_LOG_LINES][MAX_LOG_WIDTH] = {0};
static char tx_log_lines[MAX_LOG_LINES][MAX_LOG_WIDTH] = {0};
// static int rx_log_idx = 0;
// static int tx_log_idx = 0;
// 存储 (满足Req 4: 3条以上)
static char rx_storage[MAX_STORAGE_SLOTS][MAX_STORAGE_WIDTH] = {0};
static char tx_storage[MAX_STORAGE_SLOTS][MAX_STORAGE_WIDTH] = {0};
static int rx_storage_idx = 0;
static int tx_storage_idx = 0;
// 键盘
static char keyboard_buffer[MAX_KEY_BUFFER_LEN] = {0};
static int keyboard_buffer_idx = 0;
// 触摸状态
// static bool touch_was_pressed = false;
// 长数据重组 (高级Req 2)
static char chunk_buffer[MAX_CHUNK_BUFFER_LEN] = {0};
static int chunk_buffer_idx = 0;
static bool chunk_receiving = false;

/*
================================================================================
  3. 私有 (static) 函数原型 (移植自 main.c)
================================================================================
*/
static void draw_key(TouchKey_t *key);
static void draw_main_ui(void);
static void refresh_log_text(bool is_rx_zone);
static void draw_log_area(bool is_rx_zone);
static void add_to_log(bool is_rx_zone, const char* msg);
static void add_to_storage(bool is_rx, const char* msg);
static void query_storage(bool is_rx);
static void clear_storage(bool is_rx);
static TouchKey_t* find_key_at(int x, int y);
static void handle_touch_pressed(void);
static void reset_long_press_trigger(void);
// static uint8_t detect_swipe_gesture(void);
// static void display_touch_debug_info(void);
static void handle_key_press(TouchKey_t* key); // busid 从 g_busid 获取
// static void handle_long_press(TouchKey_t* key);
static void update_keyboard_buffer_display(void);
static void process_received_data(uint8_t* data, uint32_t len);
static void send_chunked_data(void); // busid 从 g_busid 获取

static void cdc_task_handler(void);
static void touch_task_handler(void);


/*
================================================================================
  4. 公共 API 函数实现
================================================================================
*/

/**
 * @brief 初始化终端应用
 */
int App_Terminal_Init(uint8_t busid)
{
    g_busid = busid; // 存储 busid 供全局使用

    /* USB CDC 驱动初始化 */
    cdc_acm_init(g_busid, USB_OTG_FS_PERIPH_BASE);

    /* LCD 驱动初始化 */
    LCD_Init(); 
    LCD_Display_Dir(1); // 1 = 横屏 (800x480)
    HAL_Delay(10); // 稍等一会儿，确保 LCD 初始化完成

    
    /* 触摸驱动初始化 */
    if (GT9147_Init(lcddev.width, lcddev.height, lcddev.dir) != 0)
    {
        // GT9147 初始化失败 (未检测到)
        LCD_ShowString(10, 10, 400, 20, 16, (uint8_t*)"Touch Init FAILED! Check I2C.");
        return -1;
    }
    
    /* 4. 绘制主 UI */
    draw_main_ui();
    
    return 0;
}

/**
 * @brief 终端应用的任务轮询
 */
/**
 * @brief 终端应用的任务轮询
 */
void App_Terminal_Tasks(void)
{
    // 任务1: 处理USB接收
    cdc_task_handler();
    
    // 任务2: 处理触摸输入
    touch_task_handler();
    
    // 任务3 (可选): 显示触摸调试信息
    #ifdef ENABLE_TOUCH_DEBUG
    display_touch_debug_info();
    #endif
    
    // 任务4: 定期调用USB发送（确保数据及时发出）
    cdc_acm_try_send(g_busid);
}


/*
================================================================================
  5. 私有 (static) 函数实现 (移植自 main.c)
================================================================================
*/

/**
  * @brief 绘制一个按键
  */
static void draw_key(TouchKey_t *key)
{
    uint32_t color = key->pressed ? COLOR_KEY_PRESSED : key->color;
    LCD_Fill(key->x, key->y, key->x + key->w - 1, key->y + key->h - 1, color);
    LCD_DrawRectangle(key->x, key->y, key->x + key->w - 1, key->y + key->h - 1); // 黑框
    
    // 居中显示文字
    int text_x = key->x + (key->w - (strlen(key->label) * 8)) / 2;
    int text_y = key->y + (key->h - 16) / 2;
    POINT_COLOR = COLOR_KEY_TEXT;
    LCD_ShowString(text_x, text_y, key->w, key->h, 16, (uint8_t*)key->label);
}

/**
  * @brief 绘制所有UI元素
  */
static void draw_main_ui(void)
{
    LCD_Clear(COLOR_BG);
    
    // 1. 绘制标题
    POINT_COLOR = COLOR_TITLE;
    LCD_Fill(0, 0, SCREEN_WIDTH - 1, ZONE_TITLE_H - 1, LGRAYBLUE);
    LCD_ShowString(10, 7, 300, 16, 16, (uint8_t*)"STM32 Serial Touch Terminal");
    
    // 2. 绘制日志区
    draw_log_area(true);  // RX
    draw_log_area(false); // TX

    // 3. 绘制键盘输入框
    POINT_COLOR = COLOR_BORDER;
    LCD_ShowString(10, KEYBOARD_INPUT_Y, 100, 16, 16, (uint8_t*)"Input:");
    update_keyboard_buffer_display(); // 绘制空的输入框
    
    // 4. 绘制键盘
    for (int i = 0; i < KEYBOARD_KEY_COUNT; i++) {
        draw_key(&keyboard_layout[i]);
    }
    
    // 5. 绘制控制按钮
    for (int i = 0; i < CONTROL_KEY_COUNT; i++) {
        draw_key(&control_buttons[i]);
    }
}

/**
  * @brief 仅刷新日志区域的文本（无背景填充）
  * @note  此函数速度快，用于滚动更新
  */
static void refresh_log_text(bool is_rx_zone)
{
    uint16_t y_start = is_rx_zone ? ZONE_RX_LOG_Y : ZONE_TX_LOG_Y;
    char (*log_lines)[MAX_LOG_WIDTH] = is_rx_zone ? rx_log_lines : tx_log_lines;
    
    // 关键：设置 LCD_ShowString 的背景色，使其能“擦除”旧文本
    BACK_COLOR = COLOR_LOG_BG; 
    POINT_COLOR = COLOR_LOG_TEXT; // (您在文件中定义了 YELLOW)
    
    for(int i = 0; i < MAX_LOG_LINES; i++) {
        // (x, y, width, height, size, *p)
        // 我们需要先用背景色填充该行，以防新字符串比旧字符串短
        LCD_Fill(10, y_start + 20 + (i * 16), SCREEN_WIDTH - 10, y_start + 20 + (i * 16) + 16, COLOR_LOG_BG);
        
        // 仅在字符串非空时绘制
        if (log_lines[i][0] != '\0') {
            LCD_ShowString(10, y_start + 20 + (i * 16), SCREEN_WIDTH - 20, 16, 16, (uint8_t*)log_lines[i]);
        }
    }
    
    // 恢复默认背景色
    BACK_COLOR = COLOR_BG;
}

/**
  * @brief 绘制(刷新)日志区域
  */
static void draw_log_area(bool is_rx_zone)
{
    uint16_t y_start = is_rx_zone ? ZONE_RX_LOG_Y : ZONE_TX_LOG_Y;
    uint16_t height = is_rx_zone ? ZONE_RX_LOG_H : ZONE_TX_LOG_H;
    char* title = is_rx_zone ? "PC -> MCU (Received)" : "MCU -> PC (Sent)";
    // char (*log_lines)[MAX_LOG_WIDTH] = is_rx_zone ? rx_log_lines : tx_log_lines;
    
    // 擦除区域
    LCD_Fill(0, y_start, SCREEN_WIDTH - 1, y_start + height - 1, COLOR_LOG_BG);
    // 绘制边框和标题
    POINT_COLOR = COLOR_BORDER;
    LCD_DrawRectangle(0, y_start, SCREEN_WIDTH - 1, y_start + height - 1);
    POINT_COLOR = COLOR_LOG;
    LCD_ShowString(5, y_start + 3, 200, 16, 16, (uint8_t*)title);
    
    // 绘制日志行
    refresh_log_text(is_rx_zone);
    // POINT_COLOR = COLOR_LOG_TEXT;
    // for(int i = 0; i < MAX_LOG_LINES; i++) {
    //     LCD_ShowString(10, y_start + 20 + (i * 16), SCREEN_WIDTH - 20, 16, 16, (uint8_t*)log_lines[i]);
    // }
}

/**
  * @brief 向日志区添加新消息 (循环滚动)
  */
static void add_to_log(bool is_rx_zone, const char* msg)
{
    char (*log_lines)[MAX_LOG_WIDTH] = is_rx_zone ? rx_log_lines : tx_log_lines;

    // 1. [滚动逻辑] 向上滚动字符串缓冲区
    //    (将 line[1]~[5] 的内容移动到 line[0]~[4])
    memmove(log_lines[0], log_lines[1], (MAX_LOG_LINES - 1) * MAX_LOG_WIDTH);
    
    // 2. 将新消息复制到最后一行
    strncpy(log_lines[MAX_LOG_LINES - 1], msg, MAX_LOG_WIDTH - 1);
    log_lines[MAX_LOG_LINES - 1][MAX_LOG_WIDTH - 1] = '\0'; // 确保null终止

    // 3. [高效重绘] 调用快速的文本刷新函数
    refresh_log_text(is_rx_zone);
}

/**
  * @brief 将消息存入存储槽 (Req 4)
  */
static void add_to_storage(bool is_rx, const char* msg)
{
    if (is_rx) {
        if (rx_storage_idx >= MAX_STORAGE_SLOTS) rx_storage_idx = 0; // 循环覆盖
        strncpy(rx_storage[rx_storage_idx], msg, MAX_STORAGE_WIDTH - 1);
        rx_storage[rx_storage_idx][MAX_STORAGE_WIDTH - 1] = '\0';
        rx_storage_idx++;
        // 简单提示
        add_to_log(true, "--> Data Stored.");
    } else {
        if (tx_storage_idx >= MAX_STORAGE_SLOTS) tx_storage_idx = 0;
        strncpy(tx_storage[tx_storage_idx], msg, MAX_STORAGE_WIDTH - 1);
        tx_storage[tx_storage_idx][MAX_STORAGE_WIDTH - 1] = '\0';
        tx_storage_idx++;
        add_to_log(false, "--> Data Stored.");
    }
}

/**
  * @brief 查询并显示存储的数据 (Req 5)
  */
static void query_storage(bool is_rx)
{
    bool is_empty = true;
    char temp_msg[MAX_STORAGE_WIDTH + 10];
    
    add_to_log(is_rx, "--- Query Storage Start ---");
    char (*storage)[MAX_STORAGE_WIDTH] = is_rx ? rx_storage : tx_storage;
    
    for (int i = 0; i < MAX_STORAGE_SLOTS; i++) {
        if (storage[i][0] != '\0') {
            is_empty = false;
            snprintf(temp_msg, sizeof(temp_msg), "Slot %d: %s", i, storage[i]);
            add_to_log(is_rx, temp_msg);
        }
    }
    if (is_empty) {
        add_to_log(is_rx, "Storage is empty.");
    }
    add_to_log(is_rx, "--- Query Storage End ---");
}

/**
  * @brief 清除存储 (Req 5)
  */
static void clear_storage(bool is_rx)
{
    if (is_rx) {
        memset(rx_storage, 0, sizeof(rx_storage));
        rx_storage_idx = 0;
        
        // 清除日志的内存缓冲区
        memset(rx_log_lines, 0, sizeof(rx_log_lines));
        // 重绘该区域（背景+空文本）
        draw_log_area(true);
        // 添加一条新消息
        add_to_log(true, "Receive Storage Cleared.");
    } else {
        memset(tx_storage, 0, sizeof(tx_storage));
        tx_storage_idx = 0;
        
        // 清除日志的内存缓冲区
        memset(tx_log_lines, 0, sizeof(tx_log_lines));
        // 重绘该区域（背景+空文本）
        draw_log_area(false);
        // 添加一条新消息
        add_to_log(false, "Send Storage Cleared.");
    }
}

/**
  * @brief 在 (x, y) 坐标处查找被按下的键
  */
static TouchKey_t* find_key_at(int x, int y)
{
    // 检查键盘
    for (int i = 0; i < KEYBOARD_KEY_COUNT; i++) {
        TouchKey_t* key = &keyboard_layout[i];
        if (x > key->x && x < (key->x + key->w) && y > key->y && y < (key->y + key->h)) {
            return key;
        }
    }
    // 检查控制按钮
    for (int i = 0; i < CONTROL_KEY_COUNT; i++) {
        TouchKey_t* key = &control_buttons[i];
        if (x > key->x && x < (key->x + key->w) && y > key->y && y < (key->y + key->h)) {
            return key;
        }
    }
    return NULL;
}

/**
  * @brief 刷新键盘输入框的显示
  */
static void update_keyboard_buffer_display(void)
{
    // 擦除旧内容
    LCD_Fill(10 + 6*8, KEYBOARD_INPUT_Y, C_KEY_X - 10, KEYBOARD_INPUT_Y + 16, COLOR_BG);
    // 绘制新内容
    POINT_COLOR = BLACK;
    LCD_ShowString(10 + 6*8, KEYBOARD_INPUT_Y, SCREEN_WIDTH, 16, 16, (uint8_t*)keyboard_buffer);
}

/**
  * @brief 处理按下事件（仅触发一次）
  */
static void handle_touch_pressed(void)
{
    // 这里可以添加按下音效、震动反馈等
    // 例如：play_click_sound();
    // extern void reset_long_press_trigger(void); // 声明一个函数
    reset_long_press_trigger(); // 调用它
    // 暂时不执行按键逻辑，等到抬起时再执行（防止误触）
    // 如果需要立即响应，可以在这里调用 handle_key_press()
}

static bool long_press_triggered = false; // 全局长按触发标志

/**
  * @brief 处理保持按下事件（持续触发）
  */
static void handle_touch_holding(void)
{
    // 可以在这里实现：
    // 1. 长按检测
    // 2. 拖动/滑动检测
    // 3. 显示按下坐标（用于调试）
    
    // 示例：长按检测（1秒）
    uint32_t hold_duration = HAL_GetTick() - g_touch_ctx.press_time;
    if (hold_duration > 1000)
    {
        // 长按触发（仅执行一次）
        // static bool long_press_triggered = false;
        if (!long_press_triggered && g_touch_ctx.pressed_key)
        {
            long_press_triggered = true;
            // 执行长按逻辑
            // 例如：handle_long_press(g_touch_ctx.pressed_key);
        }
    }
}


static void reset_long_press_trigger(void) // 定义重置函数
{
    long_press_triggered = false;
}

/**
  * @brief 处理抬起事件（仅触发一次）
  */
static void handle_touch_released(void)
{
    // ⭐ 关键：只有在抬起时才执行按键逻辑（类似真实按钮）
    if (g_touch_ctx.pressed_key)
    {
        // 检查抬起位置是否还在按键区域内（防止滑出）
        TouchKey_t* release_key = find_key_at(g_touch_ctx.current_x, g_touch_ctx.current_y);
        
        if (release_key == g_touch_ctx.pressed_key)
        {
            // 在同一个按键上抬起，执行按键逻辑
            handle_key_press(g_touch_ctx.pressed_key);
        }
        else
        {
            // 滑出按键区域，取消操作
            add_to_log(false, "[Touch] Cancelled (slid out)");
        }
        
        // 清除按键引用
        g_touch_ctx.pressed_key = NULL;
    }
}

/**
  * @brief 检测滑动手势
  * @return 0=无手势, 1=上滑, 2=下滑, 3=左滑, 4=右滑
  */
// static uint8_t detect_swipe_gesture(void)
// {
//     if (g_touch_ctx.state != TOUCH_STATE_RELEASED)
//         return 0;
    
//     int16_t dx = g_touch_ctx.current_x - g_touch_ctx.press_x;
//     int16_t dy = g_touch_ctx.current_y - g_touch_ctx.press_y;
    
//     // 至少滑动 50 像素才算有效
//     const int16_t SWIPE_THRESHOLD = 50;
    
//     if (abs(dx) > abs(dy))
//     {
//         // 水平滑动
//         if (dx > SWIPE_THRESHOLD)
//             return 4; // 右滑
//         else if (dx < -SWIPE_THRESHOLD)
//             return 3; // 左滑
//     }
//     else
//     {
//         // 垂直滑动
//         if (dy > SWIPE_THRESHOLD)
//             return 2; // 下滑
//         else if (dy < -SWIPE_THRESHOLD)
//             return 1; // 上滑
//     }
    
//     return 0; // 无手势
// }

/**
  * @brief 在屏幕上显示触摸状态（调试用）
  */
// static void display_touch_debug_info(void)
// {
//     char debug_str[100];
    
//     // 擦除旧内容
//     LCD_Fill(600, 5, 790, 25, COLOR_BG);
    
//     // 显示状态
//     POINT_COLOR = RED;
//     switch (g_touch_ctx.state)
//     {
//         case TOUCH_STATE_PRESSED:
//             sprintf(debug_str, "PRESS (%d,%d)", g_touch_ctx.press_x, g_touch_ctx.press_y);
//             break;
//         case TOUCH_STATE_HOLDING:
//             sprintf(debug_str, "HOLD (%d,%d)", g_touch_ctx.current_x, g_touch_ctx.current_y);
//             break;
//         case TOUCH_STATE_RELEASED:
//             sprintf(debug_str, "RELEASE");
//             break;
//         default:
//             sprintf(debug_str, "IDLE");
//             break;
//     }
    
//     LCD_ShowString(600, 5, 190, 16, 16, (uint8_t*)debug_str);
// }

/**
  * @brief 处理按键的核心逻辑
  */
/**
  * @brief 处理按键的核心逻辑（抬起时调用）
  */
static void handle_key_press(TouchKey_t* key)
{
    if (key == NULL) return;

    // --- 1. 字符键 ---
    if (strlen(key->label) == 1) {
        if (keyboard_buffer_idx < MAX_KEY_BUFFER_LEN - 1) {
            keyboard_buffer[keyboard_buffer_idx++] = key->label[0];
            keyboard_buffer[keyboard_buffer_idx] = '\0';
            update_keyboard_buffer_display();
        }
    }
    // --- 2. 退格键 (Bksp) ---
    else if (strcmp(key->label, "Bksp") == 0) {
        if (keyboard_buffer_idx > 0) {
            keyboard_buffer_idx--;
            keyboard_buffer[keyboard_buffer_idx] = '\0';
            update_keyboard_buffer_display();
        }
    }
    // --- 3. 发送键 (SEND) ---
    else if (strcmp(key->label, "SEND") == 0) {
        if (keyboard_buffer_idx > 0) {
            cdc_acm_send_data(g_busid, (uint8_t*)keyboard_buffer, keyboard_buffer_idx);
            add_to_log(false, keyboard_buffer);
            keyboard_buffer_idx = 0;
            keyboard_buffer[0] = '\0';
            update_keyboard_buffer_display();
        }
    }
    // --- 4. 存储键 ---
    else if (strcmp(key->label, "Store TX") == 0) {
        if(keyboard_buffer_idx > 0) {
            add_to_storage(false, keyboard_buffer);
        }
    }
    else if (strcmp(key->label, "Store RX") == 0) {
        // [正确逻辑] 存储滚动日志的最后一行 (即数组的最后一条)
        int last_line_index = MAX_LOG_LINES - 1;
        
        if(rx_log_lines[last_line_index][0] != '\0') {
            add_to_storage(true, rx_log_lines[last_line_index]);
        }
    }
    // --- 5. 查询键 ---
    else if (strcmp(key->label, "Query TX") == 0) {
        query_storage(false);
    }
    else if (strcmp(key->label, "Query RX") == 0) {
        query_storage(true);
    }
    // --- 6. 清除键 ---
    else if (strcmp(key->label, "Clear TX") == 0) {
        clear_storage(false);
    }
    else if (strcmp(key->label, "Clear RX") == 0) {
        clear_storage(true);
    }
    // --- 7. 分包发送 ---
    else if (strcmp(key->label, "Send 8-Byte Chunks") == 0) {
        send_chunked_data();
    }
}

/**
  * @brief 处理长按逻辑（示例：长按 "Store TX" 清空输入框）
  */
// static void handle_long_press(TouchKey_t* key)
// {
//     if (strcmp(key->label, "Store TX") == 0)
//     {
//         // 长按 Store TX 清空输入框
//         keyboard_buffer_idx = 0;
//         keyboard_buffer[0] = '\0';
//         update_keyboard_buffer_display();
//         add_to_log(false, "[Long Press] Input cleared.");
//     }
//     else if (strcmp(key->label, "Bksp") == 0)
//     {
//         // 长按退格键清空所有
//         keyboard_buffer_idx = 0;
//         keyboard_buffer[0] = '\0';
//         update_keyboard_buffer_display();
//         add_to_log(false, "[Long Press] All cleared.");
//     }
// }

/**
  * @brief 任务1: 处理USB接收环形缓冲区的数据
  */
static void cdc_task_handler(void)
{
    static uint8_t rx_buf[256]; // 临时缓冲区
    
    if (cdc_acm_get_rx_available() > 0)
    {
        uint32_t len = cdc_acm_read_data(rx_buf, sizeof(rx_buf) - 1);
        if (len > 0)
        {
            rx_buf[len] = '\0'; // 确保null终止
            
            // 处理接收到的数据 (包括高级Req 7)
            process_received_data(rx_buf, len);
        }
    }
}

/**
  * @brief 处理接收到的数据 (包含分包重组逻辑)
  */
static void process_received_data(uint8_t* data, uint32_t len)
{
    char* str_data = (char*)data;
    
    // --- 高级功能 2: 分包重组 (Req 7) ---
    if (chunk_receiving) {
        if (strcmp(str_data, "END") == 0) {
            // 收到结束包
            chunk_receiving = false;
            add_to_log(true, "[Chunked] RECV END.");
            // 显示重组后的长数据
            add_to_log(true, chunk_buffer);
            add_to_storage(true, chunk_buffer); // 存起来
            // 清空重组缓冲区
            chunk_buffer_idx = 0;
            chunk_buffer[0] = '\0';
        } else {
            // 收到数据包
            add_to_log(true, str_data); // 显示 8 字节包
            // 拼接到重组缓冲区
            if (chunk_buffer_idx + len < MAX_CHUNK_BUFFER_LEN) {
                strncpy(&chunk_buffer[chunk_buffer_idx], str_data, len);
                chunk_buffer_idx += len;
                chunk_buffer[chunk_buffer_idx] = '\0';
            } else {
                add_to_log(true, "[Chunked] Buffer Overflow!");
                chunk_receiving = false;
            }
        }
    } else if (strcmp(str_data, "START") == 0) {
        // 收到起始包
        chunk_receiving = true;
        chunk_buffer_idx = 0;
        chunk_buffer[0] = '\0';
        add_to_log(true, "[Chunked] RECV START.");
    }
    // --- 基本功能: 显示和存储 (Req 2, 4) ---
    else {
        add_to_log(true, str_data);
        add_to_storage(true, str_data);
    }
}

/**
  * @brief 任务2: 处理触摸屏输入 (已修复竞态条件)
  */
static void touch_task_handler(void)
{
    GT_TouchPoint_t tp_data[GT_MAX_POINTS];
    uint8_t touch_count = GT9147_Scan(tp_data, GT_MAX_POINTS);
    
    // =========================================================================
    // 状态机：检测按下、保持、抬起
    // =========================================================================
    
    if (touch_count > 0)
    {
        // =====================================================================
        // 情况1: 有触摸点 (按下或保持)
        // =====================================================================
        
        // 只要检测到触摸，就重置“释放防抖”计数器
        g_release_debounce_count = 0;
        
        // 更新坐标
        g_touch_ctx.current_x = tp_data[0].x;
        g_touch_ctx.current_y = tp_data[0].y;
        
        if (g_touch_ctx.state == TOUCH_STATE_IDLE || g_touch_ctx.state == TOUCH_STATE_RELEASED)
        {
            // ------------------------------------------------------------------
            // 从 无触摸 -> 有触摸：这是一个新的按下事件
            // ------------------------------------------------------------------
            g_touch_ctx.state = TOUCH_STATE_PRESSED;
            g_touch_ctx.press_x = tp_data[0].x;
            g_touch_ctx.press_y = tp_data[0].y;
            g_touch_ctx.press_time = HAL_GetTick();
            
            // 查找被按下的按键
            TouchKey_t* key = find_key_at(g_touch_ctx.press_x, g_touch_ctx.press_y);
            if (key)
            {
                g_touch_ctx.pressed_key = key;
                key->pressed = true;
                draw_key(key); // 绘制按下效果
            }
            
            // [调用] 按下瞬间的处理
            handle_touch_pressed();
        }
        else
        {
            // ------------------------------------------------------------------
            // 从 有触摸 -> 有触摸：保持按下状态
            // ------------------------------------------------------------------
            g_touch_ctx.state = TOUCH_STATE_HOLDING;
            
            // [调用] 持续按下期间的处理
            handle_touch_holding();
        }
    }
    else
    {
        // =====================================================================
        // 情况2: 无触摸点 (可能是抬起，也可能是竞态条件)
        // =====================================================================
        
        if (g_touch_ctx.state == TOUCH_STATE_PRESSED || g_touch_ctx.state == TOUCH_STATE_HOLDING)
        {
            // ------------------------------------------------------------------
            // 从 有触摸 -> 无触摸：这是一个 *可能* 的抬起事件
            // ------------------------------------------------------------------
            
            // 增加防抖计数器
            g_release_debounce_count++;
            
            if (g_release_debounce_count >= RELEASE_DEBOUNCE_FRAMES)
            {
                // 连续 N 帧都读到 0，我们 *确认* 手指已抬起
                g_touch_ctx.state = TOUCH_STATE_RELEASED;
                
                // 恢复按键外观
                if (g_touch_ctx.pressed_key)
                {
                    g_touch_ctx.pressed_key->pressed = false;
                    draw_key(g_touch_ctx.pressed_key);
                }
                
                // [调用] 抬起瞬间的处理
                handle_touch_released();
                
                // 重置计数器
                g_release_debounce_count = 0;
            }
        }
        else
        {
            // ------------------------------------------------------------------
            // 从 无触摸 -> 无触摸：保持空闲状态
            // ------------------------------------------------------------------
            g_touch_ctx.state = TOUCH_STATE_IDLE;
            g_release_debounce_count = 0;
        }
    }
    
    // ⭐ 移除：g_touch_ctx.last_point_count 不再需要
    // g_touch_ctx.last_point_count = touch_count;
}

/**
  * @brief 高级功能 1: 发送一串长数据，按8字节分包 (Req 6)
  */
static void send_chunked_data(void)
{
    const char* long_data = "ThisIsALongDataStringSentIn8ByteChunksBySTM32";
    int data_len = strlen(long_data);
    char chunk[9]; // 8 字节 + null
    chunk[8] = '\0'; 
    
    add_to_log(false, "[Chunked] SEND START.");
    cdc_acm_send_data(g_busid, (uint8_t*)"START", 5);
    HAL_Delay(5); // 确保PC能处理
    
    for(int i = 0; i < data_len; i += 8)
    {
        int chunk_len = (data_len - i >= 8) ? 8 : (data_len - i);
        strncpy(chunk, &long_data[i], chunk_len);
        chunk[chunk_len] = '\0';
        
        add_to_log(false, chunk); // 在本地显示 8 字节包
        cdc_acm_send_data(g_busid, (uint8_t*)chunk, chunk_len);
        HAL_Delay(5); // 模拟连续发送
    }
    
    add_to_log(false, "[Chunked] SEND END.");
    cdc_acm_send_data(g_busid, (uint8_t*)"END", 3);
}