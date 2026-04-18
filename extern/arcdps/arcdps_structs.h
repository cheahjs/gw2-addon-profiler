#pragma once

#include <stdint.h>
#include <windows.h>

/* arcdps structs -- layout matches arcdps combatdemo.cpp exactly */

/* combat event */
typedef struct cbtevent {
    uint64_t time;
    uint64_t src_agent;
    uint64_t dst_agent;
    int32_t value;
    int32_t buff_dmg;
    uint32_t overstack_value;
    uint32_t skill_id;
    uint16_t src_instid;
    uint16_t dst_instid;
    uint16_t src_master_instid;
    uint16_t dst_master_instid;
    uint8_t iff;
    uint8_t buff;
    uint8_t result;
    uint8_t is_activation;
    uint8_t is_buffremove;
    uint8_t is_ninety;
    uint8_t is_fifty;
    uint8_t is_moving;
    uint8_t is_statechange;
    uint8_t is_flanking;
    uint8_t is_shields;
    uint8_t is_offcycle;
    uint8_t pad61;
    uint8_t pad62;
    uint8_t pad63;
    uint8_t pad64;
} cbtevent;

/* agent short */
typedef struct ag {
    const char* name;
    uintptr_t id;
    uint32_t prof;
    uint32_t elite;
    uint32_t self;
    uint16_t team;
} ag;

/* arcdps export table -- returned by addon's get_init_addr callback */
typedef struct arcdps_exports {
    uintptr_t size;
    uint32_t sig;
    uint32_t imguivers;
    const char* out_name;
    const char* out_build;
    void* wnd_nofilter;     /* UINT fn(HWND, UINT, WPARAM, LPARAM) */
    void* combat;           /* void fn(cbtevent*, ag*, ag*, const char*, uint64_t, uint64_t) */
    void* imgui;            /* void fn(uint32_t not_charsel_or_loading) */
    void* options_end;      /* void fn() */
    void* combat_local;     /* void fn(cbtevent*, ag*, ag*, const char*, uint64_t, uint64_t) */
    void* wnd_filter;       /* UINT fn(HWND, UINT, WPARAM, LPARAM) */
    void* options_windows;  /* void fn(const char*) */
} arcdps_exports;

/* callback typedefs */
typedef void (*combat_callback_t)(cbtevent* ev, ag* src, ag* dst, const char* skillname, uint64_t id, uint64_t revision);
typedef uint32_t (*wndproc_callback_t)(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
typedef void (*imgui_callback_t)(uint32_t not_charsel_or_loading);
typedef void (*options_callback_t)();
typedef void (*options_windows_callback_t)(const char* windowname);

/* mod_init signature -- returned by get_init_addr, called by arcdps to get exports */
typedef arcdps_exports* (*mod_init_t)();

/* get_init_addr signature -- returns a mod_init function pointer, NOT arcdps_exports* */
typedef mod_init_t (*get_init_addr_t)(const char*, void*, void*, HMODULE, void*, void*, uint32_t);

/* get_release_addr signature */
typedef void (*get_release_addr_t)();
