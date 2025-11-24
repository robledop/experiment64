/*
 * Copyright (c) 2014, 2015, 2024 Jonas 'Sortie' Termansen.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * ubsan/ubsan.c
 * Undefined behavior sanitizer runtime support.
 */

#include "debug.h"
#include "terminal.h"
#include <stdint.h>

#define UNDEFINED_BEHAVIOR 3

struct undefined_behavior
{
    const char *filename;
    unsigned long line;
    unsigned long column;
    const char *violation;
};

[[noreturn]] void report_undefined_behavior(const int event, const struct undefined_behavior *info)
{
    printk(KBWHT "Event:" KWHT " %d\n", event);
    printk(KBWHT "File:" KWHT " %s\n", info->filename);
    printk(KBWHT "Line:" KWHT " %lu\n", info->line);
    printk(KBWHT "Column:" KWHT " %lu\n", info->column);
    printk(KBWHT "Violation:" KYEL " %s\n" KWHT, info->violation);
    panic("Undefined behavior detected");

    __builtin_unreachable();
}

struct ubsan_source_location
{
    const char *filename;
    uint32_t line;
    uint32_t column;
};

struct ubsan_type_descriptor
{
    uint16_t type_kind;
    uint16_t type_info;
    char type_name[];
};

struct ubsan_type_mismatch_data
{
    struct ubsan_source_location location;
    struct ubsan_type_descriptor *type;
    unsigned long alignment;
    unsigned char type_check_kind;
};

struct ubsan_overflow_data
{
    struct ubsan_source_location location;
    struct ubsan_type_descriptor *type;
};

struct ubsan_shift_out_of_bounds_data
{
    struct ubsan_source_location location;
    struct ubsan_type_descriptor *lhs_type;
    struct ubsan_type_descriptor *rhs_type;
};

struct ubsan_out_of_bounds_data
{
    struct ubsan_source_location location;
    struct ubsan_type_descriptor *array_type;
    struct ubsan_type_descriptor *index_type;
};

struct ubsan_unreachable_data
{
    struct ubsan_source_location location;
};

struct ubsan_vla_bound_not_positive_data
{
    struct ubsan_source_location location;
    struct ubsan_type_descriptor *type;
};

struct ubsan_float_cast_overflow_data
{
    struct ubsan_source_location location;
    struct ubsan_type_descriptor *from_type;
    struct ubsan_type_descriptor *to_type;
};

struct ubsan_load_invalid_value_data
{
    struct ubsan_source_location location;
    struct ubsan_type_descriptor *type;
};

struct ubsan_function_type_mismatch_data
{
    struct ubsan_source_location location;
    struct ubsan_type_descriptor *type;
};

struct ubsan_nonnull_return_data
{
    struct ubsan_source_location location;
};

struct ubsan_nonnull_arg_data
{
    struct ubsan_source_location location;
};

struct ubsan_pointer_overflow_data
{
    struct ubsan_source_location location;
};

struct ubsan_alignment_assumption_data
{
    struct ubsan_source_location location;
    struct ubsan_source_location assumption_location;
    struct ubsan_type_descriptor *type;
};

void __ubsan_handle_type_mismatch_v1(struct ubsan_type_mismatch_data *data, unsigned long ptr)
{
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    if (!ptr)
        info.violation = "NULL pointer dereference";
    else if (data->alignment && (ptr & (data->alignment - 1)))
        info.violation = "Unaligned memory access";
    else
        info.violation = "Type mismatch";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_add_overflow(struct ubsan_overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Signed integer overflow (addition)";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_sub_overflow(struct ubsan_overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Signed integer overflow (subtraction)";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_mul_overflow(struct ubsan_overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Signed integer overflow (multiplication)";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_negate_overflow(struct ubsan_overflow_data *data, unsigned long old_val)
{
    (void)old_val;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Signed integer overflow (negation)";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_divrem_overflow(struct ubsan_overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Division remainder overflow";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_shift_out_of_bounds(struct ubsan_shift_out_of_bounds_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Shift out of bounds";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_out_of_bounds(struct ubsan_out_of_bounds_data *data, unsigned long index)
{
    (void)index;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Out of bounds";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_builtin_unreachable(struct ubsan_unreachable_data *data)
{
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Reached unreachable code";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_missing_return(struct ubsan_unreachable_data *data)
{
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Missing return";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_vla_bound_not_positive(struct ubsan_vla_bound_not_positive_data *data, unsigned long bound)
{
    (void)bound;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "VLA bound not positive";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_float_cast_overflow(struct ubsan_float_cast_overflow_data *data, unsigned long from)
{
    (void)from;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Float cast overflow";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_load_invalid_value(struct ubsan_load_invalid_value_data *data, unsigned long val)
{
    (void)val;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Load invalid value";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_function_type_mismatch(struct ubsan_function_type_mismatch_data *data, unsigned long ptr)
{
    (void)ptr;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Function type mismatch";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_nonnull_return_v1(struct ubsan_nonnull_return_data *data, struct ubsan_source_location *loc)
{
    (void)loc;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Nonnull return";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_nonnull_arg(struct ubsan_nonnull_arg_data *data)
{
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Nonnull argument";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_pointer_overflow(struct ubsan_pointer_overflow_data *data, unsigned long base, unsigned long result)
{
    (void)base;
    (void)result;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Pointer overflow";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}

void __ubsan_handle_alignment_assumption(struct ubsan_alignment_assumption_data *data, unsigned long ptr, unsigned long alignment, unsigned long offset)
{
    (void)ptr;
    (void)alignment;
    (void)offset;
    struct undefined_behavior info;
    info.filename = data->location.filename;
    info.line = data->location.line;
    info.column = data->location.column;
    info.violation = "Alignment assumption";
    report_undefined_behavior(UNDEFINED_BEHAVIOR, &info);
}
