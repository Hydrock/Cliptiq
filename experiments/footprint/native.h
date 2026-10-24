#pragma once
#include <stddef.h>
bool native_setup(bool visible);
void native_loop();
void native_cleanup();
void native_schedule_close();
size_t native_memory();
