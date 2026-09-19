#include "image_decode_gif.h"

#include <string.h>

#include "files_image_config.h"
#include "../../storage/fat32_stream.h"

#define GIF_SIGNATURE_SIZE 6U
#define GIF_HEADER_SIZE 13U
#define GIF_PALETTE_MAX 256U
#define GIF_LZW_TABLE_SIZE 4096U
#define GIF_MIN_DELAY_MS 20U

typedef struct
{
    uint8_t data[255];
    uint8_t size;
    uint8_t position;
    uint8_t finished;
    file_result_t failure;
} gif_subblocks_t;

typedef struct
{
    gif_subblocks_t blocks;
    uint32_t bits;
    uint8_t bit_count;
} gif_bits_t;

typedef struct
{
    fat_file_entry_t entry;
    fat_stream_t stream;
    file_image_sink_t sink;
    uint16_t global_palette[GIF_PALETTE_MAX];
    uint16_t local_palette[GIF_PALETTE_MAX];
    uint16_t canvas_w;
    uint16_t canvas_h;
    uint16_t global_palette_size;
    uint16_t background_rgb565;
    uint32_t delay_ms;
    uint32_t current_delay_ms;
    uint64_t deadline_us;
    uint64_t pause_remaining_us;
    uint16_t frames_in_cycle;
    int16_t transparent_index;
    uint8_t disposal;
    uint8_t active;
    uint8_t playing;
    uint8_t paused;
    uint8_t deadline_armed;
    uint8_t known_animated;
    uint8_t netscape_extension;
} gif_session_t;

static gif_session_t g_gif;
static uint16_t g_lzw_prefix[GIF_LZW_TABLE_SIZE];
static uint8_t g_lzw_suffix[GIF_LZW_TABLE_SIZE];
static uint8_t g_lzw_stack[GIF_LZW_TABLE_SIZE];

static uint16_t gif_rd16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint16_t gif_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) |
                      ((uint16_t)b >> 3));
}

static file_result_t gif_read(void *dst, uint32_t len)
{
    if(g_gif.stream.file_offset > g_gif.entry.size ||
       len > g_gif.entry.size - g_gif.stream.file_offset)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    if(!fat_stream_read(&g_gif.stream, (uint8_t *)dst, len))
        return FILE_RESULT_READ_ERROR;
    return FILE_RESULT_OK;
}

static file_result_t gif_read_palette(uint16_t *palette, uint16_t count)
{
    uint8_t rgb[3];

    if(count == 0 || count > GIF_PALETTE_MAX)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    for(uint16_t i = 0; i < count; i++)
    {
        file_result_t result = gif_read(rgb, sizeof(rgb));
        if(result != FILE_RESULT_OK)
            return result;
        palette[i] = gif_rgb565(rgb[0], rgb[1], rgb[2]);
    }
    return FILE_RESULT_OK;
}

static int16_t gif_subblock_byte(gif_subblocks_t *blocks)
{
    file_result_t result;

    if(blocks->finished || blocks->failure != FILE_RESULT_OK)
        return -1;
    if(blocks->position >= blocks->size)
    {
        result = gif_read(&blocks->size, 1);
        if(result != FILE_RESULT_OK)
        {
            blocks->failure = result;
            return -1;
        }
        if(blocks->size == 0U)
        {
            blocks->finished = 1;
            return -1;
        }
        result = gif_read(blocks->data, blocks->size);
        if(result != FILE_RESULT_OK)
        {
            blocks->failure = result;
            return -1;
        }
        blocks->position = 0U;
    }
    return blocks->data[blocks->position++];
}

static file_result_t gif_skip_subblocks(void)
{
    gif_subblocks_t blocks;

    memset(&blocks, 0, sizeof(blocks));
    while(gif_subblock_byte(&blocks) >= 0)
        ;
    return blocks.failure;
}

