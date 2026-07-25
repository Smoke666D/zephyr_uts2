
#include "os.h"

namespace os {

// --- Task ---
rc task_base::sleep(const uint32_t _ms) {
    return (k_msleep(_ms) == 0) ? rc::ok : rc::internal;
}

rc task_base::yield() {
    k_yield();
    return rc::ok;
}

rc task_base::suspend() {
    k_thread_suspend(tid_);
    return rc::ok;
}

rc task_base::resume() {
    k_thread_resume(tid_);
    return rc::ok;
}

rc task_base::terminate() {
    k_thread_abort(tid_);
    return rc::ok;
}

task_base::~task_base() {
    if (tid_) k_thread_abort(tid_);
}

// --- Mutex ---
rc mutex::acquire(uint32_t timeout) {
    int res = k_mutex_lock(&mutex_, make_timeout(timeout));
    return static_cast<rc>(res);
}

rc mutex::release() {
    k_mutex_unlock(&mutex_);
    return rc::ok;
}

// --- Semaphore ---
rc semaphore::acquire(uint32_t timeout) {
    int res = k_sem_take(&sem_, make_timeout(timeout));
    return static_cast<rc>(res);
}

rc semaphore::release() {
    k_sem_give(&sem_);
    return rc::ok;
}

// --- Event Group ---
rc eventgrp::wait(uint32_t pattern, wait_mode mode, uint32_t timeout, uint32_t* actual) {
    bool reset = ((int)mode & 4);
    bool all = ((int)mode & 2);
    
    uint32_t res_bits = k_event_wait(&event_, pattern, all, make_timeout(timeout));
    if (actual) *actual = res_bits;
    
    if (res_bits == 0) return rc::timeout;
    if (reset) k_event_set_masked(&event_, 0, pattern);
    
    return rc::ok;
}

rc eventgrp::set(uint32_t pattern) {
    k_event_post(&event_, pattern);
    return rc::ok;
}

rc eventgrp::clr(uint32_t pattern) {
    k_event_set_masked(&event_, 0, pattern);
    return rc::ok;
}

// --- Memory Pool ---
fmem_base::fmem_base(void* buffer, uint32_t block_size, uint32_t count) {
    k_mem_slab_init(&slab_, buffer, block_size, count);
}

rc fmem_base::acquire(void** data, uint32_t timeout) {
    int res = k_mem_slab_alloc(&slab_, data, make_timeout(timeout));
    return static_cast<rc>(res);
}

rc fmem_base::release(void* data) {
    k_mem_slab_free(&slab_, (void**)data);
    return rc::ok;
}

// --- Queue ---
queue_base::queue_base(void* buffer, uint32_t msg_size, uint32_t msg_cnt) {
    k_msgq_init(&msgq_, (char*)buffer, msg_size, msg_cnt);
}

rc queue_base::send(const void* data, uint32_t timeout) {
    int res = k_msgq_put(&msgq_, data, make_timeout(timeout));
    return static_cast<rc>(res);
}

rc queue_base::receive(void* data, uint32_t timeout) {
    int res = k_msgq_get(&msgq_, data, make_timeout(timeout));
    return static_cast<rc>(res);
}

// --- Timer ---
void timer_base::expiry_fn(struct k_timer *timer_id) {
    timer_base* obj = CONTAINER_OF(timer_id, timer_base, timer_);
    if (obj->func_) obj->func_(obj->param_);
}

timer_base::timer_base(void (*f)(void*), void* p, uint32_t t, repeat_timer r) 
    : func_(f), param_(p), repeat_(r == repeat), period_(t) {
    k_timer_init(&timer_, expiry_fn, nullptr);
}

rc timer_base::start(uint32_t t) {
    k_timer_start(&timer_, K_MSEC(t), repeat_ ? K_MSEC(period_) : K_NO_WAIT);
    return rc::ok;
}

rc timer_base::cancel() {
    k_timer_stop(&timer_);
    return rc::ok;
}

} // namespace os

#include <cstddef>

// Если компилятор не находит определение std::align_val_t, мы можем определить его сами
#if __cplusplus >= 201703L
namespace std {
    enum class align_val_t : size_t {};
}

// Заглушка для оператора delete с выравниванием (C++17)
void operator delete(void* ptr, std::size_t size, std::align_val_t al) noexcept {
    (void)ptr;
    (void)size;
    (void)al;
    // Здесь ничего не делаем, так как статическая память не удаляется
}

void operator delete(void* ptr, std::align_val_t al) noexcept {
    (void)ptr;
    (void)al;
}
#endif