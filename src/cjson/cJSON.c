/*
  cJSON 1.7.18 - Minimal complete implementation
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
  MIT License
*/

#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>
#include <stdarg.h>

/* Nesting limit for arrays/objects */
#define CJSON_NESTING_LIMIT 1000

/* ------------------------------------------------------------------ */
/* Internal hooks / memory                                             */
/* ------------------------------------------------------------------ */

typedef struct internal_hooks {
    void *(*allocate)(size_t size);
    void  (*deallocate)(void *pointer);
    void *(*reallocate)(void *pointer, size_t size);
} internal_hooks;

static internal_hooks global_hooks = { malloc, free, realloc };

/* ------------------------------------------------------------------ */
/* printbuffer – used by all print functions                           */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char *buffer;
    size_t length;
    size_t offset;
    size_t depth;
    int noalloc;
    int format;
    internal_hooks hooks;
} printbuffer;

/* Ensure p->buffer has at least 'needed' bytes available from p->offset.
 * Returns pointer to p->buffer + p->offset, or NULL on failure. */
static unsigned char *ensure(printbuffer * const p, size_t needed)
{
    unsigned char *newbuffer = NULL;
    size_t newsize = 0;

    if (!p || !p->buffer) return NULL;
    if ((p->length > 0) && (p->offset >= p->length)) return NULL;

    needed += p->offset;
    if (needed <= p->length) return p->buffer + p->offset;

    if (p->noalloc) return NULL;

    if (needed > (size_t)(INT_MAX / 2)) {
        if (needed <= (size_t)INT_MAX) newsize = (size_t)INT_MAX;
        else return NULL;
    } else {
        newsize = needed * 2;
    }

    if (p->hooks.reallocate) {
        newbuffer = (unsigned char *)p->hooks.reallocate(p->buffer, newsize);
        if (!newbuffer) { p->hooks.deallocate(p->buffer); p->length = 0; p->buffer = NULL; return NULL; }
    } else {
        newbuffer = (unsigned char *)p->hooks.allocate(newsize);
        if (!newbuffer) { p->hooks.deallocate(p->buffer); p->length = 0; p->buffer = NULL; return NULL; }
        memcpy(newbuffer, p->buffer, p->offset);
        p->hooks.deallocate(p->buffer);
    }
    p->length = newsize;
    p->buffer = newbuffer;
    return newbuffer + p->offset;
}

static void update_offset(printbuffer * const buffer)
{
    const unsigned char *buf;
    if (!buffer || !buffer->buffer) return;
    buf = buffer->buffer + buffer->offset;
    buffer->offset += strlen((const char *)buf);
}

/* ------------------------------------------------------------------ */
/* Error tracking                                                      */
/* ------------------------------------------------------------------ */

typedef struct { const unsigned char *json; size_t position; } error_t;
static error_t global_error = { NULL, 0 };

CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void)
{
    return (const char *)(global_error.json + global_error.position);
}

/* ------------------------------------------------------------------ */
/* Item allocation                                                     */
/* ------------------------------------------------------------------ */

static unsigned char *cJSON_strdup(const unsigned char *string, const internal_hooks *hooks)
{
    size_t length;
    unsigned char *copy;
    if (!string) return NULL;
    length = strlen((const char *)string) + 1;
    copy = (unsigned char *)hooks->allocate(length);
    if (!copy) return NULL;
    memcpy(copy, string, length);
    return copy;
}

static cJSON *cJSON_New_Item(const internal_hooks *hooks)
{
    cJSON *node = (cJSON *)hooks->allocate(sizeof(cJSON));
    if (node) memset(node, 0, sizeof(cJSON));
    return node;
}

/* ------------------------------------------------------------------ */
/* Public init / delete                                                */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks *hooks)
{
    if (!hooks) { global_hooks.allocate = malloc; global_hooks.deallocate = free; global_hooks.reallocate = realloc; return; }
    global_hooks.allocate   = hooks->malloc_fn ? hooks->malloc_fn : malloc;
    global_hooks.deallocate = hooks->free_fn   ? hooks->free_fn   : free;
    global_hooks.reallocate = realloc;
}

CJSON_PUBLIC(void) cJSON_Delete(cJSON *item)
{
    cJSON *next;
    while (item) {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && item->child)       cJSON_Delete(item->child);
        if (!(item->type & cJSON_IsReference) && item->valuestring) global_hooks.deallocate(item->valuestring);
        if (!(item->type & cJSON_StringIsConst) && item->string)    global_hooks.deallocate(item->string);
        global_hooks.deallocate(item);
        item = next;
    }
}

/* ------------------------------------------------------------------ */
/* parse_buffer helpers                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    const unsigned char *content;
    size_t length;
    size_t offset;
    size_t depth;
    internal_hooks hooks;
} parse_buffer;

#define can_read(b,n)            ((b) && ((b)->offset + (n) <= (b)->length))
#define can_access_at_index(b,i) ((b) && ((b)->offset + (i) < (b)->length))
#define cannot_access_at_index(b,i) (!can_access_at_index(b,i))
#define buffer_at_offset(b) ((b)->content + (b)->offset)

static parse_buffer *buffer_skip_whitespace(parse_buffer *buffer)
{
    if (!buffer || !buffer->content) return NULL;
    while (can_access_at_index(buffer, 0) && buffer_at_offset(buffer)[0] <= 32)
        buffer->offset++;
    if (buffer->offset == buffer->length) buffer->offset--;
    return buffer;
}

static parse_buffer *skip_utf8_bom(parse_buffer *buffer)
{
    if (!buffer || !buffer->content || buffer->offset != 0) return NULL;
    if (can_access_at_index(buffer, 4) && strncmp((const char *)buffer_at_offset(buffer), "\xEF\xBB\xBF", 3) == 0)
        buffer->offset += 3;
    return buffer;
}

static unsigned char get_decimal_point(void) { return '.'; }

/* ------------------------------------------------------------------ */
/* Number parsing / printing                                           */
/* ------------------------------------------------------------------ */

