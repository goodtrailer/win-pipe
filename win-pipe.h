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
#include <vector>

#include <iostream>

#include <Windows.h>

static inline std::string format_name(std::string_view name)
{
    std::string formatted = R"(\\.\pipe\)";
    formatted += name;
    return formatted;
}

namespace win_pipe {

// -------------------------------------------------------------------[ receiver

class receiver {
public:
    using callback_t = std::function<void(uint8_t*, size_t)>;

public:
    /// <param name="name">Name of the pipe.</param>
    /// <param name="buffer_size">Size of the pipe buffer. Only a suggestion and can be exceeded.</param>
    /// <param name="callback">Callback for when data is read.</param>
    receiver(std::string_view name, DWORD buffer_size, callback_t callback)
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

        m_param.event = CreateEventA(NULL, TRUE, FALSE, NULL);
        m_param.callback = callback;

        m_thread = CreateThread(NULL, NULL, thread, &m_param, 0, NULL);
    }

    ~receiver()
    {
        SetEvent(m_param.event);
        CancelSynchronousIo(m_thread);
        WaitForSingleObject(m_thread, INFINITE);

        CloseHandle(m_param.pipe);
    }

public:
    static constexpr DWORD MIN_BUFFER_SIZE = 1024;

private:
    static DWORD WINAPI thread(LPVOID lp)
    {
        auto* param = reinterpret_cast<thread_param*>(lp);
        HANDLE& pipe = param->pipe;
        DWORD& buffer_size = param->buffer_size;

        std::vector<uint8_t> buffer;
        buffer.reserve(buffer_size);

        while (WaitForSingleObject(param->event, 1) == WAIT_TIMEOUT) {
            ConnectNamedPipe(pipe, NULL);

            while (WaitForSingleObject(param->event, 1) == WAIT_TIMEOUT) {
                DWORD bytes_read = 0;
                if (!ReadFile(pipe, buffer.data(), buffer.capacity(), &bytes_read, NULL)) {
                    if (GetLastError() != ERROR_MORE_DATA)
                        break;

                    DWORD leftover = 0;
                    PeekNamedPipe(pipe, NULL, NULL, NULL, NULL, &leftover);

                    std::vector<uint8_t> new_buffer;
                    new_buffer.reserve(bytes_read + leftover);
                    std::copy(buffer.begin(), buffer.begin() + bytes_read, new_buffer.begin());
                    buffer = std::move(new_buffer);

                    DWORD more_bytes_read = 0;
                    ReadFile(pipe, buffer.data() + bytes_read, leftover, &more_bytes_read, NULL);
                    bytes_read += more_bytes_read;
                }

                param->callback(buffer.data(), (size_t)bytes_read);
            }

            DisconnectNamedPipe(pipe);
        }

        return TRUE;
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

// ---------------------------------------------------------------------[ sender

class sender {
public:
    /// <param name="name">Name of the pipe.</param>
    sender(std::string_view name)
    {
        m_name = format_name(name);
        connect();
    }

    ~sender()
    {
        CloseHandle(m_pipe);
    }

    /// <param name="buffer">Buffer to write.</param>
    /// <param name="size">Size of input buffer (amount of data to write).</param>
    void write(const void* buffer, DWORD size)
    {
        if (m_pipe == INVALID_HANDLE_VALUE)
            connect();

        if (WriteFile(m_pipe, buffer, size, NULL, NULL) == FALSE
            && GetLastError() == ERROR_PIPE_NOT_CONNECTED) {
            connect();
            WriteFile(m_pipe, buffer, size, NULL, NULL);
        }

        FlushFileBuffers(m_pipe);
    }

private:
    void connect()
    {
        m_pipe = CreateFileA(m_name.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, NULL, OPEN_ALWAYS, NULL,
            NULL);
    }

private:
    HANDLE m_pipe;
    std::string m_name;
};

}
