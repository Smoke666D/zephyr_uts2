#include "mediator.hpp"
#include "global_params.h"

extern "C" {
    extern struct zbus_channel _zbus_channel_list_start[];
    extern struct zbus_channel _zbus_channel_list_end[];
}


static void fill_mediator_cache(const struct zbus_channel** cache_array) {
    const struct zbus_channel *chan;

    // Макрос развернется в обращение к глобальным _zbus_channel_list_start/end
    STRUCT_SECTION_FOREACH(zbus_channel, chan) {
        if (chan->user_data != nullptr) {
            PARAM_ID id = *(static_cast<PARAM_ID*>(chan->user_data));
            if (id < PARAM_TOTAL_COUNT) {
                cache_array[id] = chan;
            }
        }
    }
}

namespace os {

// Определение массива в DTCM
const struct zbus_channel* mediator_registry::cache[PARAM_TOTAL_COUNT] __attribute__((section(".dtcm_bss"), used));

void mediator_registry::init_cache() {
    // Обнуляем
    for (int i = 0; i < PARAM_TOTAL_COUNT; i++) {
        cache[i] = nullptr;
    }

    // Вызываем глобальную функцию
    fill_mediator_cache(cache);
}

} // namespace os

// 3. Автоматический запуск
static int mediator_auto_init(void) {
    os::mediator_registry::init_cache();
    return 0;
}

SYS_INIT(mediator_auto_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);