static cJSON_bool parse_number(cJSON *item, parse_buffer *input_buffer)
{
    double number = 0;
    unsigned char *after_end = NULL;
    unsigned char number_c_string[64];
    unsigned char decimal_point = get_decimal_point();
    size_t i = 0;

    if (!input_buffer || !input_buffer->content) return 0;

    for (i = 0; i < sizeof(number_c_string) - 1 && can_access_at_index(input_buffer, i); i++) {
        unsigned char c = buffer_at_offset(input_buffer)[i];
        if (c >= '0' && c <= '9') { number_c_string[i] = c; }
        else if (c == '+' || c == '-' || c == 'e' || c == 'E') { number_c_string[i] = c; }
        else if (c == '.') { number_c_string[i] = decimal_point; }
        else break;
    }
    number_c_string[i] = '\0';

    number = strtod((const char *)number_c_string, (char **)&after_end);
    if (number_c_string == after_end) return 0;

    item->valuedouble = number;
    item->valueint = (number >= INT_MAX) ? INT_MAX : ((number <= (double)INT_MIN) ? INT_MIN : (int)number);
    item->type = cJSON_Number;
    input_buffer->offset += (size_t)(after_end - number_c_string);
    return 1;
}

static cJSON_bool print_number(const cJSON *item, printbuffer *output_buffer)
{
    unsigned char *output_pointer;
    double d = item->valuedouble;
    int length = 0;
    size_t i;
    unsigned char number_buffer[64] = {0};
    unsigned char decimal_point = get_decimal_point();
    double test = 0.0;

    if (!output_buffer) return 0;

    if (isnan(d) || isinf(d)) {
        length = sprintf((char *)number_buffer, "null");
    } else if (d == (double)item->valueint) {
        length = sprintf((char *)number_buffer, "%d", item->valueint);
    } else {
        length = sprintf((char *)number_buffer, "%1.15g", d);
        if (sscanf((char *)number_buffer, "%lg", &test) != 1 || (double)test != d)
            length = sprintf((char *)number_buffer, "%1.17g", d);
    }

    if (length < 0 || length > 63) return 0;

    output_pointer = ensure(output_buffer, (size_t)length + 1);
    if (!output_pointer) return 0;

    for (i = 0; i < (size_t)length; i++) {
        output_pointer[i] = (number_buffer[i] == decimal_point) ? '.' : number_buffer[i];
    }
    output_pointer[i] = '\0';
    output_buffer->offset += (size_t)length;
    return 1;
}

/* ------------------------------------------------------------------ */
/* String parsing / printing                                           */
/* ------------------------------------------------------------------ */

static unsigned int parse_hex4(const unsigned char *input)
{
    unsigned int h = 0;
    size_t i;
    for (i = 0; i < 4; i++) {
        unsigned char c = input[i];
        if (c >= '0' && c <= '9')      h += c - '0';
        else if (c >= 'A' && c <= 'F') h += 10 + c - 'A';
        else if (c >= 'a' && c <= 'f') h += 10 + c - 'a';
        else return 0;
        if (i < 3) h <<= 4;
    }
    return h;
}

static unsigned char utf16_literal_to_utf8(const unsigned char *input_pointer,
                                            const unsigned char *input_end,
                                            unsigned char **output_pointer)
{
    unsigned long codepoint = 0;
    unsigned int first_code;
    unsigned char utf8_length, first_byte_mark = 0;

    if ((input_end - input_pointer) < 6) return 0;
    first_code = parse_hex4(input_pointer + 2);

    if (first_code >= 0xDC00 && first_code <= 0xDFFF) return 0;

    if (first_code >= 0xD800 && first_code <= 0xDBFF) {
        unsigned int second_code;
        const unsigned char *second_sequence = input_pointer + 6;
        if ((input_end - second_sequence) < 6) return 0;
        if (second_sequence[0] != '\\' || second_sequence[1] != 'u') return 0;
        second_code = parse_hex4(second_sequence + 2);
        if (second_code < 0xDC00 || second_code > 0xDFFF) return 0;
        codepoint = 0x10000 + (((unsigned long)(first_code & 0x3FF) << 10) | (second_code & 0x3FF));
    } else {
        codepoint = (unsigned long)first_code;
    }

    if (codepoint < 0x80) { utf8_length = 1; }
    else if (codepoint < 0x800) { utf8_length = 2; first_byte_mark = 0xC0; }
    else if (codepoint < 0x10000) { utf8_length = 3; first_byte_mark = 0xE0; }
    else if (codepoint <= 0x10FFFF) { utf8_length = 4; first_byte_mark = 0xF0; }
    else return 0;

    {
        unsigned char pos;
        for (pos = (unsigned char)(utf8_length - 1); pos > 0; pos--) {
            (*output_pointer)[pos] = (unsigned char)((codepoint | 0x80) & 0xBF);
            codepoint >>= 6;
        }
        if (utf8_length > 1) (*output_pointer)[0] = (unsigned char)((codepoint | first_byte_mark) & 0xFF);
        else                  (*output_pointer)[0] = (unsigned char)(codepoint & 0x7F);
    }

    *output_pointer += utf8_length;
    return (first_code >= 0xD800 && first_code <= 0xDBFF) ? 12 : 6;
}