static int32_t gif_read_code(gif_bits_t *bits, uint8_t width)
{
    while(bits->bit_count < width)
    {
        int16_t value = gif_subblock_byte(&bits->blocks);
        if(value < 0)
            return -1;
        bits->bits |= (uint32_t)(uint8_t)value << bits->bit_count;
        bits->bit_count = (uint8_t)(bits->bit_count + 8U);
    }
    {
        uint16_t code = (uint16_t)(bits->bits & ((1UL << width) - 1UL));
        bits->bits >>= width;
        bits->bit_count = (uint8_t)(bits->bit_count - width);
        return code;
    }
}

static uint16_t gif_interlace_row(uint16_t sequence, uint16_t height)
{
    static const uint8_t starts[4] = {0, 4, 2, 1};
    static const uint8_t steps[4] = {8, 8, 4, 2};

    for(uint8_t pass = 0; pass < 4; pass++)
    {
        for(uint16_t row = starts[pass]; row < height; row = (uint16_t)(row + steps[pass]))
        {
            if(sequence == 0)
                return row;
            sequence--;
        }
    }
    return height;
}

static file_result_t gif_emit_index(gif_session_t *gif, const file_gif_frame_t *frame,
                                    const uint16_t *palette, uint16_t palette_size,
                                    uint8_t interlaced, uint8_t index,
                                    uint32_t *pixel_count, uint16_t *row_used)
{
    uint8_t *row = image_decode_row_buffer();
    uint16_t row_index;

    if(index >= palette_size)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    if(*pixel_count >= (uint32_t)frame->frame_w * frame->frame_h)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    row[(*row_used)++] = index;
    (*pixel_count)++;
    if(*row_used != frame->frame_w)
        return FILE_RESULT_OK;

    row_index = (uint16_t)((*pixel_count / frame->frame_w) - 1U);
    if(interlaced)
        row_index = gif_interlace_row(row_index, frame->frame_h);
    if(row_index >= frame->frame_h)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    gif->sink.animation_render_indexed_row(gif->sink.context, frame, row_index,
                                            row, palette, palette_size);
    *row_used = 0;
    return FILE_RESULT_OK;
}

static file_result_t gif_decode_lzw(gif_session_t *gif, const file_gif_frame_t *frame,
                                    const uint16_t *palette, uint16_t palette_size,
                                    uint8_t interlaced)
{
    gif_bits_t bits;
    uint8_t minimum_code_size;
    uint16_t clear_code;
    uint16_t end_code;
    uint16_t next_code;
    uint8_t code_width;
    int32_t previous = -1;
    uint8_t first = 0;
    uint32_t pixel_count = 0;
    uint16_t row_used = 0;
    uint8_t saw_end = 0;

    memset(&bits, 0, sizeof(bits));
    {
        file_result_t result = gif_read(&minimum_code_size, 1);
        if(result != FILE_RESULT_OK)
            return result;
    }
    if(minimum_code_size < 2 || minimum_code_size > 8 ||
       frame->frame_w > image_decode_row_buffer_size())
        return FILE_RESULT_UNSUPPORTED_FORMAT;

    clear_code = (uint16_t)(1U << minimum_code_size);
    end_code = (uint16_t)(clear_code + 1U);
    next_code = (uint16_t)(end_code + 1U);
    code_width = (uint8_t)(minimum_code_size + 1U);
    for(uint16_t i = 0; i < clear_code; i++)
        g_lzw_suffix[i] = (uint8_t)i;

    for(;;)
    {
        int32_t raw_code = gif_read_code(&bits, code_width);
        uint16_t code;
        uint16_t in_code;
        uint16_t stack_size = 0;
        file_result_t result;

        if(raw_code < 0)
            break;
        code = (uint16_t)raw_code;
        if(code == clear_code)
        {
            next_code = (uint16_t)(end_code + 1U);
            code_width = (uint8_t)(minimum_code_size + 1U);
            previous = -1;
            continue;
        }
        if(code == end_code)
        {
            saw_end = 1;
            break;
        }
        if(code > next_code || (code == next_code && previous < 0))
            return FILE_RESULT_UNSUPPORTED_FORMAT;

        in_code = code;
        if(code == next_code)
        {
            if(stack_size >= GIF_LZW_TABLE_SIZE)
                return FILE_RESULT_UNSUPPORTED_FORMAT;
            g_lzw_stack[stack_size++] = first;
            code = (uint16_t)previous;
        }
        while(code >= clear_code)
        {
            if(code >= next_code || stack_size >= GIF_LZW_TABLE_SIZE)
                return FILE_RESULT_UNSUPPORTED_FORMAT;
            g_lzw_stack[stack_size++] = g_lzw_suffix[code];
            code = g_lzw_prefix[code];
        }
        first = (uint8_t)code;
        if(stack_size >= GIF_LZW_TABLE_SIZE)
            return FILE_RESULT_UNSUPPORTED_FORMAT;
        g_lzw_stack[stack_size++] = first;

        while(stack_size)
        {
            result = gif_emit_index(gif, frame, palette, palette_size, interlaced,
                                    g_lzw_stack[--stack_size], &pixel_count, &row_used);
            if(result != FILE_RESULT_OK)
                return result;
        }

        if(previous >= 0 && next_code < GIF_LZW_TABLE_SIZE)
        {
            g_lzw_prefix[next_code] = (uint16_t)previous;
            g_lzw_suffix[next_code] = first;
            next_code++;
            if(next_code == (1U << code_width) && code_width < 12U)
                code_width++;
        }
        previous = in_code;
    }

    while(gif_subblock_byte(&bits.blocks) >= 0)
        ;
    if(bits.blocks.failure != FILE_RESULT_OK)
        return bits.blocks.failure;
    if(!saw_end || pixel_count != (uint32_t)frame->frame_w * frame->frame_h || row_used != 0)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    return FILE_RESULT_OK;
}

