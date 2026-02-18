#include <wm/rect.h>
#include <stdlib.h>

rect_t *rect_new(int top, int left, int bottom, int right)
{
    rect_t *rect = (rect_t *)malloc(sizeof(rect_t));
    if (!rect) {
        return rect;
    }

    rect->top = top;
    rect->left = left;
    rect->bottom = bottom;
    rect->right = right;

    return rect;
}

list_t *rect_split(rect_t *subject_rect, rect_t *cutting_rect)
{
    list_t *output_rects = list_new();
    if (!output_rects) {
        return output_rects;
    }

    rect_t subject_copy;
    subject_copy.top = subject_rect->top;
    subject_copy.left = subject_rect->left;
    subject_copy.bottom = subject_rect->bottom;
    subject_copy.right = subject_rect->right;

    rect_t *temp_rect;

    if (cutting_rect->left > subject_copy.left && cutting_rect->left <= subject_copy.right) {
        temp_rect = rect_new(subject_copy.top, subject_copy.left, subject_copy.bottom, cutting_rect->left - 1);
        if (!temp_rect) {
            free(output_rects);
            return nullptr;
        }
        list_add(output_rects, temp_rect);
        subject_copy.left = cutting_rect->left;
    }

    if (cutting_rect->top > subject_copy.top && cutting_rect->top <= subject_copy.bottom) {
        temp_rect = rect_new(subject_copy.top, subject_copy.left, cutting_rect->top - 1, subject_copy.right);
        if (temp_rect == nullptr) {
            for (; output_rects->count; temp_rect = list_remove_at(output_rects, 0))
            {
                free(temp_rect);
            }
            free(output_rects);
            return nullptr;
        }
        list_add(output_rects, temp_rect);
        subject_copy.top = cutting_rect->top;
    }

    if (cutting_rect->right >= subject_copy.left && cutting_rect->right < subject_copy.right) {
        temp_rect = rect_new(subject_copy.top, cutting_rect->right + 1, subject_copy.bottom, subject_copy.right);
        if (temp_rect == nullptr) {
            for (; output_rects->count; temp_rect = list_remove_at(output_rects, 0))
                free(temp_rect);
            free(output_rects);
            return nullptr;
        }
        list_add(output_rects, temp_rect);
        subject_copy.right = cutting_rect->right;
    }

    if (cutting_rect->bottom >= subject_copy.top && cutting_rect->bottom < subject_copy.bottom) {
        temp_rect = rect_new(cutting_rect->bottom + 1, subject_copy.left, subject_copy.bottom, subject_copy.right);
        if (temp_rect == nullptr) {
            for (; output_rects->count; temp_rect = list_remove_at(output_rects, 0))
                free(temp_rect);
            free(output_rects);
            return nullptr;
        }
        list_add(output_rects, temp_rect);
        subject_copy.bottom = cutting_rect->bottom;
    }

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
