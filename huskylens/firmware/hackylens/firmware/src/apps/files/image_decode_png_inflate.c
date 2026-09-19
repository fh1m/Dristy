#include "image_decode_png_inflate.h"

#include <string.h>

#include "inflate_types.h"

#include "files_image_config.h"


static uint16_t g_huff_lit_table[HUFF_TABLE_SIZE] __attribute__((aligned(4), section(".bss")));
static uint16_t g_huff_dist_table[HUFF_TABLE_SIZE] __attribute__((aligned(4), section(".bss")));

static uint16_t huff_reverse(uint16_t code, uint8_t len)
{
    uint16_t out = 0;
    while(len--)
    {
        out = (uint16_t)((out << 1) | (code & 1U));
        code >>= 1;
    }
    return out;
}

static uint8_t huff_build_table(const uint8_t *lengths, uint16_t count, uint16_t *table)
{
    uint16_t bl_count[HUFF_TABLE_BITS + 1U];
    uint16_t next_code[HUFF_TABLE_BITS + 1U];
    uint16_t code = 0;

    for(uint32_t i = 0; i < HUFF_TABLE_SIZE; i++)
        table[i] = HUFF_INVALID;
    memset(bl_count, 0, sizeof(bl_count));
    memset(next_code, 0, sizeof(next_code));

    for(uint16_t i = 0; i < count; i++)
    {
        if(lengths[i] > HUFF_TABLE_BITS)
            return 0;
        if(lengths[i])
            bl_count[lengths[i]]++;
    }

    for(uint8_t bits = 1; bits <= HUFF_TABLE_BITS; bits++)
    {
        code = (uint16_t)((code + bl_count[bits - 1U]) << 1);
        next_code[bits] = code;
    }

    for(uint16_t sym = 0; sym < count; sym++)
    {
        uint8_t len = lengths[sym];
        uint16_t value;
        uint16_t rev;
        uint32_t step;
        if(!len)
            continue;
        rev = huff_reverse(next_code[len]++, len);
        value = (uint16_t)(((uint16_t)len << 9) | sym);
        step = 1UL << len;
        for(uint32_t idx = rev; idx < HUFF_TABLE_SIZE; idx += step)
            table[idx] = value;
    }

    return 1;
}

static uint8_t inflate_need(inflate_stream_t *s, uint8_t bits)
{
    while(s->bitcount < bits)
    {
        if(s->pos >= s->size)
        {
            s->bitcount = bits;
            return 1;
        }
        s->bitbuf |= (uint32_t)s->data[s->pos++] << s->bitcount;
        s->bitcount = (uint8_t)(s->bitcount + 8U);
    }
    return 1;
}

static uint32_t inflate_get_bits(inflate_stream_t *s, uint8_t bits, uint8_t *ok)
{
    uint32_t value;
    if(!inflate_need(s, bits))
    {
        *ok = 0;
        return 0;
    }
    value = s->bitbuf & ((1UL << bits) - 1UL);
    s->bitbuf >>= bits;
    s->bitcount = (uint8_t)(s->bitcount - bits);
    return value;
}

static int16_t inflate_huff_decode(inflate_stream_t *s, const uint16_t *table)
{
    uint16_t entry;
    uint8_t len;

    if(!inflate_need(s, HUFF_TABLE_BITS))
        return -1;
    entry = table[s->bitbuf & (HUFF_TABLE_SIZE - 1U)];
    if(entry == HUFF_INVALID)
        return -1;
    len = (uint8_t)(entry >> 9);
    s->bitbuf >>= len;
    s->bitcount = (uint8_t)(s->bitcount - len);
    return (int16_t)(entry & 0x01FFU);
}

static uint8_t inflate_build_fixed_tables(void)
{
    uint8_t lit_lengths[288];
    uint8_t dist_lengths[32];

    for(uint16_t i = 0; i <= 143; i++)
        lit_lengths[i] = 8;
    for(uint16_t i = 144; i <= 255; i++)
        lit_lengths[i] = 9;
    for(uint16_t i = 256; i <= 279; i++)
        lit_lengths[i] = 7;
    for(uint16_t i = 280; i <= 287; i++)
        lit_lengths[i] = 8;
    for(uint8_t i = 0; i < 32; i++)
        dist_lengths[i] = 5;

    return huff_build_table(lit_lengths, 288, g_huff_lit_table) &&
           huff_build_table(dist_lengths, 32, g_huff_dist_table);
}

