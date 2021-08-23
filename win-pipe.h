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
#include <stdexcept>
#include <string>

#include <Windows.h>

static inline std::string format_name(std::string_view name)
{
    std::string formatted = R"(\\.\pipe\)";
    formatted += name;
    return formatted;
}

namespace win_pipe {

// ----------------------------------------------------------------[ pipe_server

class server {
public:
    using callback_t = std::function<void(uint8_t*, size_t)>;

public:
    /// <param name="name">Name of the pipe.</param>
    /// <param name="buffer_size">Size of the pipe buffer.</param>
    /// <param name="callback">Callback for when data is read.</param>
    server(std::string_view name, DWORD buffer_size, callback_t callback)
    {
        m_param.buffer_size = (std::max)(buffer_size, MIN_BUFFER_SIZE);

        std::string pipe_name = format_name(name);
        m_param.pipe = CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, m_param.buffer_size, m_param.buffer_size,
            NMPWAIT_USE_DEFAULT_WAIT, NULL);
        if (m_param.pipe == INVALID_HANDLE_VALUE) {
            std::string msg = "Pipe creation failed: ";
            msg += std::to_string(GetLastError());
            throw std::runtime_error(msg);
        }

        m_param.event = CreateEventA(NULL, FALSE, FALSE, NULL);
        m_param.callback = callback;

        m_thread = CreateThread(NULL, NULL, thread, &m_param, 0, NULL);
    }

    ~server()
    {
        SetEvent(m_param.event);
        CancelSynchronousIo(m_thread);
        WaitForSingleObject(m_thread, INFINITE);

        DisconnectNamedPipe(m_param.pipe);
        CloseHandle(m_param.pipe);
    }

public:
    static constexpr DWORD MIN_BUFFER_SIZE = 1024;

private:
    static DWORD WINAPI thread(LPVOID lp)
    {
        auto* param = reinterpret_cast<thread_param*>(lp);
        HANDLE& pipe = param->pipe;

        uint8_t* buffer = new uint8_t[param->buffer_size];

        while (WaitForSingleObject(param->event, 1) == WAIT_TIMEOUT) {
            DWORD size;
            if (ReadFile(pipe, buffer, sizeof(buffer), &size, NULL)) {
                param->callback(buffer, (size_t)size);
            }
        }

        delete[] buffer;
    }

private:
    struct thread_param {
        HANDLE pipe;
        HANDLE event;
        callback_t callback;
        DWORD buffer_size;
    };

private:
    thread_param m_param;
    HANDLE m_thread;
};

// ----------------------------------------------------------------[ pipe_client

class client {
public:
    /// <param name="name">Name of the pipe.</param>
    /// <param name="buffer_size">Size of the pipe buffer.</param>
    client(std::string_view name, DWORD buffer_size)
    {
        m_buffer_size = (std::max)(buffer_size, MIN_BUFFER_SIZE);

        std::string pipe_name = format_name(name);
        m_pipe = CreateFileA(pipe_name.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, NULL, OPEN_ALWAYS, NULL,
            NULL);
        if (m_pipe == INVALID_HANDLE_VALUE) {
            std::string msg = "Opening pipe failed: ";
            msg += std::to_string(GetLastError());
            throw std::runtime_error(msg);
        }
    }

    ~client()
    {
        CloseHandle(m_pipe);
    }

    /// <param name="buffer">Buffer to write.</param>
    /// <param name="size">Size of input buffer.</param>
    void write(uint8_t* buffer, DWORD size)
    {
        DWORD trunc_size = (std::min)(size, m_buffer_size);
        WriteFile(m_pipe, buffer, trunc_size, NULL, NULL);
    }

public:
    static constexpr DWORD MIN_BUFFER_SIZE = 1024;

private:
    DWORD m_buffer_size;
    HANDLE m_pipe;
};

}
