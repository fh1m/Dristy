#include "ui.h"

#include "lcd.h"

#ifdef ISP_HEADLESS

void ui_init(void) { }
void ui_waiting(void) { }
void ui_initializing(void) { }
void ui_flash_ready(void) { }
void ui_erasing(uint32_t done, uint32_t total) { (void)done; (void)total; }
void ui_reading(uint32_t done, uint32_t total) { (void)done; (void)total; }
uint8_t ui_validate_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    (void)address; (void)data; (void)length; return 1U;
}
void ui_write_complete(uint32_t address, const uint8_t *data, uint32_t length)
{
    (void)address; (void)data; (void)length;
}
void ui_done(void) { }
void ui_error(const char *code) { (void)code; }
uint8_t ui_percent(void) { return 0U; }
uint32_t ui_wire_size(void) { return 0U; }

#else

#define BLACK 0x0000U
#define WHITE 0xFFFFU
#define GREEN 0x07E0U
#define DARK_GREEN 0x0200U
#define RED 0xF800U
#define AMBER 0xFD20U
#define MAX_FLASH_SIZE (16U * 1024U * 1024U)
#define TRACKED_BLOCK_SIZE 4096U
#define TRACKED_BLOCKS (MAX_FLASH_SIZE / TRACKED_BLOCK_SIZE)

static uint32_t wire_size;
static uint32_t contiguous;
static uint8_t percent;
static uint8_t main_complete;
static uint16_t written_length[TRACKED_BLOCKS];

typedef enum {
    UI_STATE_NONE,
    UI_STATE_WAITING,
    UI_STATE_INITIALIZING,
    UI_STATE_READY,
    UI_STATE_ERASING,
    UI_STATE_READING,
    UI_STATE_FLASHING,
    UI_STATE_FINALIZING,
    UI_STATE_DONE,
    UI_STATE_ERROR,
} ui_state_t;

typedef enum {
    PROGRESS_NONE,
    PROGRESS_ERASE,
    PROGRESS_FLASH,
} progress_mode_t;

static ui_state_t current_state;
static progress_mode_t progress_mode;
static uint16_t bar_width;
static uint8_t displayed_percent;

static void reset_progress_area(void)
{
    lcd_fill_rect(0U, 130U, 320U, 110U, BLACK);
    progress_mode = PROGRESS_NONE;
    bar_width = 0U;
    displayed_percent = 0xFFU;
}

static void set_status(ui_state_t state, const char *status, uint16_t color)
{
    if(current_state == state)
        return;
    lcd_fill_rect(0U, 74U, 320U, 42U, BLACK);
    lcd_draw_text_centered(82U, status, 2U, color, BLACK);
    current_state = state;

    if(state != UI_STATE_FINALIZING && state != UI_STATE_DONE)
        reset_progress_area();
}

static void prepare_bar(progress_mode_t mode)
{
    if(progress_mode == mode)
        return;
    lcd_fill_rect(20U, 138U, 280U, 34U, DARK_GREEN);
    lcd_fill_rect(24U, 142U, 272U, 26U, DARK_GREEN);
    progress_mode = mode;
    bar_width = 0U;
    displayed_percent = 0xFFU;
}

static void update_bar(uint16_t width, uint16_t color)
{
    if(width > 272U) width = 272U;
    if(width < bar_width)
    {
        lcd_fill_rect(24U, 142U, 272U, 26U, DARK_GREEN);
        bar_width = 0U;
    }
    if(width > bar_width)
        lcd_fill_rect((uint16_t)(24U + bar_width), 142U,
                      (uint16_t)(width - bar_width), 26U, color);
    bar_width = width;
}

static void draw_progress(uint8_t value, uint16_t color)
{
    char label[5];
    uint8_t index = 0U;
    if(value > 100U) value = 100U;
    prepare_bar(PROGRESS_FLASH);
    if(displayed_percent == value)
        return;
    if(value == 100U)
    {
        label[index++] = '1'; label[index++] = '0'; label[index++] = '0';
    }
    else
    {
        if(value >= 10U) label[index++] = (char)('0' + value / 10U);
        label[index++] = (char)('0' + value % 10U);
    }
    label[index++] = '%';
    label[index] = '\0';
    update_bar((uint16_t)(272U * value / 100U), color);
    lcd_fill_rect(0U, 184U, 320U, 50U, BLACK);
    lcd_draw_text_centered(190U, label, 3U, WHITE, BLACK);
    displayed_percent = value;
}

