
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>


// Zephyr includes
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace os {

// --- Вспомогательные типы ---

enum class rc : int {
    ok          = 0,
    timeout     = -EAGAIN,
    overflow    = -ENOMEM,
    wcontext    = -EPERM,
    wstate      = -EBUSY,
    wparam      = -EINVAL,
    illegal_use = -EACCES,
    invalid_obj = -EINVAL,
    deleted     = -ESHUTDOWN,
    forced      = -ECANCELED,
    internal    = -EFAULT,
};

enum class priority : int8_t {
    base     = 10,
    realtime = 1,
    high     = 3,
    normal   = 7,
    low      = 12,
    idle     = 14,
};

enum wait : uint32_t {
    nowait     = 0,
    infinitely = 0xFFFFFFFF // Мапится на K_FOREVER
};

enum class opt : int8_t {
    nostart = 0,
    start   = 1,
};

enum repeat_timer : int8_t {
    norepeat = 0,
    repeat   = 1,
};

// Вспомогательная функция для конвертации времени
static inline k_timeout_t make_timeout(uint32_t t) {
    if (t == infinitely) return K_FOREVER;
    if (t == nowait) return K_NO_WAIT;
    return K_MSEC(t); // Предполагаем, что вход в мс. Если в тиках - K_TICKS(t)
}

// --- Задачи (Threads) ---

class task_base {
protected:
    struct k_thread task_;
    k_tid_t tid_ = nullptr;
    k_thread_stack_t* stack_ptr_ = nullptr;
    uint32_t stack_size_ = 0;

public:
    enum class state : int8_t {
        none, runnable, wait, suspend, waitsusp, dormant, err = -1
    };

    static rc sleep(const uint32_t _ms);
    static rc yield(void);
    static void exit(void);

    rc suspend(void);
    rc resume(void);
    rc terminate(void);
    state state_get(void);
    rc change_priority(const priority _priority);
    rc activate(void);
    
    virtual ~task_base();
};

template <class T, uint32_t _stack_size>
class task : public task_base {
    // В Zephyr стек должен быть выровнен. Используем массив с запасом.
    K_KERNEL_STACK_MEMBER(stack_mem, _stack_size);

    static void thread_entry_adapter(void* p1, void* p2, void* p3) {
        T* obj = static_cast<T*>(p1);
        obj->task_func();
    }

public:
    task(const char* _name, const priority _prio = priority::normal, const opt _opt = opt::start) {
        stack_ptr_ = stack_mem;
        stack_size_ = _stack_size;
        
        int flags = 0;
        if (_opt == opt::nostart) flags |= K_USER; // Просто пример, в Zephyr старт обычно сразу

        tid_ = k_thread_create(&task_, stack_mem, K_THREAD_STACK_SIZEOF(stack_mem),
                               thread_entry_adapter, this, nullptr, nullptr,
                               static_cast<int>(_prio), 0, 
                               (_opt == opt::start) ? K_NO_WAIT : K_FOREVER);
        if (_name) k_thread_name_set(tid_, _name);
    }
};

// --- Синхронизация ---

class mutex {
    struct k_mutex mutex_;
public:
    mutex() { k_mutex_init(&mutex_); }
    rc acquire(uint32_t timeout = infinitely);
    rc release();
    
    auto guard(uint32_t timeout = infinitely) {
        struct mutex_guard {
            mutex& m; rc res;
            mutex_guard(mutex& _m, uint32_t t) : m(_m), res(m.acquire(t)) {}
            ~mutex_guard() { if (res == rc::ok) m.release(); }
            operator bool() { return res == rc::ok; }
        };
        return mutex_guard(*this, timeout);
    }
};

class semaphore {
    struct k_sem sem_;
public:
    semaphore(uint32_t start, uint32_t max) { k_sem_init(&sem_, start, max); }
    rc acquire(uint32_t timeout = infinitely);
    rc release();
};

class eventgrp {
    struct k_event event_;
public:
    enum class wait_mode { w_or = 1, w_and = 2, w_or_clr = 5, w_and_clr = 6 };
    
    eventgrp(uint32_t pattern = 0) { 
        k_event_init(&event_); 
        if (pattern) k_event_set(&event_, pattern);
    }
    rc wait(uint32_t pattern, wait_mode mode, uint32_t timeout = infinitely, uint32_t* actual = nullptr);
    rc set(uint32_t pattern);
    rc clr(uint32_t pattern);
};

// --- Память (Slabs) ---

class fmem_base {
protected:
    struct k_mem_slab slab_;
public:
    fmem_base(void* buffer, uint32_t block_size, uint32_t count);
    rc acquire(void** data, uint32_t timeout = nowait);
    rc release(void* data);
};

template <class T, uint32_t cnt>
class fmem : public fmem_base {
    uint8_t __aligned(4) buffer[cnt * sizeof(T)];
public:
    fmem() : fmem_base(buffer, sizeof(T), cnt) {}
    rc acquire(T** data, uint32_t t = nowait) { return fmem_base::acquire((void**)data, t); }
};

// --- Очереди (Message Queues) ---

class queue_base {
    struct k_msgq msgq_;
public:
    queue_base(void* buffer, uint32_t msg_size, uint32_t msg_cnt);
    rc send(const void* data, uint32_t timeout = nowait);
    rc receive(void* data, uint32_t timeout = infinitely);
};

template <class T, uint32_t cnt>
class queue : public queue_base {
    uint8_t __aligned(4) buffer[cnt * sizeof(T)];
public:
    queue() : queue_base(buffer, sizeof(T), cnt) {}
    rc send(const T& data, uint32_t t = nowait) { return queue_base::send(&data, t); }
    rc receive(T& data, uint32_t t = infinitely) { return queue_base::receive(&data, t); }
};

// --- Таймеры ---

class timer_base {
protected:
    struct k_timer timer_;
    void (*func_)(void*);
    void* param_;
    bool repeat_;
    uint32_t period_;

    static void expiry_fn(struct k_timer *timer_id);
public:
    timer_base(void (*f)(void*), void* p, uint32_t timeout = 0, repeat_timer r = norepeat);
    rc start(uint32_t t);
    rc cancel();
};

} // namespace os