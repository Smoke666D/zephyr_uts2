#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "Lua/lua.h"
#include "Lua/lauxlib.h"
#include "Lua/lualib.h"
#include "led.h"


LOG_MODULE_REGISTER(lua_thread_mod, LOG_LEVEL_INF);



/* Массив с программой на Lua (мигание раз в секунду) */
static const char lua_blink_program[] = 
    "print('Lua environment active!')\n"
    "local state = 0\n"
    "while true do\n"
    "    state = 1 - state\n"
    "    led_set(state)\n"
    "    msleep(1000)\n"
    "end\n";

static const char *current_script = lua_blink_program;

static int l_led_set(lua_State *L) {
    int state = luaL_checkinteger(L, 1);

      led_manager_set_states_sync(false, false, state == 1, K_MSEC(100));

    return 0;
}

static int l_sleep(lua_State *L) {
    int ms = luaL_checkinteger(L, 1);
    k_msleep(ms);
    return 0;
}

#define LUA_STACK_SIZE 8048
#define LUA_PRIORITY   10

static void lua_thread_entry(void *p1, void *p2, void *p3)
 {
    LOG_INF("Lua thread routine started.");


    lua_State *L = luaL_newstate();
    if (!L) {
        LOG_ERR("Failed to create Lua state");
        return;
    }

    luaL_openlibs(L);

    lua_register(L, "led_set", l_led_set);
    lua_register(L, "msleep", l_sleep);

    if (current_script) {
        if (luaL_dostring(L, current_script) != LUA_OK) {
            // ВАЖНО: используем LOG_ERR вместо printf/fprintf!
            LOG_ERR("Lua error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    lua_close(L);
}

//K_THREAD_DEFINE(lua_thread_handle, LUA_STACK_SIZE, lua_thread_entry,
 //             NULL, NULL, NULL, LUA_PRIORITY, 0, 0);