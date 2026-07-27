#pragma once

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include "os.h" 

// Здесь должен быть ваш файл с перечислением PARAM_ID
#include "global_params.h" 




namespace os {

/**
 * @brief Интерфейс элемента медиатора.
 * Оборачивает Zbus канал в привычные методы .get() / .set().
 */
template<typename T>
class message_t {
protected:
    const struct zbus_channel* chan_;

public:
    using type = T;

    constexpr message_t(const struct zbus_channel* chan = nullptr) : chan_(chan) {}

    // Чтение значения из шины
    const T get() const {
        T val;
        if (chan_ == nullptr) return T{};
        zbus_chan_read(chan_, &val, K_NO_WAIT);
        return val;
    }

    // Публикация значения в шину
    void set(const T& _val) {
        if (chan_ != nullptr) {
            zbus_chan_pub(chan_, &_val, K_NO_WAIT);
        }
    }

    // Запись только если значение изменилось (Dirty check)
    bool set_if_neq(const T& _val) {
        T tmp = get();
        if (memcmp(&tmp, &_val, sizeof(T)) != 0) {
            set(_val);
            return true;
        }
        return false;
    }

    void cmd(const T& _val) { set(_val); }
    
    const struct zbus_channel* get_chan() const { return chan_; }
    bool is_valid() const { return chan_ != nullptr; }
};

/**
 * @brief Глобальный реестр медиатора.
 */
class mediator {
private:
    // Массив указателей на каналы. Размер берется из вашего Enum.
    static const struct zbus_channel* cache[PARAM_TOTAL_COUNT];

public:
    /**
     * @brief Инициализация кэша. Находит все каналы с PARAM_ID в user_data.
     */
    static void init_cache();

    /**
     * @brief Доступ к любому параметру по ID из любой точки проекта.
     */
    template<typename T>
    static message_t<T> access(PARAM_ID id) {
        if (id >= PARAM_TOTAL_COUNT || cache[id] == nullptr) {
            return message_t<T>(nullptr);
        }
        return message_t<T>(cache[id]);
    }
};


/**
 * @brief Глобальный реестр медиатора
 */
class mediator_registry {
public:
    // Массив кэша
    static const struct zbus_channel* cache[PARAM_TOTAL_COUNT];
    
    static void init_cache();

    template<typename T>
    static message_t<T> access(PARAM_ID id) {
        if (id >= PARAM_TOTAL_COUNT) return message_t<T>(nullptr);
        return message_t<T>(cache[id]);
    }
};

// --- САХАР №3: Короткая функция доступа ---
// По умолчанию возвращает обертку для PARAM_VAL
template<typename T = PARAM_VAL>
inline message_t<T> mediator(PARAM_ID id) {
    return mediator_registry::access<T>(id);
}


}

/**
 * @brief Супер-макрос для создания элемента медиатора в модуле.
 * @param Type - тип данных (struct/int/...)
 * @param Name - имя переменной-объекта в текущем файле
 * @param IdValue - PARAM_ID из enum
 * @param InitVal - начальное значение
 */
#define MEDIATOR_ELEMENT_DEFINE(Type, Name, IdValue, InitVal) \
    static PARAM_ID Name##_id_meta = IdValue; \
    ZBUS_CHAN_DEFINE(Name##_chan, \
                     Type, \
                     NULL, \
                     &Name##_id_meta, \
                     ZBUS_OBSERVERS_EMPTY, \
                     ZBUS_MSG_INIT(InitVal)); \
    os::message_t<Type> Name(&Name##_chan)









/**
 * @brief Типизированный подписчик-очередь.
 */
template<typename T, uint32_t _queue_count>
class subscriber_queue {
private:
    struct k_msgq msgq_;
    uint8_t buffer[_queue_count * sizeof(T)];

public:
    subscriber_queue() {
        k_msgq_init(&msgq_, (char*)buffer, sizeof(T), _queue_count);
    }

    // Сюда Zbus будет "заталкивать" данные
    void push(const T& val) {
        k_msgq_put(&msgq_, &val, K_NO_WAIT);
    }

    os::rc receive(T& out_val, uint32_t timeout = os::infinitely) {
        int res = k_msgq_get(&msgq_, &out_val, 
                             (timeout == os::infinitely) ? K_FOREVER : K_MSEC(timeout));
        return (res == 0) ? os::rc::ok : os::rc::timeout;
    }
};