static uint8_t inflate_build_dynamic_tables(inflate_stream_t *s)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t ok = 1;
    uint16_t hlit = (uint16_t)inflate_get_bits(s, 5, &ok) + 257U;
    uint16_t hdist = (uint16_t)inflate_get_bits(s, 5, &ok) + 1U;
    uint16_t hclen = (uint16_t)inflate_get_bits(s, 4, &ok) + 4U;
    uint8_t cl_lengths[19];
    uint8_t lengths[288 + 32];
    uint16_t total;
    uint16_t i = 0;

    if(!ok || hlit > 288 || hdist > 32)
        return 0;

    memset(cl_lengths, 0, sizeof(cl_lengths));
    memset(lengths, 0, sizeof(lengths));
    for(uint16_t j = 0; j < hclen; j++)
        cl_lengths[order[j]] = (uint8_t)inflate_get_bits(s, 3, &ok);
    if(!ok || !huff_build_table(cl_lengths, 19, g_huff_dist_table))
        return 0;

    total = (uint16_t)(hlit + hdist);
    while(i < total)
    {
        int16_t sym = inflate_huff_decode(s, g_huff_dist_table);
        if(sym < 0)
            return 0;
        if(sym <= 15)
        {
            lengths[i++] = (uint8_t)sym;
        }
        else if(sym == 16)
        {
            uint8_t repeat;
            uint8_t prev;
            if(i == 0)
                return 0;
            repeat = (uint8_t)inflate_get_bits(s, 2, &ok) + 3U;
            prev = lengths[i - 1U];
            if(!ok || i + repeat > total)
                return 0;
            while(repeat--)
                lengths[i++] = prev;
        }
        else if(sym == 17)
        {
            uint8_t repeat = (uint8_t)inflate_get_bits(s, 3, &ok) + 3U;
            if(!ok || i + repeat > total)
                return 0;
            while(repeat--)
                lengths[i++] = 0;
        }
        else if(sym == 18)
        {
            uint8_t repeat = (uint8_t)inflate_get_bits(s, 7, &ok) + 11U;
            if(!ok || i + repeat > total)
                return 0;
            while(repeat--)
                lengths[i++] = 0;
        }
        else
        {
            return 0;
        }
    }

    return huff_build_table(lengths, hlit, g_huff_lit_table) &&
           huff_build_table(&lengths[hlit], hdist, g_huff_dist_table);
}

static uint8_t inflate_codes(inflate_stream_t *s, uint8_t *out, uint32_t out_size, uint32_t *out_pos)
{
    static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    uint8_t ok = 1;

    while(1)
    {
        int16_t sym = inflate_huff_decode(s, g_huff_lit_table);
        if(sym < 0)
            return 0;
        if(sym < 256)
        {
            if(*out_pos >= out_size)
                return 0;
            out[(*out_pos)++] = (uint8_t)sym;
            continue;
        }
        if(sym == 256)
            return 1;
        if(sym < 257 || sym > 285)
            return 0;

        uint16_t len_index = (uint16_t)(sym - 257);
        uint32_t length = len_base[len_index] + inflate_get_bits(s, len_extra[len_index], &ok);
        int16_t dist_sym = inflate_huff_decode(s, g_huff_dist_table);
        if(!ok || dist_sym < 0 || dist_sym >= 30)
            return 0;
        uint32_t distance = dist_base[dist_sym] + inflate_get_bits(s, dist_extra[dist_sym], &ok);
        if(!ok || distance == 0 || distance > *out_pos || *out_pos + length > out_size)
            return 0;

        while(length--)
        {
            out[*out_pos] = out[*out_pos - distance];
            (*out_pos)++;
        }
    }
}

uint8_t png_zlib_inflate_to_buffer(const uint8_t *src, uint32_t src_size, uint8_t *out, uint32_t out_size)
{
    inflate_stream_t s;
    uint32_t out_pos = 0;
    uint8_t final_block = 0;
    uint8_t ok = 1;

    if(src_size < 6 || (src[0] & 0x0F) != 8 || (((uint16_t)src[0] << 8) + src[1]) % 31U != 0)
        return 0;

    s.data = src + 2;
    s.size = src_size - 6U;
    s.pos = 0;
    s.bitbuf = 0;
    s.bitcount = 0;

    while(!final_block)
    {
        uint8_t block_type;
        final_block = (uint8_t)inflate_get_bits(&s, 1, &ok);
        block_type = (uint8_t)inflate_get_bits(&s, 2, &ok);
        if(!ok)
            return 0;

        if(block_type == 0)
        {
            uint16_t len;
            uint16_t nlen;
            s.bitbuf = 0;
            s.bitcount = 0;
            if(s.pos + 4U > s.size)
                return 0;
            len = (uint16_t)s.data[s.pos] | ((uint16_t)s.data[s.pos + 1U] << 8);
            nlen = (uint16_t)s.data[s.pos + 2U] | ((uint16_t)s.data[s.pos + 3U] << 8);
            s.pos += 4U;
            if((uint16_t)~len != nlen || s.pos + len > s.size || out_pos + len > out_size)
                return 0;
            memcpy(&out[out_pos], &s.data[s.pos], len);
            s.pos += len;
            out_pos += len;
        }
        else if(block_type == 1)
        {
            if(!inflate_build_fixed_tables() || !inflate_codes(&s, out, out_size, &out_pos))
                return 0;
        }
        else if(block_type == 2)
        {
            if(!inflate_build_dynamic_tables(&s) || !inflate_codes(&s, out, out_size, &out_pos))
                return 0;
        }
        else
        {
            return 0;
        }
    }

    return out_pos == out_size;
}
