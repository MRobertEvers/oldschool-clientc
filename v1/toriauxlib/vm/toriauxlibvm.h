#ifndef TORIAUXLIBVM_H
#define TORIAUXLIBVM_H

#include <stdbool.h>
#include <stdint.h>

struct ToriAuxLibVM;
struct StaticUIComponent;
struct RSCacheShared_FileListDat;
struct CS1VM;
struct CS1Host;
struct VarPVarBitManager;

struct ToriAuxLibVM*
ToriAuxLibVM_New(void);

void
ToriAuxLibVM_Free(struct ToriAuxLibVM* vm);

int
ToriAuxLibVM_EvalScript(
    struct ToriAuxLibVM* vm,
    struct StaticUIComponent const* c,
    int script_id);

bool
ToriAuxLibVM_IsActive(
    struct ToriAuxLibVM* vm,
    struct CS1Host const* cs1host,
    struct StaticUIComponent const* c);

int
ToriAuxLibVM_GetVarp(
    struct ToriAuxLibVM* vm,
    int id);

int
ToriAuxLibVM_GetVarbit(
    struct ToriAuxLibVM* vm,
    int id);

void
ToriAuxLibVM_SetVarpOptimistic(
    struct ToriAuxLibVM* vm,
    int id,
    int value);

bool
ToriAuxLibVM_LoadConfig(
    struct ToriAuxLibVM* vm,
    struct RSCacheShared_FileListDat* config_jagfile);

void
ToriAuxLibVM_ApplyButtonClickOptimistic(
    struct ToriAuxLibVM* vm,
    struct StaticUIComponent const* c);

struct CS1VM*
ToriAuxLibVM_CS1VM(struct ToriAuxLibVM* vm);

struct VarPVarBitManager*
ToriAuxLibVM_VarPVarBit(struct ToriAuxLibVM* vm);

#endif
