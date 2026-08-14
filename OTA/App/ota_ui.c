/* OTA 中文验证界面：用于确认显示、中文字体、进度条和六按键输入。 */
#include "ota_ui.h"
#include "lvgl.h"
#include "lv_port_indev.h"

/*
 * 中文采用明确的 UTF-8 字节序列，避免 ARM Compiler 5 对源码编码的处理差异。
 * 每个宏后的中文注释同时作为文本含义说明。
 */
#define UI_TEXT_TITLE             "\xe5\x9b\xba\xe4\xbb\xb6\xe5\x9c\xa8\xe7\xba\xbf\xe5\x8d\x87\xe7\xba\xa7" /* 固件在线升级 */
#define UI_TEXT_NETWORK_OFFLINE   "\xe7\xbd\x91\xe7\xbb\x9c\xef\xbc\x9a\xe6\x9c\xaa\xe8\xbf\x9e\xe6\x8e\xa5" /* 网络：未连接 */
#define UI_TEXT_NETWORK_ONLINE    "\xe7\xbd\x91\xe7\xbb\x9c\xef\xbc\x9a\xe5\xb7\xb2\xe8\xbf\x9e\xe6\x8e\xa5" /* 网络：已连接 */
#define UI_TEXT_CURRENT_VERSION   "\xe5\xbd\x93\xe5\x89\x8d\xe7\x89\x88\xe6\x9c\xac\xef\xbc\x9a\x31\x2e\x30\x2e\x30" /* 当前版本：1.0.0 */
#define UI_TEXT_LATEST_UNKNOWN    "\xe6\x9c\x80\xe6\x96\xb0\xe7\x89\x88\xe6\x9c\xac\xef\xbc\x9a\x2d\x2d" /* 最新版本：-- */
#define UI_TEXT_LATEST_DEMO       "\xe6\x9c\x80\xe6\x96\xb0\xe7\x89\x88\xe6\x9c\xac\xef\xbc\x9a\x31\x2e\x31\x2e\x30\xef\xbc\x88\xe7\x95\x8c\xe9\x9d\xa2\xe6\xbc\x94\xe7\xa4\xba\xef\xbc\x89" /* 最新版本：1.1.0（界面演示） */
#define UI_TEXT_PRESS_ENTER       "\xe6\x8c\x89\xe7\xa1\xae\xe8\xae\xa4\xe9\x94\xae\xe6\xb5\x8b\xe8\xaf\x95\xe5\xbd\x93\xe5\x89\x8d\xe6\x8c\x89\xe9\x92\xae" /* 按确认键测试当前按钮 */
#define UI_TEXT_TEST_OK           "\xe6\x98\xbe\xe7\xa4\xba\xe5\x92\x8c\xe6\x8c\x89\xe9\x94\xae\xe5\xb7\xa5\xe4\xbd\x9c\xe6\xad\xa3\xe5\xb8\xb8" /* 显示和按键工作正常 */
#define UI_TEXT_PROGRESS          "\xe4\xb8\x8b\xe8\xbd\xbd\xe8\xbf\x9b\xe5\xba\xa6" /* 下载进度 */
#define UI_TEXT_CHECK_UPDATE      "\xe6\xa3\x80\xe6\x9f\xa5\xe6\x9b\xb4\xe6\x96\xb0" /* 检查更新 */
#define UI_TEXT_RESET_SCREEN      "\xe9\x87\x8d\xe7\xbd\xae\xe7\x95\x8c\xe9\x9d\xa2" /* 重置界面 */

/* 需要在事件回调中更新的界面控件。 */
static lv_obj_t *network_label;
static lv_obj_t *latest_label;
static lv_obj_t *state_label;
static lv_obj_t *progress_bar;

static void check_update_event_cb(lv_event_t *event)
{
  /* 当前只验证交互效果，后续再接入真正的 ESP8266 检查更新流程。 */
  LV_UNUSED(event);

  lv_label_set_text(network_label, UI_TEXT_NETWORK_ONLINE);
  lv_label_set_text(latest_label, UI_TEXT_LATEST_DEMO);
  lv_label_set_text(state_label, UI_TEXT_TEST_OK);
  lv_bar_set_value(progress_bar, 100, LV_ANIM_ON);
}

