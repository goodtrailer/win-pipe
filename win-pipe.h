/*
 * ISC License
 *
 * Copyright (c) 2021 Alden Wu
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#ifndef _WIN32
#error "win-pipe is a Windows only library."
#endif

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <Windows.h>

namespace win_pipe {

namespace details {
    static inline std::string format_name(std::string_view name)
    {
        std::string formatted = R"(\\.\pipe\)";
        formatted += name;
        return formatted;
    }

    struct handle_deleter {
        void operator()(HANDLE handle)
        {
            if (handle != NULL && handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
        }
    };

    using unique_handle = std::unique_ptr<void, handle_deleter>;
}

using callback_t = std::function<void(uint8_t*, size_t)>;

// -------------------------------------------------------------------[ receiver

class receiver {
public:
    /// <summary>
    /// Default constructor. Does nothing. No pipe is opened/created, and no
    /// read thread is started.
    /// <para/>
    /// Note: remember that move constructor exists. This constructor is mainly
    /// meant for use with containers which require a default constructor.
    /// </summary>
    receiver() = default;

    receiver(std::string_view name, DWORD buffer_size, callback_t callback)
    {
        m_param = std::make_unique<thread_param>();
        m_param->name = name;
        m_param->callback = callback;
        m_param->stop_event.reset(CreateEventA(NULL, TRUE, FALSE, NULL));

        DWORD trunc = buffer_size % 1024;
        if (trunc == 0)
            trunc = 1024;
        m_param->buffer_size = buffer_size - trunc + 1024;

        add_pipe(m_param.get());
        m_thread.reset(CreateThread(NULL, NULL, thread, m_param.get(), 0, NULL));
    }

    receiver(receiver&&) noexcept = default;

    ~receiver()
    {
        if (m_param)
            SetEvent(m_param->stop_event.get());

        CancelSynchronousIo(m_thread.get());
        WaitForSingleObject(m_thread.get(), INFINITE);
    }

    receiver& operator=(receiver&&) noexcept = default;

    void set_callback(callback_t callback)
    {
        if (!m_param)
            return;

        std::lock_guard lock { m_param->callback_mutex };
        m_param->callback = callback;
    }

private:
    enum class pipe_state {
        connecting,
        reading,
        sending,
    };

    struct instance {
        details::unique_handle pipe = nullptr;
        std::vector<uint8_t> buffer;
        OVERLAPPED overlap = {};
        pipe_state state = pipe_state::connecting;
        bool pending = false;

        instance() = default;
        instance(instance&&) = default;
        instance(instance&) = delete;

        instance& operator=(instance&&) = default;
    };

    struct thread_param {
        std::vector<instance> instances;
        std::vector<HANDLE> events;

        std::string name;
        DWORD buffer_size;

        details::unique_handle stop_event;
        std::mutex callback_mutex;
        callback_t callback;
    };

private:
    static std::optional<instance> create_instance(std::string_view name,
        DWORD size)
    {
        std::string pipe_name = details::format_name(name);
        instance inst;

        inst.pipe.reset(CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 1024, 1024, NMPWAIT_USE_DEFAULT_WAIT,
            NULL));
        if (!inst.pipe.get() || inst.pipe.get() == INVALID_HANDLE_VALUE)
            return std::nullopt;

        inst.overlap.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!inst.overlap.hEvent || inst.overlap.hEvent == INVALID_HANDLE_VALUE)
            return std::nullopt;

        if (ConnectNamedPipe(inst.pipe.get(), &inst.overlap)
            || GetLastError() != ERROR_IO_PENDING)
            return std::nullopt;

        inst.pending = true;
        inst.state = pipe_state::connecting;
        inst.buffer.resize(size);

        return inst;
    }

    static void delete_instance(instance& inst)
    {
        DisconnectNamedPipe(inst.pipe.get());
        CloseHandle(inst.overlap.hEvent);
        inst = {};
    }

    static bool add_pipe(thread_param* param)
    {
        std::optional<instance> inst_opt = create_instance(param->name,
            param->buffer_size);
        if (!inst_opt.has_value())
            return false;

        auto& inst = param->instances.emplace_back(std::move(inst_opt.value()));
        param->events.emplace_back(inst.overlap.hEvent);

        return true;
    }

    static void remove_pipe(thread_param* param, size_t index)
    {
        auto inst = param->instances.begin() + index;
        delete_instance(*inst);
        param->instances.erase(inst);
        param->events.erase(param->events.begin() + index);
    }

    static void clear_pipes(thread_param* param)
    {
        for (auto& inst : param->instances)
            delete_instance(inst);
        param->instances.clear();
        param->events.clear();
    }

    static DWORD WINAPI thread(LPVOID lp)
    {
        auto* param = reinterpret_cast<thread_param*>(lp);
        auto& instances = param->instances;
        auto& events = param->events;
        auto& stop_event = param->stop_event;
        auto& callback = param->callback;
        auto& callback_mutex = param->callback_mutex;

        size_t index = 0;
        DWORD bytes_read = 0;
        while (WaitForSingleObject(stop_event.get(), 0) == WAIT_TIMEOUT) {
            index = WaitForMultipleObjects((DWORD)events.size(), events.data(),
                        FALSE, 1)
                - WAIT_OBJECT_0;

            if (index < 0 || index >= instances.size())
                continue;
            instance* inst = &instances[index];

            if (inst->pending) {
                BOOL success = GetOverlappedResult(inst->pipe.get(),
                    &inst->overlap, &bytes_read, FALSE);

                switch (inst->state) {
                case pipe_state::connecting:
                    inst->state = pipe_state::reading;
                    add_pipe(param);
                    inst = &instances[index];
                    break;

                case pipe_state::reading:
                    if (!success && GetLastError() != ERROR_MORE_DATA)
                        break;
                    inst->state = pipe_state::sending;
                    break;

                case pipe_state::sending:
                default:
                    remove_pipe(param, index);
                    continue;
                }
            }

            switch (inst->state) {
            case pipe_state::reading:
                if (ReadFile(inst->pipe.get(), inst->buffer.data(),
                        (DWORD)inst->buffer.size(), &bytes_read, &inst->overlap)
                    || GetLastError() == ERROR_MORE_DATA) {
                    inst->pending = false;
                } else if (GetLastError() == ERROR_IO_PENDING) {
                    inst->pending = true;
                    break;
                } else {
                    remove_pipe(param, index);
                    continue;
                }

                [[fallthrough]];
            case pipe_state::sending:
                /* clang-format off */ {
                    std::lock_guard lock { callback_mutex };
                    callback(inst->buffer.data(), (size_t)bytes_read);
                } /* clang-format on */
                inst->pending = false;
                inst->state = pipe_state::reading;
                break;
            default:
                break;
            }
        }

        clear_pipes(param);
        return TRUE;
    }

