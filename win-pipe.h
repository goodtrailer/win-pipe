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

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <iostream>

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

    receiver(std::string_view name, callback_t callback)
    {
        m_param = std::make_unique<thread_param>();

        std::string pipe_name { details::format_name(name) };
        m_param->pipe.reset(CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 1024, 1024, NMPWAIT_USE_DEFAULT_WAIT, NULL));
        if (m_param->pipe.get() == INVALID_HANDLE_VALUE) {
            std::string msg { "Pipe creation failed: " };
            msg += std::to_string(GetLastError());
            throw std::runtime_error(msg);
        }

        m_param->callback = callback;
        m_param->event.reset(CreateEventA(NULL, TRUE, FALSE, NULL));

        m_thread.reset(CreateThread(NULL, NULL, thread, m_param.get(), 0, NULL));
    }

    receiver(receiver&&) noexcept = default;

    ~receiver()
    {
        if (m_param)
            SetEvent(m_param->event.get());

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
    static DWORD WINAPI thread(LPVOID lp)
    {
        auto* param = reinterpret_cast<thread_param*>(lp);
        auto pipe = param->pipe.get();
        auto event = param->event.get();
        auto& callback = param->callback;
        auto& callback_mutex = param->callback_mutex;

        std::vector<uint8_t> buffer(1024);

        while (WaitForSingleObject(event, 1) == WAIT_TIMEOUT) {
            ConnectNamedPipe(pipe, NULL);

            while (WaitForSingleObject(event, 1) == WAIT_TIMEOUT) {
                DWORD bytes_read = 0;
                if (!ReadFile(pipe, buffer.data(), (DWORD)buffer.size(), &bytes_read, NULL)) {
                    if (GetLastError() != ERROR_MORE_DATA)
                        break;

                    DWORD leftover = 0;
                    PeekNamedPipe(pipe, NULL, NULL, NULL, NULL, &leftover);
                    buffer.resize(bytes_read + leftover);

                    DWORD more_bytes_read = 0;
                    ReadFile(pipe, buffer.data() + bytes_read, leftover, &more_bytes_read, NULL);
                    bytes_read += more_bytes_read;
                }
                std::lock_guard lock { callback_mutex };
                callback(buffer.data(), (size_t)bytes_read);
            }

            DisconnectNamedPipe(pipe);
        }

        return TRUE;
    }

private:
    struct thread_param {
        details::unique_handle pipe;
        details::unique_handle event;
        std::mutex callback_mutex;
        callback_t callback;
    };

private:
    std::unique_ptr<thread_param> m_param;
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
        if (WriteFile(m_pipe.get(), buffer, size, NULL, NULL) == FALSE) {
            DWORD error = GetLastError();
            switch (error) {
            case ERROR_INVALID_HANDLE:
            case ERROR_PIPE_NOT_CONNECTED:
                connect();
                break;
            default:
                return false;
            }

            if (WriteFile(m_pipe.get(), buffer, size, NULL, NULL) == FALSE)
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
        m_pipe.reset(CreateFileA(m_name.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, NULL, OPEN_ALWAYS, NULL,
            NULL));
    }

private:
    details::unique_handle m_pipe;
    std::string m_name;
};

}
