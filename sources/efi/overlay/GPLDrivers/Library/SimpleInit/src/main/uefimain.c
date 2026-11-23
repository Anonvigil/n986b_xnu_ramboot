/*
 *
 * Copyright (C) 2021 BigfootACA <bigfoot@classfun.cn>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 */

#ifdef ENABLE_UEFI
#include<Library/PcdLib.h>
#include<Library/DebugLib.h>
#include<Library/UefiBootManagerLib.h>
#include<Library/ReportStatusCodeLib.h>
#if defined (N986B_DXE_PROBE)
#include<Library/BaseLib.h>
#include<Library/CacheMaintenanceLib.h>
#endif
#ifdef ENABLE_LUA
#include"xlua.h"
#endif
#include"boot.h"
#include"confd.h"
#include"errno.h"
#include"logger.h"
#include"setjmp.h"
#include"language.h"
#include"gui.h"
#include"gui/splash.h"
#define TAG "main"

#if defined (N986B_DXE_PROBE)
static void trace_n986b_app_value(UINTN offset,UINT64 value){
	volatile UINT64 *slot=(volatile UINT64 *)(UINTN)(0x95E00000ULL+offset);
	*slot=value;
	WriteBackDataCacheRange((VOID *)(UINTN)slot,sizeof(*slot));
	MemoryFence();
}
#define TRACE_N986B_APP_STAGE(stage) trace_n986b_app_value(0x3D0,(stage))
#else
#define TRACE_N986B_APP_STAGE(stage)
#define trace_n986b_app_value(offset,value)
#endif

int main_retval=0;
jmp_buf main_exit;
extern int bootmenu_draw();

static void show_bootmenu(void*d __attribute__((unused))){
	bootmenu_draw();
}

static int post_main(){
	int r;
	TRACE_N986B_APP_STAGE(0xF610);
	logger_set_console(PcdGetBool(PcdLoggerdUseConsole));
	confd_init();
	logger_init();
	tlog_notice("initialize simple-init");
	tlog_debug("simple init main entry point at %p",&UefiMain);
	TRACE_N986B_APP_STAGE(0xF611);
	if((r=gui_pre_init())!=0)return r;
	TRACE_N986B_APP_STAGE(0xF612);
	if((r=gui_screen_init())!=0)return r;
	TRACE_N986B_APP_STAGE(0xF613);
	gui_splash_draw();
	lv_task_handler();

	gui_splash_set_text(true,_("Initializing config store..."));
	confd_dump(LEVEL_VERBOSE);

	gui_splash_set_text(true,_("Loading extra UEFI Drivers..."));
	boot_load_drivers();

	gui_splash_set_text(true,_("Initializing I18N locale..."));
	char*lang=confd_get_string("language",NULL);
	if(lang)lang_set(lang);
	lang_init_locale();

	gui_splash_set_text(true,_("Initializing Boot Manager..."));
	boot_init_configs();

	#ifdef ENABLE_LUA
	gui_splash_set_text(true,_("Initializing LUA Framework..."));
	lua_State*L=xlua_init();
	if(L){
		xlua_run_confd(L,TAG,"lua.on_post_startup");
		lua_close(L);
	}
	#endif

	gui_splash_set_text(true,_("Starting boot menu..."));
	gui_splash_exit(true,show_bootmenu);
	TRACE_N986B_APP_STAGE(0xF614);
	if((r=gui_main())!=0)return r;
	TRACE_N986B_APP_STAGE(0xF615);
	return 0;
}

// simple-init uefi entry point
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ih,IN EFI_SYSTEM_TABLE*st){
	TRACE_N986B_APP_STAGE(0xF600);
	REPORT_STATUS_CODE(EFI_PROGRESS_CODE,(EFI_SOFTWARE_DXE_BS_DRIVER|EFI_SW_PC_USER_SETUP));
	TRACE_N986B_APP_STAGE(0xF601);

	TRACE_N986B_APP_STAGE(0xF602);
	EfiBootManagerConnectAll();
	TRACE_N986B_APP_STAGE(0xF603);
	TRACE_N986B_APP_STAGE(0xF604);
	EfiBootManagerRefreshAllBootOption();
	TRACE_N986B_APP_STAGE(0xF605);
	DEBUG((EFI_D_INFO,"Initialize SimpleInit GUI...\n"));

	errno=0;
	main_retval=0;
	if(setjmp(main_exit)==0){
		TRACE_N986B_APP_STAGE(0xF606);
		main_retval=post_main();
		TRACE_N986B_APP_STAGE(0xF607);
	}
	trace_n986b_app_value(0x3D8,(UINT64)main_retval);
	TRACE_N986B_APP_STAGE(0xF608);
	return main_retval;
}
#endif
