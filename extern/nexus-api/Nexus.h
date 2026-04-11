///----------------------------------------------------------------------------------------------------
/// Copyright (c) Raidcore.GG - Licensed under the MIT license.
///
/// Name         :  Nexus.h
/// Description  :  C/C++ Definitions header for Nexus API.
/// Authors      :  K. Bieniek
///----------------------------------------------------------------------------------------------------

#ifndef NEXUS_H
#define NEXUS_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#include <stdbool.h>
#endif
#include <windows.h>

#define NEXUS_API_VERSION 6

/* DataLink identifiers */
#define DL_NEXUS_LINK "DL_NEXUS_LINK"
#ifndef DL_MUMBLE_LINK
#define DL_MUMBLE_LINK "DL_MUMBLE_LINK"
#endif
#ifndef DL_MUMBLE_LINK_IDENTITY
#define DL_MUMBLE_LINK_IDENTITY "DL_MUMBLE_LINK_IDENTITY"
#endif

/* Event identifiers */
#define EV_WINDOW_RESIZED          "EV_WINDOW_RESIZED"
#define EV_MUMBLE_IDENTITY_UPDATED "EV_MUMBLE_IDENTITY_UPDATED"
#define EV_ADDON_LOADED            "EV_ADDON_LOADED"
#define EV_ADDON_UNLOADED          "EV_ADDON_UNLOADED"

typedef enum ERenderType
{
	RT_PreRender,
	RT_Render,
	RT_PostRender,
	RT_OptionsRender
} ERenderType;

typedef enum EMHStatus
{
	MH_UNKNOWN = -1,
	MH_OK = 0,
	MH_ERROR_ALREADY_INITIALIZED,
	MH_ERROR_NOT_INITIALIZED,
	MH_ERROR_ALREADY_CREATED,
	MH_ERROR_NOT_CREATED,
	MH_ERROR_ENABLED,
	MH_ERROR_DISABLED,
	MH_ERROR_NOT_EXECUTABLE,
	MH_ERROR_UNSUPPORTED_FUNCTION,
	MH_ERROR_MEMORY_ALLOC,
	MH_ERROR_MEMORY_PROTECT,
	MH_ERROR_MODULE_NOT_FOUND,
	MH_ERROR_FUNCTION_NOT_FOUND
} EMHStatus;

typedef enum ELogLevel
{
	LOGL_CRITICAL = 1,
	LOGL_WARNING = 2,
	LOGL_INFO = 3,
	LOGL_DEBUG = 4,
	LOGL_TRACE = 5
} ELogLevel;

typedef enum EAddonFlags
{
	AF_None              = 0,
	AF_IsVolatile        = 1 << 0,
	AF_DisableHotloading = 1 << 1,
	AF_LaunchOnly        = 1 << 2
} EAddonFlags;

typedef enum EUpdateProvider
{
	UP_None     = 0,
	UP_Raidcore = 1,
	UP_GitHub   = 2,
	UP_Direct   = 3,
	UP_Self     = 4
} EUpdateProvider;

typedef struct Keybind_t
{
	uint16_t Key;
	bool     Alt;
	bool     Ctrl;
	bool     Shift;
} Keybind_t;

typedef struct Texture_t
{
	uint32_t Width;
	uint32_t Height;
	void*    Resource;
} Texture_t;

typedef struct NexusLinkData_t
{
	uint32_t Width;
	uint32_t Height;
	float    Scaling;
	bool     IsMoving;
	bool     IsCameraMoving;
	bool     IsGameplay;
	void*    Font;
	void*    FontBig;
	void*    FontUI;
} NexusLinkData_t;

typedef struct AddonVersion_t
{
	uint16_t Major;
	uint16_t Minor;
	uint16_t Build;
	uint16_t Revision;
} AddonVersion_t;

struct AddonAPI_t;

