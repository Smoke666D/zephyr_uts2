#ifndef SYSTEM_DATA_BUS_H_
#define SYSTEM_DATA_BUS_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h> /* Дает доступ к типу struct zbus_observer */
#include "global_params.h"


/* Структура элемента медиатора */
struct mediator_element {
    PARAM_ID id;
    const struct zbus_channel *zbus_chan; /* Ссылка на нативный Zbus-канал */
};


/* Хелперы конкатенации */
#define PARAM_CONCAT_HELP(a, b) a ## b
#define PARAM_CONCAT(a, b) PARAM_CONCAT_HELP(a, b)

/* 
 * МАКРОС РЕГИСТРАЦИИ ЭЛЕМЕНТА МЕДИАТОРА В ДРАЙВЕРЕ.
 * Складывает структуру связи во Flash-память в секцию "mediator_elements".
 */
#define MEDIATOR_ELEMENT_REGISTER(_id, _zbus_chan) \
    static const struct mediator_element \
    PARAM_CONCAT(med_elem_, PARAM_CONCAT(_id, PARAM_CONCAT(_, __LINE__))) \
    __attribute__((section("mediator_elements"))) __attribute__((used)) = { \
        .id = _id, \
        .zbus_chan = _zbus_chan, \
    }

/* --- ЕДИНЫЙ И ЧИСТЫЙ API СЕРВЕРА ПАРАМЕТРОВ --- */

/**
 * @brief Чтение текущего значения параметра (Синхронный GET).
 * @return 0 при успехе, -ENOTSUP если параметр отсутствует.
 */
int mediator_get(PARAM_ID id, PARAM_VAL *val);

/**
 * @brief Запись значения параметра (Синхронный SET).
 * @return 0 при успехе, -ENOTSUP если параметр отсутствует.
 */
int mediator_set(PARAM_ID id, PARAM_VAL val);

/**
 * @brief Динамическая подписка на событие обновления параметра.
 * @param id ID параметра (например, AIN_AO1, AIN_DA11_test1).
 * @param obs Указатель на зарегистрированного в приложении подписчика (Zbus Observer).
 * @return 0 при успехе, -ENOTSUP если параметр отсутствует.
 */
int param_subscribe(PARAM_ID id, const struct zbus_observer *obs);


/**
 * @brief Ожидание обновления любого из параметров, на которые подписан observer.
 * 
 * @param[in]  obs     Указатель на подписчика (observer).
 * @param[out] id      Указатель на переменную, куда запишется ID изменившегося параметра.
 * @param[out] val     Указатель на структуру, куда запишется новое значение.
 * @param[in]  timeout Время ожидания (например, K_FOREVER или K_MSEC(500)).
 * 
 * @return 0 в случае успеха, иначе отрицательный код ошибки.
 */
int param_wait(const struct zbus_observer *obs, PARAM_ID *id, PARAM_VAL *val, k_timeout_t timeout);

#endif /* SYSTEM_DATA_BUS_H_ */