private:
    std::unique_ptr<thread_param>
        m_param;
    details::unique_handle m_thread;
};

// ---------------------------------------------------------------------[ sender

class sender {
public:
    /// <summary>
    /// Default constructor. Does nothing. Cannot actually write to a pipe.
    /// <para />
    /// Note: remember that move constructor exists. This constructor is mainly
    /// meant for use with containers which require a default constructor.
    /// </summary>
    sender() = default;

    sender(std::string_view name)
        : m_name { details::format_name(name) }
    {
    }

    sender(sender&&) noexcept = default;

    sender& operator=(sender&&) noexcept = default;

    bool send(const void* buffer, DWORD size)
    {
        if (!WriteFile(m_pipe.get(), buffer, size, NULL, NULL)) {
            DWORD error = GetLastError();
            switch (error) {
            case ERROR_INVALID_HANDLE:
            case ERROR_PIPE_NOT_CONNECTED:
                connect();
                break;
            default:
                return false;
            }

            if (!WriteFile(m_pipe.get(), buffer, size, NULL, NULL))
                return false;
        }

        FlushFileBuffers(m_pipe.get());
        return true;
    }

private:
    void connect()
    {
        // In order to CloseHandle before CreateFile, you need to destroy
        // what's inside the unique_ptr by either calling reset() or assigning
        // it nullptr.
        m_pipe = nullptr;
        m_pipe.reset(CreateFileA(m_name.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            NULL, OPEN_ALWAYS, NULL, NULL));
    }

private:
    details::unique_handle m_pipe;
    std::string m_name;
};
}
