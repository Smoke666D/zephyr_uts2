#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "global_params.h"

int param_set(PARAM_ID id, const PARAM_VAL *val);
int param_get(PARAM_ID id, PARAM_VAL *val);

#ifdef __cplusplus
}
#endif