#ifndef LIBTORIRS_H
#define LIBTORIRS_H

#include <stdint.h>

struct LibToriRS_Instance;

struct LibToriRS_Instance*
LibToriRS_InstanceNew(void);

void
LibToriRS_InstanceFree(struct LibToriRS_Instance* instance);

struct LibToriRS_ScriptQueue*
LibToriRS_GetScriptQueue(struct LibToriRS_Instance* instance);

#endif