static void gif_reset_graphics_control(void)
{
    g_gif.delay_ms = GIF_MIN_DELAY_MS;
    g_gif.transparent_index = -1;
    g_gif.disposal = 0;
}

static file_result_t gif_read_graphics_control(void)
{
    uint8_t data[6];
    uint16_t delay;
    file_result_t result;

    result = gif_read(data, sizeof(data));
    if(result != FILE_RESULT_OK)
        return result;
    if(data[0] != 4 || data[5] != 0)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    g_gif.disposal = (uint8_t)((data[1] >> 2) & 0x07U);
    if(g_gif.disposal > 3)
        g_gif.disposal = 0;
    delay = gif_rd16(&data[2]);
    g_gif.delay_ms = delay < 2U ? GIF_MIN_DELAY_MS : (uint32_t)delay * 10U;
    g_gif.transparent_index = (data[1] & 0x01U) ? data[4] : -1;
    return FILE_RESULT_OK;
}

static file_result_t gif_read_application_extension(void)
{
    uint8_t size;
    uint8_t app[255];
    file_result_t result;

    result = gif_read(&size, 1);
    if(result != FILE_RESULT_OK)
        return result;
    if(size == 0)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    result = gif_read(app, size);
    if(result != FILE_RESULT_OK)
        return result;
    if(size == 11 &&
       (memcmp(app, "NETSCAPE2.0", 11) == 0 || memcmp(app, "ANIMEXTS1.0", 11) == 0))
        g_gif.netscape_extension = 1;
    result = gif_skip_subblocks();
    return result;
}

static file_result_t gif_skip_extension_header(void)
{
    uint8_t size;
    uint8_t scratch[255];
    file_result_t result;

    result = gif_read(&size, 1);
    if(result != FILE_RESULT_OK)
        return result;
    if(size)
    {
        result = gif_read(scratch, size);
        if(result != FILE_RESULT_OK)
            return result;
    }
    return gif_skip_subblocks();
}