static cJSON_bool parse_string(cJSON *item, parse_buffer *input_buffer)
{
    const unsigned char *input_pointer  = buffer_at_offset(input_buffer) + 1;
    const unsigned char *input_end      = buffer_at_offset(input_buffer) + 1;
    unsigned char *output_pointer;
    unsigned char *output;
    size_t allocation_length = 0;
    size_t skipped_bytes = 0;

    if (buffer_at_offset(input_buffer)[0] != '\"') return 0;

    while ((size_t)(input_end - input_buffer->content) < input_buffer->length && *input_end != '\"') {
        if (input_end[0] == '\\') {
            if ((size_t)(input_end + 1 - input_buffer->content) >= input_buffer->length) goto fail;
            skipped_bytes++;
            input_end++;
        }
        input_end++;
    }
    if ((size_t)(input_end - input_buffer->content) >= input_buffer->length || *input_end != '\"') goto fail;

    allocation_length = (size_t)(input_end - buffer_at_offset(input_buffer)) - skipped_bytes;
    output = (unsigned char *)input_buffer->hooks.allocate(allocation_length + 1);
    if (!output) goto fail;

    output_pointer = output;
    while (input_pointer < input_end) {
        if (*input_pointer != '\\') {
            *output_pointer++ = *input_pointer++;
        } else {
            unsigned char seq_len = 2;
            if ((input_end - input_pointer) < 1) goto fail_free;
            switch (input_pointer[1]) {
                case 'b': *output_pointer++ = '\b'; break;
                case 'f': *output_pointer++ = '\f'; break;
                case 'n': *output_pointer++ = '\n'; break;
                case 'r': *output_pointer++ = '\r'; break;
                case 't': *output_pointer++ = '\t'; break;
                case '\"': case '\\': case '/': *output_pointer++ = input_pointer[1]; break;
                case 'u':
                    seq_len = utf16_literal_to_utf8(input_pointer, input_end, &output_pointer);
                    if (!seq_len) goto fail_free;
                    break;
                default: goto fail_free;
            }
            input_pointer += seq_len;
        }
    }
    *output_pointer = '\0';

    item->type = cJSON_String;
    item->valuestring = (char *)output;
    input_buffer->offset = (size_t)(input_end - input_buffer->content) + 1;
    return 1;

fail_free:
    input_buffer->hooks.deallocate(output);
fail:
    return 0;
}

static cJSON_bool print_string_ptr(const unsigned char *input, printbuffer *output_buffer)
{
    const unsigned char *ip;
    unsigned char *output, *op;
    size_t output_length = 0, escape_characters = 0;

    if (!output_buffer) return 0;

    if (!input) {
        output = ensure(output_buffer, 3);
        if (!output) return 0;
        strcpy((char *)output, "\"\"");
        return 1;
    }

    for (ip = input; *ip; ip++) {
        switch (*ip) {
            case '\"': case '\\': case '\b': case '\f': case '\n': case '\r': case '\t':
                escape_characters++; break;
            default:
                if ((unsigned char)*ip < 32) { escape_characters += 5; } break;
        }
    }
    output_length = (size_t)(ip - input) + escape_characters;

    output = ensure(output_buffer, output_length + 3);
    if (!output) return 0;

    output[0] = '\"';
    op = output + 1;

    if (escape_characters == 0) {
        memcpy(op, input, output_length);
        op += output_length;
    } else {
        for (ip = input; *ip; ip++, op++) {
            if ((unsigned char)*ip > 31 && *ip != '\"' && *ip != '\\') {
                *op = *ip;
            } else {
                *op++ = '\\';
                switch (*ip) {
                    case '\\': *op = '\\'; break;
                    case '\"': *op = '\"'; break;
                    case '\b': *op = 'b';  break;
                    case '\f': *op = 'f';  break;
                    case '\n': *op = 'n';  break;
                    case '\r': *op = 'r';  break;
                    case '\t': *op = 't';  break;
                    default:
                        sprintf((char *)op, "u%04x", (unsigned char)*ip);
                        op += 4;
                        break;
                }
            }
        }
    }
    *op++ = '\"';
    *op   = '\0';
    return 1;
}

static cJSON_bool print_string(const cJSON *item, printbuffer *p)
{
    return print_string_ptr((const unsigned char *)item->valuestring, p);
}

/* ------------------------------------------------------------------ */
/* Forward declarations for mutually recursive parse/print functions  */
/* ------------------------------------------------------------------ */

static cJSON_bool parse_value(cJSON *item, parse_buffer *input_buffer);
static cJSON_bool print_value(const cJSON *item, printbuffer *output_buffer);
static cJSON_bool parse_array(cJSON *item, parse_buffer *input_buffer);
static cJSON_bool print_array(const cJSON *item, printbuffer *output_buffer);
static cJSON_bool parse_object(cJSON *item, parse_buffer *input_buffer);
static cJSON_bool print_object(const cJSON *item, printbuffer *output_buffer);

/* ------------------------------------------------------------------ */
/* print_value                                                         */
/* ------------------------------------------------------------------ */

static cJSON_bool print_value(const cJSON *item, printbuffer *output_buffer)
{
    unsigned char *output;
    if (!item || !output_buffer) return 0;
    switch (item->type & 0xFF) {
        case cJSON_NULL:
            output = ensure(output_buffer, 5); if (!output) return 0;
            strcpy((char *)output, "null"); return 1;
        case cJSON_False:
            output = ensure(output_buffer, 6); if (!output) return 0;
            strcpy((char *)output, "false"); return 1;
        case cJSON_True:
            output = ensure(output_buffer, 5); if (!output) return 0;
            strcpy((char *)output, "true"); return 1;
        case cJSON_Number:
            return print_number(item, output_buffer);
        case cJSON_Raw: {
            size_t raw_length;
            if (!item->valuestring) return 0;
            raw_length = strlen(item->valuestring) + 1;
            output = ensure(output_buffer, raw_length); if (!output) return 0;
            memcpy(output, item->valuestring, raw_length); return 1;
        }
        case cJSON_String:  return print_string(item, output_buffer);
        case cJSON_Array:   return print_array(item, output_buffer);
        case cJSON_Object:  return print_object(item, output_buffer);
        default: return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Array parsing / printing                                            */
/* ------------------------------------------------------------------ */

static cJSON_bool parse_array(cJSON *item, parse_buffer *input_buffer)
{
    cJSON *head = NULL, *current_item = NULL;

    if (input_buffer->depth >= CJSON_NESTING_LIMIT) return 0;
    input_buffer->depth++;

    if (buffer_at_offset(input_buffer)[0] != '[') goto fail;
    input_buffer->offset++;
    buffer_skip_whitespace(input_buffer);
    if (can_access_at_index(input_buffer, 0) && buffer_at_offset(input_buffer)[0] == ']') goto success;
    if (cannot_access_at_index(input_buffer, 0)) { input_buffer->offset--; goto fail; }

    input_buffer->offset--;
    do {
        cJSON *new_item = cJSON_New_Item(&input_buffer->hooks);
        if (!new_item) goto fail;
        if (!head) { current_item = head = new_item; }
        else { current_item->next = new_item; new_item->prev = current_item; current_item = new_item; }
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_value(current_item, input_buffer)) goto fail;
        buffer_skip_whitespace(input_buffer);
    } while (can_access_at_index(input_buffer, 0) && buffer_at_offset(input_buffer)[0] == ',');

    if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != ']') goto fail;

success:
    input_buffer->depth--;
    if (head) head->prev = current_item;
    item->type = cJSON_Array;
    item->child = head;
    input_buffer->offset++;
    return 1;
fail:
    if (head) cJSON_Delete(head);
    return 0;
}

