#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "global_params.h"

int param_set(PARAM_ID id, const PARAM_VAL *val, bool is_async);
int param_get(PARAM_ID id, PARAM_VAL *val,bool is_async);

#ifdef __cplusplus
}
#endif