static void draw_erase_activity(uint32_t done, uint32_t total)
{
    uint16_t width = total ? (uint16_t)((uint64_t)272U * done / total) : 0U;
    prepare_bar(PROGRESS_ERASE);
    update_bar(width, AMBER);
}

void ui_init(void)
{
    wire_size = 0U;
    contiguous = 0U;
    percent = 0U;
    main_complete = 0U;
    current_state = UI_STATE_NONE;
    progress_mode = PROGRESS_NONE;
    displayed_percent = 0xFFU;
    lcd_init();
#ifndef ISP_LCD_INIT_ONLY
    lcd_clear(BLACK);
    lcd_draw_text_centered(20U, "HUSKYLENS UPDATE", 2U, WHITE, BLACK);
    ui_waiting();
#endif
}

void ui_waiting(void) { set_status(UI_STATE_WAITING, "WAITING FOR HOST", GREEN); }
void ui_initializing(void) { set_status(UI_STATE_INITIALIZING, "INITIALIZING FLASH", AMBER); }
void ui_flash_ready(void) { set_status(UI_STATE_READY, "READY", GREEN); }

void ui_erasing(uint32_t done, uint32_t total)
{
    set_status(UI_STATE_ERASING, "ERASING", AMBER);
    draw_erase_activity(done, total);
}

void ui_reading(uint32_t done, uint32_t total)
{
    set_status(UI_STATE_READING, "READING FLASH", GREEN);
    percent = total ? (uint8_t)((uint64_t)done * 100U / total) : 0U;
    draw_progress(percent, GREEN);
}

uint8_t ui_validate_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t raw;
    if(address != 0U || wire_size != 0U)
        return 1U;
    if(length < 5U)
        return 0U;
    raw = (uint32_t)data[1] | ((uint32_t)data[2] << 8) |
          ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
    return raw > 0U && raw <= MAX_FLASH_SIZE - 37U;
}

void ui_write_complete(uint32_t address, const uint8_t *data, uint32_t length)
{
    if(address == 0U && length >= 5U && wire_size == 0U)
    {
        uint32_t raw = (uint32_t)data[1] | ((uint32_t)data[2] << 8) |
                       ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
        if(raw > 0U && raw <= MAX_FLASH_SIZE - 37U)
            wire_size = raw + 37U;
    }

    uint32_t block = address / TRACKED_BLOCK_SIZE;
    if(block < TRACKED_BLOCKS && length > written_length[block])
        written_length[block] = (uint16_t)length;
    while((contiguous & (TRACKED_BLOCK_SIZE - 1U)) == 0U)
    {
        block = contiguous / TRACKED_BLOCK_SIZE;
        if(block >= TRACKED_BLOCKS || written_length[block] == 0U)
            break;
        length = written_length[block];
        contiguous += length;
        if(length < TRACKED_BLOCK_SIZE)
            break;
    }

    if(wire_size != 0U && contiguous >= wire_size)
    {
        percent = 100U;
        main_complete = 1U;
    }
    else if(wire_size != 0U)
        percent = (uint8_t)((uint64_t)contiguous * 100U / wire_size);

    if(main_complete && address >= wire_size)
        set_status(UI_STATE_FINALIZING, "FINALIZING", GREEN);
    else
        set_status(UI_STATE_FLASHING, "FLASHING", GREEN);
    draw_progress(percent, GREEN);
}

void ui_done(void)
{
    set_status(UI_STATE_DONE, "DONE", GREEN);
    draw_progress(100U, GREEN);
}

void ui_error(const char *code)
{
    set_status(UI_STATE_ERROR, "ERROR", RED);
    lcd_draw_text_centered(150U, code, 3U, WHITE, BLACK);
}

uint8_t ui_percent(void) { return percent; }
uint32_t ui_wire_size(void) { return wire_size; }

#endif