static cJSON_bool print_array(const cJSON *item, printbuffer *output_buffer)
{
    unsigned char *output_pointer;
    size_t length;
    cJSON *current = item->child;

    if (!output_buffer) return 0;
    output_pointer = ensure(output_buffer, 1); if (!output_pointer) return 0;
    *output_pointer = '[';
    output_buffer->offset++;
    output_buffer->depth++;

    while (current) {
        if (!print_value(current, output_buffer)) return 0;
        update_offset(output_buffer);
        if (current->next) {
            length = (size_t)(output_buffer->format ? 2 : 1);
            output_pointer = ensure(output_buffer, length + 1); if (!output_pointer) return 0;
            *output_pointer++ = ',';
            if (output_buffer->format) *output_pointer++ = ' ';
            *output_pointer = '\0';
            output_buffer->offset += length;
        }
        current = current->next;
    }

    output_pointer = ensure(output_buffer, 2); if (!output_pointer) return 0;
    *output_pointer++ = ']'; *output_pointer = '\0';
    output_buffer->depth--;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Object parsing / printing                                           */
/* ------------------------------------------------------------------ */

static cJSON_bool parse_object(cJSON *item, parse_buffer *input_buffer)
{
    cJSON *head = NULL, *current_item = NULL;

    if (input_buffer->depth >= CJSON_NESTING_LIMIT) return 0;
    input_buffer->depth++;

    if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != '{') goto fail;
    input_buffer->offset++;
    buffer_skip_whitespace(input_buffer);
    if (can_access_at_index(input_buffer, 0) && buffer_at_offset(input_buffer)[0] == '}') goto success;
    if (cannot_access_at_index(input_buffer, 0)) { input_buffer->offset--; goto fail; }

    input_buffer->offset--;
    do {
        cJSON *new_item = cJSON_New_Item(&input_buffer->hooks);
        if (!new_item) goto fail;
        if (!head) { current_item = head = new_item; }
        else { current_item->next = new_item; new_item->prev = current_item; current_item = new_item; }

        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_string(current_item, input_buffer)) goto fail;
        buffer_skip_whitespace(input_buffer);
        current_item->string = current_item->valuestring;
        current_item->valuestring = NULL;

        if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != ':') goto fail;
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_value(current_item, input_buffer)) goto fail;
        buffer_skip_whitespace(input_buffer);
    } while (can_access_at_index(input_buffer, 0) && buffer_at_offset(input_buffer)[0] == ',');

    if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != '}') goto fail;

success:
    input_buffer->depth--;
    if (head) head->prev = current_item;
    item->type = cJSON_Object;
    item->child = head;
    input_buffer->offset++;
    return 1;
fail:
    if (head) cJSON_Delete(head);
    return 0;
}

static cJSON_bool print_object(const cJSON *item, printbuffer *output_buffer)
{
    unsigned char *output_pointer;
    size_t length;
    cJSON *current = item->child;

    if (!output_buffer) return 0;

    length = (size_t)(output_buffer->format ? 2 : 1);
    output_pointer = ensure(output_buffer, length + 1); if (!output_pointer) return 0;
    *output_pointer++ = '{';
    output_buffer->depth++;
    if (output_buffer->format) *output_pointer++ = '\n';
    *output_pointer = '\0';
    output_buffer->offset += length;

    while (current) {
        if (output_buffer->format) {
            size_t i;
            output_pointer = ensure(output_buffer, output_buffer->depth); if (!output_pointer) return 0;
            for (i = 0; i < output_buffer->depth; i++) *output_pointer++ = '\t';
            output_buffer->offset += output_buffer->depth;
        }
        if (!print_string_ptr((unsigned char *)current->string, output_buffer)) return 0;
        update_offset(output_buffer);

        length = (size_t)(output_buffer->format ? 2 : 1);
        output_pointer = ensure(output_buffer, length); if (!output_pointer) return 0;
        *output_pointer++ = ':';
        if (output_buffer->format) *output_pointer++ = '\t';
        output_buffer->offset += length;

        if (!print_value(current, output_buffer)) return 0;
        update_offset(output_buffer);

        length = (size_t)(output_buffer->format ? 1 : 0) + (current->next ? 1 : 0);
        output_pointer = ensure(output_buffer, length + 1); if (!output_pointer) return 0;
        if (current->next) *output_pointer++ = ',';
        if (output_buffer->format) *output_pointer++ = '\n';
        *output_pointer = '\0';
        output_buffer->offset += length;

        current = current->next;
    }

    output_pointer = ensure(output_buffer, output_buffer->format ? (output_buffer->depth + 1) : 2);
    if (!output_pointer) return 0;
    output_buffer->depth--;
    if (output_buffer->format) {
        size_t i;
        for (i = 0; i < output_buffer->depth; i++) *output_pointer++ = '\t';
    }
    *output_pointer++ = '}';
    *output_pointer = '\0';
    return 1;
}

/* ------------------------------------------------------------------ */
/* parse_value – dispatches to type-specific parsers                  */
/* ------------------------------------------------------------------ */

