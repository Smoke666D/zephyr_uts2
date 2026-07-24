#include "mediator.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sys_bus, LOG_LEVEL_INF);

/* Описание маршрута параметра */
struct param_route {
    PARAM_ID id;
    const struct zbus_channel *zbus_chan;
};

/* Извлекаем указатели границ секций из линкера */
extern const struct mediator_element __start_mediator_elements[];
extern const struct mediator_element __stop_mediator_elements[];

/* 
 * КЭШ-ТАБЛИЦА СВЕРХБЫСТРОГО ДОСТУПА В ОЗУ.
 * Заполняется автоматически при старте ОС на основе данных линкера.
 */
static const struct zbus_channel *mediator_cache[PARAM_TOTAL_COUNT] __dtcm_bss_section;

/* Функция построения кэш-таблицы */
static int mediator_cache_init(void)
{
    size_t count = __stop_mediator_elements - __start_mediator_elements;

    // Очищаем таблицу нулями (по умолчанию все параметры - NULL / не поддерживаются)
    memset(mediator_cache, 0, sizeof(mediator_cache));

    // Пробегаем по ROM-таблице линкера и заполняем кэш-таблицу
    for (size_t i = 0; i < count; i++) {
        const struct mediator_element *elem = &__start_mediator_elements[i];
        if (elem->id < PARAM_TOTAL_COUNT) {
            mediator_cache[elem->id] = elem->zbus_chan;
        }
    }
    LOG_INF("Mediator cache table compiled successfully. Active routes: %u", (unsigned int)count);
    return 0;
}

/* Регистрируем автозапуск построения кэш-таблицы на этапе POST_KERNEL до старта приложений */
SYS_INIT(mediator_cache_init, POST_KERNEL, 15);

/* Унифицированный метод чтения параметров (GET) */
int mediator_get(PARAM_ID id, PARAM_VAL *val)
{
    if (id >= PARAM_TOTAL_COUNT|| val == NULL) {
        return -EINVAL;
    }

    /* Извлекаем указатель на нативный Zbus-канал из кэш-таблицы за 1 такт! */
    const struct zbus_channel *chan = mediator_cache[id];
    if (chan == NULL) {
        return -ENOTSUP; /* Параметр не реализован на данной плате / ревизии */
    }

    return zbus_chan_read(chan, &val->value, K_MSEC(10));
}

/* Унифицированный метод записи параметров (SET) */
int mediator_set(PARAM_ID id, PARAM_VAL val)
{
    if (id >= PARAM_TOTAL_COUNT) {
        return -EINVAL;
    }

    const struct zbus_channel *chan = mediator_cache[id];
    if (chan == NULL) {
        return -ENOTSUP;
    }

    return zbus_chan_pub(chan, &val.value, K_MSEC(10));
}

/* Унифицированный метод динамической подписки в рантайме */
int param_subscribe(PARAM_ID id, const struct zbus_observer *obs)
{
    if (id >= PARAM_TOTAL_COUNT || obs == NULL) {
        return -EINVAL;
    }

    const struct zbus_channel *chan = mediator_cache[id];
    if (chan == NULL) {
        return -ENOTSUP;
    }

    return zbus_chan_add_obs(chan, obs, K_MSEC(100));
}

PARAM_ID param_get_id_by_channel(const struct zbus_channel *chan)
{
    if (chan == NULL) {
        return PARAM_TOTAL_COUNT; /* Возвращаем невалидный ID */
    }

    // Ищем указатель в кэше
    for (size_t i = 0; i < PARAM_TOTAL_COUNT; i++) {
        if (mediator_cache[i] == chan) {
            return (PARAM_ID)i;
        }
    }

    return PARAM_TOTAL_COUNT; /* Не нашли совпадений */
}


int param_wait(const struct zbus_observer *obs, PARAM_ID *id, PARAM_VAL *val, k_timeout_t timeout)
{
    if (obs == NULL || id == NULL || val == NULL) {
        return -EINVAL;
    }

    const struct zbus_channel *chan = NULL;

    /* 1. Ждем уведомления от zbus */
    int err = zbus_sub_wait(obs, &chan, timeout);
    if (err != 0) {
        return err; /* Возвращаем ошибку таймаута или шины */
    }

    /* 2. Определяем ID параметра по указателю на канал */
    *id = param_get_id_by_channel(chan);
    if (*id >= PARAM_TOTAL_COUNT) {
        return -ENODEV; /* Канал не зарегистрирован в медиаторе */
    }

    /* 3. Читаем актуальное значение из канала */
    return param_get(*id, val);
}