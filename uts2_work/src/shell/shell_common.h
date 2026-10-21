#pragma once
#include <stdint.h>
#include <stdlib.h>  /* Для strtol */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "shell_common.h"

bool shell_keypress_check(const struct shell *sh);