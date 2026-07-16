#ifndef MEMORY_LINK_MACRO_H
#define MEMORY_LINK_MACRO_H

#include <zephyr/kernel.h>

/**
 * @brief Определить стек потока в DTCM
 *
 * @param name  Имя переменной стека
 * @param size  Размер стека в байтах
 */
#define K_THREAD_STACK_DEFINE_DTCM(name, size) \
    static uint8_t __attribute__((__section__("DTCM"))) name[size]

#endif