static file_result_t gif_decode_frame(void)
{
    uint8_t descriptor[9];
    uint8_t packed;
    uint8_t interlaced;
    uint16_t palette_size;
    uint16_t *palette;
    file_gif_frame_t frame;
    file_result_t result;

    result = gif_read(descriptor, sizeof(descriptor));
    if(result != FILE_RESULT_OK)
        return result;
    memset(&frame, 0, sizeof(frame));
    frame.canvas_w = g_gif.canvas_w;
    frame.canvas_h = g_gif.canvas_h;
    frame.frame_x = gif_rd16(&descriptor[0]);
    frame.frame_y = gif_rd16(&descriptor[2]);
    frame.frame_w = gif_rd16(&descriptor[4]);
    frame.frame_h = gif_rd16(&descriptor[6]);
    frame.background_rgb565 = g_gif.background_rgb565;
    frame.delay_ms = g_gif.delay_ms;
    frame.transparent_index = g_gif.transparent_index;
    frame.disposal = g_gif.disposal;
    packed = descriptor[8];
    interlaced = (packed & 0x40U) ? 1U : 0U;

    if(frame.frame_w == 0 || frame.frame_h == 0 ||
       (uint32_t)frame.frame_x + frame.frame_w > frame.canvas_w ||
       (uint32_t)frame.frame_y + frame.frame_h > frame.canvas_h)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    if(frame.frame_w > PHOTO_VIEW_MAX_W || frame.frame_h > PHOTO_VIEW_MAX_H)
        return FILE_RESULT_TOO_LARGE;

    if(packed & 0x80U)
    {
        palette_size = (uint16_t)(2U << (packed & 0x07U));
        result = gif_read_palette(g_gif.local_palette, palette_size);
        if(result != FILE_RESULT_OK)
            return result;
        palette = g_gif.local_palette;
    }
    else
    {
        if(g_gif.global_palette_size == 0)
            return FILE_RESULT_UNSUPPORTED_FORMAT;
        palette_size = g_gif.global_palette_size;
        palette = g_gif.global_palette;
    }
    if(frame.transparent_index >= (int16_t)palette_size)
        frame.transparent_index = -1;

    if(!g_gif.sink.animation_frame_begin(g_gif.sink.context, &frame))
        return FILE_RESULT_READ_ERROR;
    result = gif_decode_lzw(&g_gif, &frame, palette, palette_size, interlaced);
    if(result != FILE_RESULT_OK)
        return result;
    if(!g_gif.sink.animation_frame_end(g_gif.sink.context))
        return FILE_RESULT_READ_ERROR;

    g_gif.frames_in_cycle++;
    if(g_gif.frames_in_cycle >= 2)
        g_gif.known_animated = 1;
    g_gif.current_delay_ms = frame.delay_ms;
    gif_reset_graphics_control();
    return FILE_RESULT_OK;
}

static file_result_t gif_parse_header(uint8_t reset_canvas)
{
    uint8_t header[GIF_HEADER_SIZE];
    uint8_t packed;
    uint8_t background_index;
    file_result_t result;

    if(!fat_stream_open(&g_gif.stream, &g_gif.entry, 0))
        return FILE_RESULT_READ_ERROR;
    result = gif_read(header, sizeof(header));
    if(result != FILE_RESULT_OK)
        return result;
    if((memcmp(header, "GIF87a", GIF_SIGNATURE_SIZE) != 0 &&
        memcmp(header, "GIF89a", GIF_SIGNATURE_SIZE) != 0))
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    g_gif.canvas_w = gif_rd16(&header[6]);
    g_gif.canvas_h = gif_rd16(&header[8]);
    if(g_gif.canvas_w == 0 || g_gif.canvas_h == 0 ||
       g_gif.canvas_w > PHOTO_VIEW_MAX_W || g_gif.canvas_h > PHOTO_VIEW_MAX_H)
        return FILE_RESULT_TOO_LARGE;

    packed = header[10];
    background_index = header[11];
    g_gif.global_palette_size = 0;
    g_gif.background_rgb565 = 0;
    if(packed & 0x80U)
    {
        g_gif.global_palette_size = (uint16_t)(2U << (packed & 0x07U));
        result = gif_read_palette(g_gif.global_palette, g_gif.global_palette_size);
        if(result != FILE_RESULT_OK)
            return result;
        if(background_index < g_gif.global_palette_size)
            g_gif.background_rgb565 = g_gif.global_palette[background_index];
    }
    gif_reset_graphics_control();
    g_gif.frames_in_cycle = 0;
    if(reset_canvas && !g_gif.sink.animation_begin(g_gif.sink.context, g_gif.canvas_w,
                                                    g_gif.canvas_h, g_gif.background_rgb565))
        return FILE_RESULT_READ_ERROR;
    return FILE_RESULT_OK;
}