static cJSON_bool parse_value(cJSON *item, parse_buffer *input_buffer)
{
    if (!input_buffer || !input_buffer->content) return 0;

    if (can_read(input_buffer, 4) && strncmp((const char *)buffer_at_offset(input_buffer), "null", 4) == 0) {
        item->type = cJSON_NULL; input_buffer->offset += 4; return 1;
    }
    if (can_read(input_buffer, 5) && strncmp((const char *)buffer_at_offset(input_buffer), "false", 5) == 0) {
        item->type = cJSON_False; input_buffer->offset += 5; return 1;
    }
    if (can_read(input_buffer, 4) && strncmp((const char *)buffer_at_offset(input_buffer), "true", 4) == 0) {
        item->type = cJSON_True; item->valueint = 1; input_buffer->offset += 4; return 1;
    }
    if (can_access_at_index(input_buffer, 0) && buffer_at_offset(input_buffer)[0] == '\"')
        return parse_string(item, input_buffer);
    if (can_access_at_index(input_buffer, 0) &&
        (buffer_at_offset(input_buffer)[0] == '-' ||
         (buffer_at_offset(input_buffer)[0] >= '0' && buffer_at_offset(input_buffer)[0] <= '9')))
        return parse_number(item, input_buffer);
    if (can_access_at_index(input_buffer, 0) && buffer_at_offset(input_buffer)[0] == '[')
        return parse_array(item, input_buffer);
    if (can_access_at_index(input_buffer, 0) && buffer_at_offset(input_buffer)[0] == '{')
        return parse_object(item, input_buffer);
    return 0;
}

/* ------------------------------------------------------------------ */
/* print() – top-level print dispatcher                               */
/* ------------------------------------------------------------------ */

static unsigned char *do_print(const cJSON *item, int format, const internal_hooks *hooks)
{
    static const size_t default_buffer_size = 256;
    printbuffer buffer;
    unsigned char *printed;

    memset(&buffer, 0, sizeof(buffer));
    buffer.buffer = (unsigned char *)hooks->allocate(default_buffer_size);
    buffer.length = default_buffer_size;
    buffer.format = format;
    buffer.hooks  = *hooks;
    if (!buffer.buffer) return NULL;

    if (!print_value(item, &buffer)) goto fail;
    update_offset(&buffer);
    if (buffer.depth != 0) goto fail;

    printed = (unsigned char *)hooks->allocate(buffer.offset + 1);
    if (!printed) goto fail;
    memcpy(printed, buffer.buffer, buffer.offset + 1);
    hooks->deallocate(buffer.buffer);
    return printed;

fail:
    hooks->deallocate(buffer.buffer);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public parse API                                                    */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length,
                                                 const char **return_parse_end,
                                                 int require_null_terminated)
{
    parse_buffer buffer;
    cJSON *item;

    global_error.json = NULL;
    global_error.position = 0;

    if (!value || buffer_length == 0) return NULL;

    memset(&buffer, 0, sizeof(buffer));
    buffer.content = (const unsigned char *)value;
    buffer.length  = buffer_length;
    buffer.hooks   = global_hooks;

    item = cJSON_New_Item(&global_hooks);
    if (!item) return NULL;

    if (!parse_value(item, buffer_skip_whitespace(skip_utf8_bom(&buffer)))) goto fail;

    if (require_null_terminated) {
        buffer_skip_whitespace(&buffer);
        if (buffer.offset >= buffer.length || buffer_at_offset(&buffer)[0] != '\0') goto fail;
    }
    if (return_parse_end) *return_parse_end = (const char *)buffer_at_offset(&buffer);
    return item;

fail:
    cJSON_Delete(item);
    if (value) {
        global_error.json = (const unsigned char *)value;
        global_error.position = (buffer.offset < buffer.length) ? buffer.offset : (buffer.length > 0 ? buffer.length - 1 : 0);
        if (return_parse_end) *return_parse_end = (const char *)global_error.json + global_error.position;
    }
    return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end,
                                           int require_null_terminated)
{
    if (!value) return NULL;
    return cJSON_ParseWithLengthOpts(value, strlen(value) + 1, return_parse_end, require_null_terminated);
}

CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length)
{
    return cJSON_ParseWithLengthOpts(value, buffer_length, NULL, 0);
}

CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Public print API                                                    */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item)
{
    return (char *)do_print(item, 1, &global_hooks);
}

CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item)
{
    return (char *)do_print(item, 0, &global_hooks);
}

CJSON_PUBLIC(char *) cJSON_PrintBuffered(const cJSON *item, int prebuffer, int fmt)
{
    printbuffer p;
    if (prebuffer < 0) return NULL;
    memset(&p, 0, sizeof(p));
    p.buffer = (unsigned char *)global_hooks.allocate((size_t)prebuffer);
    if (!p.buffer) return NULL;
    p.length = (size_t)prebuffer;
    p.format = fmt;
    p.hooks  = global_hooks;
    if (!print_value(item, &p)) { global_hooks.deallocate(p.buffer); return NULL; }
    return (char *)p.buffer;
}

CJSON_PUBLIC(int) cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const int format)
{
    printbuffer p;
    if (length < 0 || !buffer) return 0;
    memset(&p, 0, sizeof(p));
    p.buffer  = (unsigned char *)buffer;
    p.length  = (size_t)length;
    p.noalloc = 1;
    p.format  = format;
    p.hooks   = global_hooks;
    return print_value(item, &p);
}

/* ------------------------------------------------------------------ */
/* Array / object access                                               */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array)
{
    cJSON *child; int size = 0;
    if (!array) return 0;
    for (child = array->child; child; child = child->next) size++;
    return size;
}

static cJSON *get_array_item(const cJSON *array, size_t index)
{
    cJSON *c;
    if (!array) return NULL;
    for (c = array->child; c && index > 0; c = c->next) index--;
    return c;
}

CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index)
{
    if (index < 0) return NULL;
    return get_array_item(array, (size_t)index);
}

static int case_insensitive_strcmp(const unsigned char *s1, const unsigned char *s2)
{
    if (!s1 || !s2) return 1;
    if (s1 == s2) return 0;
    for (; tolower(*s1) == tolower(*s2); s1++, s2++) if (!*s1) return 0;
    return tolower(*s1) - tolower(*s2);
}