typedef void        (*ADDON_LOAD)                       (AddonAPI_t* aAPI);
typedef void        (*ADDON_UNLOAD)                     ();
typedef void        (*GUI_RENDER)                       ();
typedef void        (*GUI_ADDRENDER)                    (ERenderType aRenderType, GUI_RENDER aRenderCallback);
typedef void        (*GUI_REMRENDER)                    (GUI_RENDER aRenderCallback);
typedef void        (*GUI_REGISTERCLOSEONESCAPE)        (const char* aWindowName, bool* aIsVisible);
typedef void        (*GUI_DEREGISTERCLOSEONESCAPE)      (const char* aWindowName);
typedef void        (*UPDATER_REQUESTUPDATE)            (uint32_t aSignature, const char* aUpdateURL);
typedef const char* (*PATHS_GETGAMEDIR)                 ();
typedef const char* (*PATHS_GETADDONDIR)                (const char* aName);
typedef const char* (*PATHS_GETCOMMONDIR)               ();
typedef EMHStatus   (__stdcall* MINHOOK_CREATE)         (LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
typedef EMHStatus   (__stdcall* MINHOOK_REMOVE)         (LPVOID pTarget);
typedef EMHStatus   (__stdcall* MINHOOK_ENABLE)         (LPVOID pTarget);
typedef EMHStatus   (__stdcall* MINHOOK_DISABLE)        (LPVOID pTarget);
typedef void        (*LOGGER_LOG)                       (ELogLevel aLogLevel, const char* aChannel, const char* aStr);
typedef void        (*ALERTS_NOTIFY)                    (const char* aMessage);
typedef void        (*EVENT_CONSUME)                    (void* aEventArgs);
typedef void        (*EVENTS_RAISE)                     (const char* aIdentifier, void* aEventData);
typedef void        (*EVENTS_RAISENOTIFICATION)         (const char* aIdentifier);
typedef void        (*EVENTS_RAISE_TARGETED)            (uint32_t aSignature, const char* aIdentifier, void* aEventData);
typedef void        (*EVENTS_RAISENOTIFICATION_TARGETED)(uint32_t aSignature, const char* aIdentifier);
typedef void        (*EVENTS_SUBSCRIBE)                 (const char* aIdentifier, EVENT_CONSUME aConsumeEventCallback);
typedef UINT        (*WNDPROC_CALLBACK)                 (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
typedef void        (*WNDPROC_ADDREM)                   (WNDPROC_CALLBACK aWndProcCallback);
typedef LRESULT     (*WNDPROC_SENDTOGAME)               (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
typedef void        (*INPUTBINDS_PROCESS)               (const char* aIdentifier, bool aIsRelease);
typedef void        (*INPUTBINDS_INVOKE)                (const char* aIdentifier, bool aIsRelease);
typedef void        (*INPUTBINDS_REGISTERWITHSTRING)    (const char* aIdentifier, INPUTBINDS_PROCESS aKeybindHandler, const char* aKeybind);
typedef void        (*INPUTBINDS_REGISTERWITHSTRUCT)    (const char* aIdentifier, INPUTBINDS_PROCESS aKeybindHandler, Keybind_t aKeybind);
typedef void        (*INPUTBINDS_DEREGISTER)            (const char* aIdentifier);
typedef void*       (*DATALINK_GET)                     (const char* aIdentifier);
typedef void*       (*DATALINK_SHARE)                   (const char* aIdentifier, uint64_t aResourceSize);
typedef void        (*TEXTURES_RECEIVECALLBACK)         (const char* aIdentifier, Texture_t* aTexture);
typedef Texture_t*  (*TEXTURES_GET)                     (const char* aIdentifier);
typedef Texture_t*  (*TEXTURES_GETORCREATEFROMFILE)     (const char* aIdentifier, const char* aFilename);
typedef Texture_t*  (*TEXTURES_GETORCREATEFROMRESOURCE) (const char* aIdentifier, uint32_t aResourceID, HMODULE aModule);
typedef Texture_t*  (*TEXTURES_GETORCREATEFROMURL)      (const char* aIdentifier, const char* aRemote, const char* aEndpoint);
typedef Texture_t*  (*TEXTURES_GETORCREATEFROMMEMORY)   (const char* aIdentifier, void* aData, uint64_t aSize);
typedef void        (*TEXTURES_LOADFROMFILE)            (const char* aIdentifier, const char* aFilename, TEXTURES_RECEIVECALLBACK aCallback);
typedef void        (*TEXTURES_LOADFROMRESOURCE)        (const char* aIdentifier, uint32_t aResourceID, HMODULE aModule, TEXTURES_RECEIVECALLBACK aCallback);
typedef void        (*TEXTURES_LOADFROMURL)             (const char* aIdentifier, const char* aRemote, const char* aEndpoint, TEXTURES_RECEIVECALLBACK aCallback);
typedef void        (*TEXTURES_LOADFROMMEMORY)          (const char* aIdentifier, void* aData, uint64_t aSize, TEXTURES_RECEIVECALLBACK aCallback);
typedef void        (*QUICKACCESS_ADDSHORTCUT)          (const char* aIdentifier, const char* aTextureIdentifier, const char* aTextureHoverIdentifier, const char* aKeybindIdentifier, const char* aTooltipText);
typedef void        (*QUICKACCESS_ADDSIMPLE)            (const char* aIdentifier, const char* aTargetShortcutIdentifier, GUI_RENDER aShortcutRenderCallback);
typedef void        (*QUICKACCESS_GENERIC)              (const char* aIdentifier);
typedef const char* (*LOCALIZATION_TRANSLATE)           (const char* aIdentifier);
typedef const char* (*LOCALIZATION_TRANSLATETO)         (const char* aIdentifier, const char* aLanguageIdentifier);
typedef void        (*LOCALIZATION_SET)                 (const char* aIdentifier, const char* aLanguageIdentifier, const char* aString);
typedef void        (*FONTS_RECEIVECALLBACK)            (const char* aIdentifier, void* aFont);
typedef void        (*FONTS_GETRELEASE)                 (const char* aIdentifier, FONTS_RECEIVECALLBACK aCallback);
typedef void        (*FONTS_ADDFROMFILE)                (const char* aIdentifier, float aFontSize, const char* aFilename, FONTS_RECEIVECALLBACK aCallback, void* aConfig);
typedef void        (*FONTS_ADDFROMRESOURCE)            (const char* aIdentifier, float aFontSize, uint32_t aResourceID, HMODULE aModule, FONTS_RECEIVECALLBACK aCallback, void* aConfig);
typedef void        (*FONTS_ADDFROMMEMORY)              (const char* aIdentifier, float aFontSize, void* aData, uint64_t aSize, FONTS_RECEIVECALLBACK aCallback, void* aConfig);
typedef void        (*FONTS_RESIZE)                     (const char* aIdentifier, float aFontSize);

typedef struct AddonDefinition_t
{
	uint32_t        Signature;
	uint32_t        APIVersion;
	const char*     Name;
	AddonVersion_t  Version;
	const char*     Author;
	const char*     Description;
	ADDON_LOAD      Load;
	ADDON_UNLOAD    Unload;
	EAddonFlags     Flags;
	EUpdateProvider Provider;
	const char*     UpdateLink;
} AddonDefinition_t;

typedef struct AddonAPI_t
{
	void* SwapChain;
	void* ImguiContext;
	void* ImguiMalloc;
	void* ImguiFree;

	GUI_ADDRENDER                     GUI_Register;
	GUI_REMRENDER                     GUI_Deregister;
	UPDATER_REQUESTUPDATE             RequestUpdate;
	LOGGER_LOG                        Log;
	ALERTS_NOTIFY                     GUI_SendAlert;
	GUI_REGISTERCLOSEONESCAPE         GUI_RegisterCloseOnEscape;
	GUI_DEREGISTERCLOSEONESCAPE       GUI_DeregisterCloseOnEscape;
	PATHS_GETGAMEDIR                  Paths_GetGameDirectory;
	PATHS_GETADDONDIR                 Paths_GetAddonDirectory;
	PATHS_GETCOMMONDIR                Paths_GetCommonDirectory;
	MINHOOK_CREATE                    MinHook_Create;
	MINHOOK_REMOVE                    MinHook_Remove;
	MINHOOK_ENABLE                    MinHook_Enable;
	MINHOOK_DISABLE                   MinHook_Disable;
	EVENTS_RAISE                      Events_Raise;
	EVENTS_RAISENOTIFICATION          Events_RaiseNotification;
	EVENTS_RAISE_TARGETED             Events_RaiseTargeted;
	EVENTS_RAISENOTIFICATION_TARGETED Events_RaiseNotificationTargeted;
	EVENTS_SUBSCRIBE                  Events_Subscribe;
	EVENTS_SUBSCRIBE                  Events_Unsubscribe;
	WNDPROC_ADDREM                    WndProc_Register;
	WNDPROC_ADDREM                    WndProc_Deregister;
	WNDPROC_SENDTOGAME                WndProc_SendToGameOnly;
	INPUTBINDS_INVOKE                 InputBinds_Invoke;
	INPUTBINDS_REGISTERWITHSTRING     InputBinds_RegisterWithString;
	INPUTBINDS_REGISTERWITHSTRUCT     InputBinds_RegisterWithStruct;
	INPUTBINDS_DEREGISTER             InputBinds_Deregister;
	DATALINK_GET                      DataLink_Get;
	DATALINK_SHARE                    DataLink_Share;
	TEXTURES_GET                      Textures_Get;
	TEXTURES_GETORCREATEFROMFILE      Textures_GetOrCreateFromFile;
	TEXTURES_GETORCREATEFROMRESOURCE  Textures_GetOrCreateFromResource;
	TEXTURES_GETORCREATEFROMURL       Textures_GetOrCreateFromURL;
	TEXTURES_GETORCREATEFROMMEMORY    Textures_GetOrCreateFromMemory;
	TEXTURES_LOADFROMFILE             Textures_LoadFromFile;
	TEXTURES_LOADFROMRESOURCE         Textures_LoadFromResource;
	TEXTURES_LOADFROMURL              Textures_LoadFromURL;
	TEXTURES_LOADFROMMEMORY           Textures_LoadFromMemory;
	QUICKACCESS_ADDSHORTCUT           QuickAccess_Add;
	QUICKACCESS_GENERIC               QuickAccess_Remove;
	QUICKACCESS_GENERIC               QuickAccess_Notify;
	QUICKACCESS_ADDSIMPLE             QuickAccess_AddContextMenu;
	QUICKACCESS_GENERIC               QuickAccess_RemoveContextMenu;
	LOCALIZATION_TRANSLATE            Localization_Translate;
	LOCALIZATION_TRANSLATETO          Localization_TranslateTo;
	LOCALIZATION_SET                  Localization_Set;
	FONTS_GETRELEASE                  Fonts_Get;
	FONTS_GETRELEASE                  Fonts_Release;
	FONTS_ADDFROMFILE                 Fonts_AddFromFile;
	FONTS_ADDFROMRESOURCE             Fonts_AddFromResource;
	FONTS_ADDFROMMEMORY               Fonts_AddFromMemory;
	FONTS_RESIZE                      Fonts_Resize;
} AddonAPI_t;

#endif
