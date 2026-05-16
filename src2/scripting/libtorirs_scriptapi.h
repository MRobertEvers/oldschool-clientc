#ifndef LIBTORIRS_SCRIPTAPI_H
#define LIBTORIRS_SCRIPTAPI_H

#include "../libtorirs.h"
#include "libtorirs_scripting.h"

#include <stdbool.h>
#include <stdint.h>

void
LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(struct LibToriRS_Instance* instance);

#endif
