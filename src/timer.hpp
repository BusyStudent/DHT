#pragma once

#include <Windows.h>
#include <chrono>
#include <tuple>
#include <ctime>
#include <map>

#if defined(_MSC_VER)
    #pragma comment(lib, "user32.lib")
#endif

class TimerService {
    public:
        TimerService() {
            static bool once = false;
            if (!once) {
                ::WNDCLASSEXW wx = { };
                wx.cbSize = sizeof(wx);
                wx.lpszClassName = L"TimerService";
                wx.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
                    auto service = reinterpret_cast<TimerService*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                    if (!service) {
                        return ::DefWindowProcW(hwnd, msg, wp, lp);
                    }
                    return service->wnd_proc(msg, wp, lp);
                };

                ::RegisterClassExW(&wx);
            }

            msg_hwnd = ::CreateWindowExW(
                0,
                L"TimerService",
                nullptr,
                0,
                0,
                0,
                0,
                0,
                HWND_MESSAGE,
                nullptr,
                nullptr,
                nullptr
            );

            ::SetWindowLongPtrW(msg_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        }
        ~TimerService() {
            // Kill all timer
            while (!timers.empty()) {
                del_timer(timers.begin()->first);
            }

            ::DestroyWindow(msg_hwnd);
        }
        void process() {
            ::MSG msg;
            while (::PeekMessageW(&msg, msg_hwnd, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
        template <typename Callable, typename ...Args>
        uintptr_t add_timer(int ms, Callable &&cb, Args &&...args) {
            using Fn = Functor<Callable, Args...>;
            auto  fn = new Fn(std::forward<Callable>(cb), std::forward<Args>(args)...);
            auto  id = alloc_id();

            id = ::SetTimer(msg_hwnd, id, ms, nullptr);
            if (id == 0) {
                // Error
                delete fn;
                return 0;
            }
            insert_functor(id, fn);
            return id;
        }
        template <typename Callable, typename ...Args>
        uintptr_t add_timer(std::chrono::milliseconds ms, Callable &&cb, Args &&...args) {
            return add_timer(ms.count(), std::forward<Callable>(cb), std::forward<Args>(args)...);
        }
        template <typename Callable, typename ...Args>
        uintptr_t single_shot(int ms, Callable &&cb, Args &&...args) {
            auto t = add_timer(ms, std::forward<Callable>(cb), std::forward<Args>(args)...);
            return set_timer_single_shot(t);
        }
        template <typename Callable, typename ...Args>
        uintptr_t single_shot(std::chrono::milliseconds ms, Callable &&cb, Args &&...args) {
            auto t = add_timer(ms, std::forward<Callable>(cb), std::forward<Args>(args)...);
            return set_timer_single_shot(t);
        }
        bool     del_timer(uintptr_t t) {
            auto timer = timers.find(t);
            if (timer != timers.end()) {
                auto [id, data] = *timer;
                data.destroy(data.args);
                timers.erase(timer);

                return ::KillTimer(msg_hwnd, t);
            }
            return false;
        }
        bool     set_timer_single_shot(uintptr_t t) {
            auto timer = timers.find(t);
            if (timer != timers.end()) {
                timer->second.single_shot = true;
                return true;
            }
            return false;
        }
    private:
        template <typename Callable, typename ...Args>
        class Functor : public std::tuple<Args...> {
            public:
                Functor (Callable &&callable, Args &&...args) : 
                    std::tuple<Args...>(std::forward<Args>(args)...), cb(callable) { }
                void invoke() {
                    std::apply(cb, static_cast<std::tuple<Args...>&>(*this));
                }

                static void Invoke(void *self) {
                    static_cast<Functor*>(self)->invoke();
                }
                static void Destroy(void *self) {
                    delete static_cast<Functor*>(self);
                }
                Callable cb;
        };
        template <typename  T>
        void    insert_functor(uintptr_t id, T *functor) {
            TimerData data;
            data.args = functor;
            data.invoke = T::Invoke;
            data.destroy = T::Destroy;

            timers[id] = data;
        }
        LRESULT wnd_proc(::UINT msg, ::WPARAM wp, ::LPARAM lp) {
            if (msg == WM_TIMER) {
                auto timer = timers.find(wp);
                if (timer != timers.end()) {
                    auto [id, data] = *timer;
                    data.invoke(data.args);

                    if (data.single_shot) {
                        del_timer(id);
                    }
                    return 0;
                }
            }
            return ::DefWindowProcW(msg_hwnd, msg, wp, lp);
        }
        uintptr_t alloc_id() {
            ::srand(::time(nullptr));

            uintptr_t id;
            do {
                id = ::rand();
            }
            while (timers.find(id) != timers.end());

            return id;
        }

        class TimerData {
            public:
                void *args = nullptr;
                void (*invoke)(void *) = nullptr;
                void (*destroy)(void *) = nullptr;
                bool single_shot = false;
        };
        ::std::map<uintptr_t, TimerData> timers;
        ::HWND                           msg_hwnd = nullptr;
};