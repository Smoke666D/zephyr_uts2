#ifndef PARAM_SERVER_H_
#define PARAM_SERVER_H_

#include "global_params.h"

int param_set(PARAM_ID id, const PARAM_VAL *val);
int param_get(PARAM_ID id, PARAM_VAL *val);

#endif /* PARAM_SERVER_H_ */