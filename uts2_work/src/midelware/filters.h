#pragma once

#include <stdint.h>
#include <string.h>

#define MEDIAN_WINDOW_SIZE 5

/* Структура истории для одного канала */
typedef struct {
    uint16_t history[MEDIAN_WINDOW_SIZE];
    uint8_t  head;
} channel_median_filter_t;

uint16_t apply_median_filter_5(channel_median_filter_t *filter, uint16_t new_val);