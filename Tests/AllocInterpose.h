#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void openeqAllocReset (void);
void openeqAllocSetArmed (int armed);
void openeqAllocRead (long long* mallocs, long long* callocs, long long* reallocs);
void* openeqAllocFirstCaller (int which);

/* Legacy GodEQ aliases */
void godeqAllocReset (void);
void godeqAllocSetArmed (int armed);
void godeqAllocRead (long long* mallocs, long long* callocs, long long* reallocs);
void* godeqAllocFirstCaller (int which);

#ifdef __cplusplus
}
#endif
