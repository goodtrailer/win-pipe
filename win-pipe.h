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
#include <stdexcept>
#include <string>
#include <vector>

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
    /// <summary>
    /// Default constructor. Does nothing. No pipe is opened/created, and no
    /// read thread is started.
    /// <para/>
    /// Note: remember that move constructor exists. This constructor is mainly
    /// meant for use with containers which require a default constructor.
    /// </summary>
    receiver() = default;

    /// <param name="name">Name of the pipe.</param>
    /// <param name="buffer_size">Size of the pipe buffer. Only a suggestion and
    /// can be exceeded.</param>
    /// <param name="callback">Callback for when data is read.</param>
    receiver(std::string_view name, DWORD buffer_size, callback_t callback)
    {
        m_param = std::make_shared<thread_param>();
        m_param->buffer_size = (std::max)(buffer_size, MIN_BUFFER_SIZE);

        std::string pipe_name { format_name(name) };
        m_param->pipe = CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, m_param->buffer_size, m_param->buffer_size,
            NMPWAIT_USE_DEFAULT_WAIT, NULL);
        if (m_param->pipe == INVALID_HANDLE_VALUE) {
            std::string msg { "Pipe creation failed: " };
            msg += std::to_string(GetLastError());
            throw std::runtime_error(msg);
        }

        m_param->event = CreateEventA(NULL, TRUE, FALSE, NULL);
        m_param->callback = callback;

        m_thread = CreateThread(NULL, NULL, thread, m_param.get(), 0, NULL);
    }

    /// <summary>
    /// Move constructor.
    /// </summary>
    /// <param name="other">Instance of receiver to move.</param>
    receiver(receiver&& other) noexcept
        : m_thread(other.m_thread)
        , m_param(other.m_param)
    {
        other.m_thread = NULL;
        other.m_param = nullptr;
    }

    /// <summary>
    /// Copy constructor.
    /// <para />
    /// Deleted because there cannot be multiple receivers per
    /// pipe.
    /// </summary>
    /// <param name="other">Instance of receiver to copy.</param>
    receiver(const receiver& other) = delete;

    ~receiver()
    {
        if (m_param)
            SetEvent(m_param->event);

        CancelSynchronousIo(m_thread);
        WaitForSingleObject(m_thread, INFINITE);

        if (m_param)
            CloseHandle(m_param->pipe);
    }

    /// <summary>
    /// Move assignment operator.
    /// <para />
    /// Closes the current pipe and takes control over the other pipe.
    /// </summary>
    /// <param name="other">Instance of receiver to move.</param>
    /// <returns></returns>
    receiver& operator=(receiver&& other) noexcept
    {
        m_thread = other.m_thread;
        m_param = other.m_param;

        other.m_thread = NULL;
        other.m_param = nullptr;

        return *this;
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
        buffer.resize(buffer_size);

        while (WaitForSingleObject(param->event, 1) == WAIT_TIMEOUT) {
            ConnectNamedPipe(pipe, NULL);

            while (WaitForSingleObject(param->event, 1) == WAIT_TIMEOUT) {
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
    std::shared_ptr<thread_param> m_param;
    HANDLE m_thread;
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

    /// <param name="name">Name of the pipe.</param>
    sender(std::string_view name)
    {
        m_name = format_name(name);
        connect();
    }

    /// <summary>
    /// Move constructor.
    /// </summary>
    /// <param name="other">Instance of sender to move.</param>
    sender(sender&& other) noexcept
        : m_name(std::move(other.m_name))
        , m_pipe(other.m_pipe)
    {
        other.m_pipe = NULL;
    }

    /// <summary>
    /// Copy constructor.
    /// <para />
    /// Deleted because support for multiple senders per pipe has not yet been
    /// implemented.
    /// </summary>
    /// <param name="other">Instance of sender to copy.</param>
    sender(const sender& other) = delete;

    ~sender()
    {
        CloseHandle(m_pipe);
    }

    /// <summary>
    /// Move assignment operator.
    /// <para />
    /// Closes the current pipe and takes control over the other pipe.
    /// </summary>
    /// <param name="other">Instance of sender to move.</param>
    /// <returns></returns>
    sender& operator=(sender&& other) noexcept
    {
        CloseHandle(m_pipe);

        m_name = other.m_name;
        m_pipe = other.m_pipe;

        other.m_name = std::string {};
        other.m_pipe = NULL;

        return *this;
    }

    /// <param name="buffer">Buffer to write.</param>
    /// <param name="size">Size of input buffer (amount of data to write).</param>
    ///
    /// <returns>bool True = success; false = fail.</returns>
    bool write(const void* buffer, DWORD size)
    {
        if (WriteFile(m_pipe, buffer, size, NULL, NULL) == FALSE) {
            DWORD error = GetLastError();
            switch (error) {
            case ERROR_INVALID_HANDLE:
            case ERROR_PIPE_NOT_CONNECTED:
                connect();
                break;
            default:
                return false;
            }

            if (WriteFile(m_pipe, buffer, size, NULL, NULL) == FALSE)
                return false;
        }

        FlushFileBuffers(m_pipe);
        return true;
    }

private:
    void connect()
    {
        if (m_pipe != INVALID_HANDLE_VALUE && m_pipe != NULL)
            CloseHandle(m_pipe);

        m_pipe = CreateFileA(m_name.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, NULL, OPEN_ALWAYS, NULL,
            NULL);
    }

private:
    HANDLE m_pipe;
    std::string m_name;
};

}