static cJSON *get_object_item(const cJSON *object, const char *name, int case_sensitive)
{
    cJSON *c;
    if (!object || !name) return NULL;
    for (c = object->child; c; c = c->next) {
        if (!c->string) continue;
        if (case_sensitive ? strcmp(name, c->string) == 0
                           : case_insensitive_strcmp((const unsigned char *)name,
                                                     (const unsigned char *)c->string) == 0)
            return c;
    }
    return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string)
{
    return get_object_item(object, string, 0);
}

CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string)
{
    return get_object_item(object, string, 1);
}

CJSON_PUBLIC(int) cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item)
{
    if (!cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item)
{
    if (!cJSON_IsNumber(item)) return (double)0.0 / 0.0;
    return item->valuedouble;
}

/* ------------------------------------------------------------------ */
/* Type checks                                                         */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(int) cJSON_IsInvalid(const cJSON * const i) { return i && (i->type & 0xFF) == cJSON_Invalid; }
CJSON_PUBLIC(int) cJSON_IsFalse(const cJSON * const i)   { return i && (i->type & 0xFF) == cJSON_False; }
CJSON_PUBLIC(int) cJSON_IsTrue(const cJSON * const i)    { return i && (i->type & 0xFF) == cJSON_True; }
CJSON_PUBLIC(int) cJSON_IsBool(const cJSON * const i)    { return i && (i->type & (cJSON_True | cJSON_False)) != 0; }
CJSON_PUBLIC(int) cJSON_IsNull(const cJSON * const i)    { return i && (i->type & 0xFF) == cJSON_NULL; }
CJSON_PUBLIC(int) cJSON_IsNumber(const cJSON * const i)  { return i && (i->type & 0xFF) == cJSON_Number; }
CJSON_PUBLIC(int) cJSON_IsString(const cJSON * const i)  { return i && (i->type & 0xFF) == cJSON_String; }
CJSON_PUBLIC(int) cJSON_IsArray(const cJSON * const i)   { return i && (i->type & 0xFF) == cJSON_Array; }
CJSON_PUBLIC(int) cJSON_IsObject(const cJSON * const i)  { return i && (i->type & 0xFF) == cJSON_Object; }
CJSON_PUBLIC(int) cJSON_IsRaw(const cJSON * const i)     { return i && (i->type & 0xFF) == cJSON_Raw; }

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void)   { cJSON *i = cJSON_New_Item(&global_hooks); if(i) i->type = cJSON_NULL;  return i; }
CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void)   { cJSON *i = cJSON_New_Item(&global_hooks); if(i) i->type = cJSON_True;  return i; }
CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void)  { cJSON *i = cJSON_New_Item(&global_hooks); if(i) i->type = cJSON_False; return i; }