static file_result_t gif_decode_next(uint8_t allow_restart)
{
    for(;;)
    {
        uint8_t marker;
        file_result_t result = gif_read(&marker, 1);
        if(result != FILE_RESULT_OK)
            return result;
        if(marker == 0x2CU)
            return gif_decode_frame();
        if(marker == 0x3BU)
        {
            if(g_gif.frames_in_cycle == 0)
                return FILE_RESULT_UNSUPPORTED_FORMAT;
            if(!g_gif.known_animated && g_gif.frames_in_cycle == 1)
            {
                g_gif.playing = 0;
                return FILE_RESULT_OK;
            }
            if(!allow_restart)
                return FILE_RESULT_OK;
            result = gif_parse_header(1);
            if(result != FILE_RESULT_OK)
                return result;
            continue;
        }
        if(marker == 0x21U)
        {
            uint8_t label;
            result = gif_read(&label, 1);
            if(result != FILE_RESULT_OK)
                return result;
            if(label == 0xF9U)
                result = gif_read_graphics_control();
            else if(label == 0xFFU)
                result = gif_read_application_extension();
            else if(label == 0x01U)
                result = gif_skip_extension_header();
            else
                result = gif_skip_subblocks();
            if(result != FILE_RESULT_OK)
                return result;
            continue;
        }
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    }
}

void files_gif_close(void)
{
    if(g_gif.active && g_gif.sink.animation_end)
        g_gif.sink.animation_end(g_gif.sink.context);
    memset(&g_gif, 0, sizeof(g_gif));
}

file_result_t files_open_gif(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    file_result_t result;

    files_gif_close();
    if(!entry || !sink || !sink->animation_begin || !sink->animation_frame_begin ||
       !sink->animation_render_indexed_row || !sink->animation_frame_end)
        return FILE_RESULT_NOT_FOUND;
    memset(&g_gif, 0, sizeof(g_gif));
    g_gif.entry = *entry;
    g_gif.sink = *sink;
    g_gif.active = 1;
    g_gif.playing = 1;
    result = gif_parse_header(1);
    if(result == FILE_RESULT_OK)
        result = gif_decode_next(0);
    if(result != FILE_RESULT_OK)
        files_gif_close();
    return result;
}

file_result_t files_gif_tick(uint64_t now_us)
{
    file_result_t result;

    if(!g_gif.active || !g_gif.playing || g_gif.paused)
        return FILE_RESULT_OK;
    if(!g_gif.deadline_armed)
    {
        g_gif.deadline_us = now_us + (uint64_t)g_gif.current_delay_ms * 1000ULL;
        g_gif.deadline_armed = 1;
        return FILE_RESULT_OK;
    }
    if(now_us < g_gif.deadline_us)
        return FILE_RESULT_OK;

    /* The FAT stream and periodic SD health checks share the FAT sector
       scratch buffer. A GIF yields between frames, so another tick may have
       replaced the cached sector while the stream still marks it valid. */
    g_gif.stream.cache_valid = 0;
    result = gif_decode_next(1);
    if(result != FILE_RESULT_OK)
    {
        files_gif_close();
        return result;
    }
    g_gif.deadline_armed = 0;
    return FILE_RESULT_OK;
}

uint8_t files_gif_active(void)
{
    return g_gif.active;
}

uint8_t files_gif_is_animation(void)
{
    return g_gif.active && (g_gif.playing || g_gif.known_animated);
}

uint8_t files_gif_toggle_pause(uint64_t now_us)
{
    if(!files_gif_is_animation() || !g_gif.playing)
        return 0;
    if(g_gif.paused)
    {
        g_gif.deadline_us = now_us + g_gif.pause_remaining_us;
        g_gif.deadline_armed = 1;
        g_gif.paused = 0;
    }
    else
    {
        g_gif.pause_remaining_us =
            g_gif.deadline_armed && g_gif.deadline_us > now_us ? g_gif.deadline_us - now_us : 0;
        g_gif.paused = 1;
    }
    return 1;
}
