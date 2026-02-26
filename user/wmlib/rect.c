#include <wm/rect.h>
#include <array.h>
#include <stdlib.h>

static constexpr size_t RECT_SPLIT_MAX_PARTS = 4;

static void rect_free_range(rect_t **rects, size_t start, size_t end)
{
    for (size_t i = start; i < end; i++)
        free(rects[i]);
}

static rect_t **rect_array_new(size_t count)
{
    if (count == 0)
        return nullptr;

    size_t alloc_size = sizeof(array_header_t) + (sizeof(rect_t *) * count);
    auto header = (array_header_t *)malloc(alloc_size);
    if (!header)
        return nullptr;

    header->magic    = ARR_HEADER_MAGIC;
    header->count    = count;
    header->capacity = count;
    return (rect_t **)(header + 1);
}

static bool rect_add_split(rect_t **parts, size_t *part_count, int top, int left, int bottom, int right)
{
    if (*part_count >= RECT_SPLIT_MAX_PARTS)
        return false;

    rect_t *temp_rect = rect_new(top, left, bottom, right);
    if (!temp_rect)
        return false;

    parts[*part_count] = temp_rect;
    (*part_count)++;
    return true;
}

rect_t *rect_new(int top, int left, int bottom, int right)
{
    auto rect = (rect_t *)malloc(sizeof(rect_t));
    if (!rect) {
        return rect;
    }

    rect->top    = top;
    rect->left   = left;
    rect->bottom = bottom;
    rect->right  = right;

    return rect;
}

rect_t **rect_split(const rect_t *subject_rect, const rect_t *cutting_rect)
{
    rect_t *parts[RECT_SPLIT_MAX_PARTS] = {nullptr};
    size_t part_count                   = 0;

    rect_t subject_copy;
    subject_copy.top    = subject_rect->top;
    subject_copy.left   = subject_rect->left;
    subject_copy.bottom = subject_rect->bottom;
    subject_copy.right  = subject_rect->right;

    if (cutting_rect->left > subject_copy.left && cutting_rect->left <= subject_copy.right) {
        if (!rect_add_split(parts,
                            &part_count,
                            subject_copy.top,
                            subject_copy.left,
                            subject_copy.bottom,
                            cutting_rect->left - 1)) {
            rect_free_range(parts, 0, part_count);
            return nullptr;
        }
        subject_copy.left = cutting_rect->left;
    }

    if (cutting_rect->top > subject_copy.top && cutting_rect->top <= subject_copy.bottom) {
        if (!rect_add_split(parts,
                            &part_count,
                            subject_copy.top,
                            subject_copy.left,
                            cutting_rect->top - 1,
                            subject_copy.right)) {
            rect_free_range(parts, 0, part_count);
            return nullptr;
        }
        subject_copy.top = cutting_rect->top;
    }

    if (cutting_rect->right >= subject_copy.left && cutting_rect->right < subject_copy.right) {
        if (!rect_add_split(parts,
                            &part_count,
                            subject_copy.top,
                            cutting_rect->right + 1,
                            subject_copy.bottom,
                            subject_copy.right)) {
            rect_free_range(parts, 0, part_count);
            return nullptr;
        }
        subject_copy.right = cutting_rect->right;
    }

    if (cutting_rect->bottom >= subject_copy.top && cutting_rect->bottom < subject_copy.bottom) {
        if (!rect_add_split(parts,
                            &part_count,
                            cutting_rect->bottom + 1,
                            subject_copy.left,
                            subject_copy.bottom,
                            subject_copy.right)) {
            rect_free_range(parts, 0, part_count);
            return nullptr;
        }
        subject_copy.bottom = cutting_rect->bottom;
    }

    rect_t **output_rects = rect_array_new(part_count);
    if (!output_rects) {
        rect_free_range(parts, 0, part_count);
        return nullptr;
    }

    for (size_t i = 0; i < part_count; i++)
        output_rects[i] = parts[i];

    return output_rects;
}

rect_t *rect_intersect(rect_t *rect_a, rect_t *rect_b)
{
    if (!rect_a || !rect_b) {
        return nullptr;
    }

    if (!(rect_a->left <= rect_b->right && rect_a->right >= rect_b->left && rect_a->top <= rect_b->bottom &&
        rect_a->bottom >= rect_b->top)) {
        return nullptr;
    }

    rect_t *result_rect = rect_new(rect_a->top, rect_a->left, rect_a->bottom, rect_a->right);
    if (!result_rect) {
        return nullptr;
    }

    if (rect_b->left > result_rect->left && rect_b->left <= result_rect->right) {
        result_rect->left = rect_b->left;
    }

    if (rect_b->top > result_rect->top && rect_b->top <= result_rect->bottom) {
        result_rect->top = rect_b->top;
    }

    if (rect_b->right >= result_rect->left && rect_b->right < result_rect->right) {
        result_rect->right = rect_b->right;
    }

    if (rect_b->bottom >= result_rect->top && rect_b->bottom < result_rect->bottom) {
        result_rect->bottom = rect_b->bottom;
    }

    return result_rect;
}