CJSON_PUBLIC(cJSON *) cJSON_CreateBool(int b)
{
    cJSON *i = cJSON_New_Item(&global_hooks);
    if(i) i->type = b ? cJSON_True : cJSON_False;
    return i;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num)
{
    cJSON *i = cJSON_New_Item(&global_hooks);
    if(i) {
        i->type = cJSON_Number;
        i->valuedouble = num;
        i->valueint = (num >= INT_MAX) ? INT_MAX : ((num <= (double)INT_MIN) ? INT_MIN : (int)num);
    }
    return i;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string)
{
    cJSON *i = cJSON_New_Item(&global_hooks);
    if(i) {
        i->type = cJSON_String;
        i->valuestring = (char *)cJSON_strdup((const unsigned char *)string, &global_hooks);
        if (!i->valuestring) { cJSON_Delete(i); return NULL; }
    }
    return i;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateStringReference(const char *string)
{
    cJSON *i = cJSON_New_Item(&global_hooks);
    if(i) { i->type = cJSON_String | cJSON_IsReference; i->valuestring = (char *)string; }
    return i;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateRaw(const char *raw)
{
    cJSON *i = cJSON_New_Item(&global_hooks);
    if(i) {
        i->type = cJSON_Raw;
        i->valuestring = (char *)cJSON_strdup((const unsigned char *)raw, &global_hooks);
        if (!i->valuestring) { cJSON_Delete(i); return NULL; }
    }
    return i;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void)  { cJSON *i = cJSON_New_Item(&global_hooks); if(i) i->type = cJSON_Array;  return i; }
CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void) { cJSON *i = cJSON_New_Item(&global_hooks); if(i) i->type = cJSON_Object; return i; }

CJSON_PUBLIC(cJSON *) cJSON_CreateObjectReference(const cJSON *child)
{
    cJSON *i = cJSON_New_Item(&global_hooks);
    if(i) { i->type = cJSON_Object | cJSON_IsReference; i->child = (cJSON *)child; }
    return i;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateArrayReference(const cJSON *child)
{
    cJSON *i = cJSON_New_Item(&global_hooks);
    if(i) { i->type = cJSON_Array | cJSON_IsReference; i->child = (cJSON *)child; }
    return i;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateIntArray(const int *numbers, int count)
{
    cJSON *a, *n = NULL, *p = NULL; int i;
    if (count < 0 || !numbers) return NULL;
    a = cJSON_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = cJSON_CreateNumber(numbers[i]); if(!n){cJSON_Delete(a);return NULL;}
        if(!i) a->child = n; else { p->next = n; n->prev = p; }
        p = n;
    }
    if (a && a->child) a->child->prev = n;
    return a;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateFloatArray(const float *numbers, int count)
{
    cJSON *a, *n = NULL, *p = NULL; int i;
    if (count < 0 || !numbers) return NULL;
    a = cJSON_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = cJSON_CreateNumber((double)numbers[i]); if(!n){cJSON_Delete(a);return NULL;}
        if(!i) a->child = n; else { p->next = n; n->prev = p; }
        p = n;
    }
    if (a && a->child) a->child->prev = n;
    return a;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateDoubleArray(const double *numbers, int count)
{
    cJSON *a, *n = NULL, *p = NULL; int i;
    if (count < 0 || !numbers) return NULL;
    a = cJSON_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = cJSON_CreateNumber(numbers[i]); if(!n){cJSON_Delete(a);return NULL;}
        if(!i) a->child = n; else { p->next = n; n->prev = p; }
        p = n;
    }
    if (a && a->child) a->child->prev = n;
    return a;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char **strings, int count)
{
    cJSON *a, *n = NULL, *p = NULL; int i;
    if (count < 0 || !strings) return NULL;
    a = cJSON_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = cJSON_CreateString(strings[i]); if(!n){cJSON_Delete(a);return NULL;}
        if(!i) a->child = n; else { p->next = n; n->prev = p; }
        p = n;
    }
    if (a && a->child) a->child->prev = n;
    return a;
}

/* ------------------------------------------------------------------ */
/* Add items                                                           */
/* ------------------------------------------------------------------ */

static void suffix_object(cJSON *prev, cJSON *item)
{
    prev->next = item; item->prev = prev;
}

static cJSON *create_reference(const cJSON *item, const internal_hooks *hooks)
{
    cJSON *ref = cJSON_New_Item(hooks);
    if (!ref) return NULL;
    memcpy(ref, item, sizeof(cJSON));
    ref->string = NULL;
    ref->type |= cJSON_IsReference;
    ref->next = ref->prev = NULL;
    return ref;
}

static cJSON_bool add_item_to_array(cJSON *array, cJSON *item)
{
    cJSON *child;
    if (!item || !array || array == item) return 0;
    child = array->child;
    if (!child) { array->child = item; item->prev = item; item->next = NULL; }
    else { if (child->prev) { suffix_object(child->prev, item); array->child->prev = item; } }
    return 1;
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    return add_item_to_array(array, item);
}

static cJSON_bool add_item_to_object(cJSON *object, const char *string, cJSON *item,
                                      const internal_hooks *hooks, int constant_key)
{
    char *new_key; int new_type;
    if (!object || !string || !item || object == item) return 0;

    if (constant_key) {
        new_key  = (char *)string;
        new_type = item->type | cJSON_StringIsConst;
    } else {
        new_key  = (char *)cJSON_strdup((const unsigned char *)string, hooks);
        if (!new_key) return 0;
        new_type = item->type & ~cJSON_StringIsConst;
    }
    if (!(item->type & cJSON_StringIsConst) && item->string) hooks->deallocate(item->string);
    item->string = new_key;
    item->type   = new_type;
    return add_item_to_array(object, item);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    return add_item_to_object(object, string, item, &global_hooks, 0);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item)
{
    return add_item_to_object(object, string, item, &global_hooks, 1);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item)
{
    if (!array) return 0;
    return add_item_to_array(array, create_reference(item, &global_hooks));
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item)
{
    if (!object || !string) return 0;
    return add_item_to_object(object, string, create_reference(item, &global_hooks), &global_hooks, 0);
}

/* ------------------------------------------------------------------ */
/* Detach / delete items                                               */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(cJSON *) cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item)
{
    if (!parent || !item) return NULL;
    if (item != parent->child) item->prev->next = item->next;
    if (item->next) item->next->prev = item->prev;
    if (item == parent->child) parent->child = item->next;
    else if (!item->next) parent->child->prev = item->prev;
    item->prev = item->next = NULL;
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromArray(cJSON *array, int which)
{
    if (which < 0) return NULL;
    return cJSON_DetachItemViaPointer(array, get_array_item(array, (size_t)which));
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *array, int which)
{
    cJSON_Delete(cJSON_DetachItemFromArray(array, which));
}

CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObject(cJSON *object, const char *string)
{
    return cJSON_DetachItemViaPointer(object, cJSON_GetObjectItem(object, string));
}

CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string)
{
    return cJSON_DetachItemViaPointer(object, cJSON_GetObjectItemCaseSensitive(object, string));
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *object, const char *string)
{
    cJSON_Delete(cJSON_DetachItemFromObject(object, string));
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string)
{
    cJSON_Delete(cJSON_DetachItemFromObjectCaseSensitive(object, string));
}

/* ------------------------------------------------------------------ */
/* Insert / replace                                                    */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem)
{
    cJSON *after;
    if (which < 0) return 0;
    after = get_array_item(array, (size_t)which);
    if (!after) return add_item_to_array(array, newitem);
    newitem->next = after; newitem->prev = after->prev;
    after->prev = newitem;
    if (after == array->child) array->child = newitem;
    else newitem->prev->next = newitem;
    return 1;
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON *replacement)
{
    if (!parent || !replacement || !item) return 0;
    if (replacement == item) return 1;
    replacement->next = item->next;
    replacement->prev = item->prev;
    if (replacement->next) replacement->next->prev = replacement;
    if (parent->child == item) {
        if (parent->child->prev == parent->child) replacement->prev = replacement;
        parent->child = replacement;
    } else {
        if (replacement->prev) replacement->prev->next = replacement;
        if (!replacement->next) parent->child->prev = replacement;
    }
    item->next = item->prev = NULL;
    cJSON_Delete(item);
    return 1;
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem)
{
    if (which < 0) return 0;
    return cJSON_ReplaceItemViaPointer(array, get_array_item(array, (size_t)which), newitem);
}

static cJSON_bool replace_item_in_object(cJSON *object, const char *string, cJSON *replacement, int case_sensitive)
{
    if (!replacement || !string) return 0;
    if (!(replacement->type & cJSON_StringIsConst) && replacement->string) {
        global_hooks.deallocate(replacement->string);
    }
    replacement->string = (char *)cJSON_strdup((const unsigned char *)string, &global_hooks);
    if (!replacement->string) return 0;
    replacement->type &= ~cJSON_StringIsConst;
    return cJSON_ReplaceItemViaPointer(object, get_object_item(object, string, case_sensitive), replacement);
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *object, const char *string, cJSON *newitem)
{
    return replace_item_in_object(object, string, newitem, 0);
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object, const char *string, cJSON *newitem)
{
    return replace_item_in_object(object, string, newitem, 1);
}