static void reset_event_cb(lv_event_t *event)
{
  /* 将验证界面恢复到上电后的初始状态。 */
  LV_UNUSED(event);

  lv_label_set_text(network_label, UI_TEXT_NETWORK_OFFLINE);
  lv_label_set_text(latest_label, UI_TEXT_LATEST_UNKNOWN);
  lv_label_set_text(state_label, UI_TEXT_PRESS_ENTER);
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_coord_t x,
                               lv_event_cb_t event_cb)
{
  /* 创建一个能够加入按键焦点组的标准按钮。 */
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_size(button, 138, 34);
  lv_obj_set_pos(button, x, 194);
  lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);

  return button;
}

void ota_ui_init(void)
{
  /* 创建 320x240 的 OTA 状态验证页面。 */
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *title;
  lv_obj_t *version_panel;
  lv_obj_t *current_label;
  lv_obj_t *progress_title;
  lv_obj_t *check_button;
  lv_obj_t *reset_button;
  lv_group_t *group = lv_port_indev_get_group();

  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101827), LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_hex(0xE5E7EB), LV_PART_MAIN);
  lv_obj_set_style_text_font(screen, &ui_font_14, LV_PART_MAIN);
  lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

  title = lv_label_create(screen);
  lv_label_set_text(title, UI_TEXT_TITLE);
  lv_obj_set_style_text_color(title, lv_color_hex(0x60A5FA), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 12);

  network_label = lv_label_create(screen);
  lv_label_set_text(network_label, UI_TEXT_NETWORK_OFFLINE);
  lv_obj_set_style_text_color(network_label, lv_color_hex(0x9CA3AF), LV_PART_MAIN);
  lv_obj_align(network_label, LV_ALIGN_TOP_RIGHT, -12, 12);

  version_panel = lv_obj_create(screen);
  lv_obj_set_size(version_panel, 296, 82);
  lv_obj_set_pos(version_panel, 12, 43);
  lv_obj_set_style_bg_color(version_panel, lv_color_hex(0x1F2937), LV_PART_MAIN);
  lv_obj_set_style_border_width(version_panel, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(version_panel, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_all(version_panel, 12, LV_PART_MAIN);
  lv_obj_remove_flag(version_panel, LV_OBJ_FLAG_SCROLLABLE);

  current_label = lv_label_create(version_panel);
  lv_label_set_text(current_label, UI_TEXT_CURRENT_VERSION);
  lv_obj_align(current_label, LV_ALIGN_TOP_LEFT, 0, 0);

  latest_label = lv_label_create(version_panel);
  lv_label_set_text(latest_label, UI_TEXT_LATEST_UNKNOWN);
  lv_obj_align(latest_label, LV_ALIGN_TOP_LEFT, 0, 26);

  state_label = lv_label_create(version_panel);
  lv_label_set_text(state_label, UI_TEXT_PRESS_ENTER);
  lv_obj_set_style_text_color(state_label, lv_color_hex(0x9CA3AF), LV_PART_MAIN);
  lv_obj_align(state_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  progress_title = lv_label_create(screen);
  lv_label_set_text(progress_title, UI_TEXT_PROGRESS);
  lv_obj_set_pos(progress_title, 12, 137);

  progress_bar = lv_bar_create(screen);
  lv_obj_set_size(progress_bar, 296, 16);
  lv_obj_set_pos(progress_bar, 12, 160);
  lv_bar_set_range(progress_bar, 0, 100);
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x374151), LV_PART_MAIN);
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x22C55E), LV_PART_INDICATOR);

  check_button = create_button(screen, UI_TEXT_CHECK_UPDATE, 12, check_update_event_cb);
  reset_button = create_button(screen, UI_TEXT_RESET_SCREEN, 170, reset_event_cb);

  if (group != NULL)
  {
    /* 上下键在两个按钮之间移动焦点，确认键触发当前按钮。 */
    lv_group_add_obj(group, check_button);
    lv_group_add_obj(group, reset_button);
    lv_group_focus_obj(check_button);
  }
}
