package main

/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#include <stdlib.h>
#include "yoyo/plugin.h"

void yoyo_register_go_callbacks(const yoyo_api* api);
*/
import "C"

import (
	"fmt"
	"unsafe"
)

//export go_yoyo_event
func go_yoyo_event(user unsafe.Pointer, typ C.int, event unsafe.Pointer) {
	if event == nil {
		return
	}
	switch int(typ) {
	case int(C.YOYO_EVENT_PRE_JOIN):
		e := (*C.yoyo_pre_join_event)(event)
		fmt.Printf("[sample_go] pre_join name=%s xuid=%s\n", cstr(e.player.name), cstr(e.player.xuid))
	case int(C.YOYO_EVENT_JOIN):
		e := (*C.yoyo_join_event)(event)
		fmt.Printf("[sample_go] join name=%s player_id=%d\n", cstr(e.player.name), uint64(e.player_id))
	case int(C.YOYO_EVENT_CHAT):
		e := (*C.yoyo_chat_event)(event)
		fmt.Printf("[sample_go] chat name=%s message=%s\n", cstr(e.player.name), cstr(e.message))
	case int(C.YOYO_EVENT_LEFT):
		e := (*C.yoyo_left_event)(event)
		fmt.Printf("[sample_go] left name=%s reason=%s\n", cstr(e.player.name), cstr(e.reason))
	}
}

func cstr(p *C.char) string {
	if p == nil {
		return ""
	}
	return C.GoString(p)
}

//export yoyo_plugin_entry
func yoyo_plugin_entry(api *C.yoyo_api) *C.yoyo_api {
	if api == nil || uint32(api.abi_version) != 1 {
		return nil
	}
	C.yoyo_register_go_callbacks(api)
	message := C.CString("sample_go plugin loaded")
	defer C.free(unsafe.Pointer(message))
	api.log(0, message)
	return api
}

func main() {}