/* ------------------------------------------------------------------ */
/* Duplicate                                                           */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(cJSON *) cJSON_Duplicate(const cJSON *item, int recurse)
{
    cJSON *newitem, *child, *next, *newchild = NULL;
    if (!item) return NULL;
    newitem = cJSON_New_Item(&global_hooks); if (!newitem) return NULL;
    newitem->type = item->type & ~cJSON_IsReference;
    newitem->valueint = item->valueint;
    newitem->valuedouble = item->valuedouble;
    if (item->valuestring) {
        newitem->valuestring = (char *)cJSON_strdup((unsigned char *)item->valuestring, &global_hooks);
        if (!newitem->valuestring) { cJSON_Delete(newitem); return NULL; }
    }
    if (item->string) {
        newitem->string = (item->type & cJSON_StringIsConst) ? item->string :
            (char *)cJSON_strdup((unsigned char *)item->string, &global_hooks);
        if (!newitem->string) { cJSON_Delete(newitem); return NULL; }
    }
    if (!recurse) return newitem;
    child = item->child; next = NULL;
    while (child) {
        newchild = cJSON_Duplicate(child, 1);
        if (!newchild) { cJSON_Delete(newitem); return NULL; }
        if (next) { next->next = newchild; newchild->prev = next; }
        else newitem->child = newchild;
        next = newchild;
        child = child->next;
    }
    if (newitem->child) newitem->child->prev = next;
    return newitem;
}

/* ------------------------------------------------------------------ */
/* Compare                                                             */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(int) cJSON_Compare(const cJSON * const a, const cJSON * const b, const int case_sensitive)
{
    if (!a || !b || (a->type & 0xFF) != (b->type & 0xFF)) return 0;
    switch (a->type & 0xFF) {
        case cJSON_False: case cJSON_True: case cJSON_NULL: return 1;
        case cJSON_Number: return (fabs(a->valuedouble - b->valuedouble) <= fmax(fabs(a->valuedouble), fabs(b->valuedouble)) * DBL_EPSILON) ? 1 : 0;
        case cJSON_String: case cJSON_Raw:
            if (!a->valuestring || !b->valuestring) return 0;
            return strcmp(a->valuestring, b->valuestring) == 0 ? 1 : 0;
        case cJSON_Array: {
            cJSON *ae = a->child, *be = b->child;
            for (; ae && be; ae = ae->next, be = be->next) if (!cJSON_Compare(ae, be, case_sensitive)) return 0;
            return ae == be ? 1 : 0;
        }
        case cJSON_Object: {
            cJSON *ae; const cJSON *be2;
            cJSON_ArrayForEach(ae, a) {
                be2 = get_object_item(b, ae->string, case_sensitive);
                if (!be2 || !cJSON_Compare(ae, be2, case_sensitive)) return 0;
            }
            cJSON_ArrayForEach(ae, b) {
                be2 = get_object_item(a, ae->string, case_sensitive);
                if (!be2 || !cJSON_Compare(ae, be2, case_sensitive)) return 0;
            }
            return 1;
        }
        default: return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Minify                                                              */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(void) cJSON_Minify(char *json)
{
    char *into = json;
    if (!json) return;
    while (*json) {
        if (*json == ' ' || *json == '\t' || *json == '\r' || *json == '\n') { json++; }
        else if (*json == '/' && json[1] == '/') {
            while (*json && *json != '\n') json++;
        } else if (*json == '/' && json[1] == '*') {
            json += 2;
            while (*json && !(*json == '*' && json[1] == '/')) json++;
            if (*json) json += 2;
        } else if (*json == '\"') {
            *into++ = *json++;
            while (*json && *json != '\"') { if (*json == '\\') *into++ = *json++; *into++ = *json++; }
            *into++ = *json++; /* closing quote */
        } else {
            *into++ = *json++;
        }
    }
    *into = '\0';
}

/* ------------------------------------------------------------------ */
/* Helper add functions                                                */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(cJSON *) cJSON_AddNullToObject(cJSON * const object, const char * const name)
{
    cJSON *n = cJSON_CreateNull();
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_AddTrueToObject(cJSON * const object, const char * const name)
{
    cJSON *n = cJSON_CreateTrue();
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_AddFalseToObject(cJSON * const object, const char * const name)
{
    cJSON *n = cJSON_CreateFalse();
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const int boolean)
{
    cJSON *n = cJSON_CreateBool(boolean);
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number)
{
    cJSON *n = cJSON_CreateNumber(number);
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string)
{
    cJSON *n = cJSON_CreateString(string);
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_AddRawToObject(cJSON * const object, const char * const name, const char * const raw)
{
    cJSON *n = cJSON_CreateRaw(raw);
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_AddObjectToObject(cJSON * const object, const char * const name)
{
    cJSON *n = cJSON_CreateObject();
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_AddArrayToObject(cJSON * const object, const char * const name)
{
    cJSON *n = cJSON_CreateArray();
    if (add_item_to_object(object, name, n, &global_hooks, 0)) return n;
    cJSON_Delete(n); return NULL;
}

/* ------------------------------------------------------------------ */
/* Number helper                                                       */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number)
{
    object->valueint = (number >= INT_MAX) ? INT_MAX : ((number <= (double)INT_MIN) ? INT_MIN : (int)number);
    return object->valuedouble = number;
}

CJSON_PUBLIC(char *) cJSON_SetValuestring(cJSON *object, const char *valuestring)
{
    char *copy;
    if (!(object->type & cJSON_String) || (object->type & cJSON_IsReference)) return NULL;
    if (strlen(valuestring) <= strlen(object->valuestring)) {
        strcpy(object->valuestring, valuestring); return object->valuestring;
    }
    copy = (char *)cJSON_strdup((const unsigned char *)valuestring, &global_hooks);
    if (!copy) return NULL;
    global_hooks.deallocate(object->valuestring);
    object->valuestring = copy;
    return copy;
}

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

CJSON_PUBLIC(void *) cJSON_malloc(size_t size)  { return global_hooks.allocate(size); }
CJSON_PUBLIC(void) cJSON_free(void *object)      { global_hooks.deallocate(object); }
