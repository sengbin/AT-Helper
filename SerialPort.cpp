/*------------------------------------------------------------------------
名称：串口通信实现
说明：实现串口打开、关闭与读写线程
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-29
备注：无
------------------------------------------------------------------------*/
#include "SerialPort.h"

#include <array>
#include <chrono>

SerialPort::SerialPort()
    : _handle(INVALID_HANDLE_VALUE), _running(false)
{
}

SerialPort::~SerialPort()
{
    Close();
}

bool SerialPort::Open(const std::wstring& rawPortName, unsigned long baudRate)
{
    Close();

    std::wstring portName = rawPortName;
    if (portName.rfind(L"\\\\.\\", 0) != 0)
    {
        portName = L"\\\\.\\" + portName;
    }

    HANDLE handle = CreateFileW(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    if (!SetupComm(handle, 4096, 4096))
    {
        CloseHandle(handle);
        return false;
    }

    _handle.store(handle);
    if (!Configure(baudRate))
    {
        CloseHandle(handle);
        _handle.store(INVALID_HANDLE_VALUE);
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 40;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 40;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 40;
    if (!SetCommTimeouts(handle, &timeouts))
    {
        CloseHandle(handle);
        _handle.store(INVALID_HANDLE_VALUE);
        return false;
    }

    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    EscapeCommFunction(handle, SETDTR);
    EscapeCommFunction(handle, SETRTS);

    _running.store(true);
    _reader = std::thread(&SerialPort::ReaderLoop, this);
    return true;
}

void SerialPort::Close()
{
    _running.store(false);
    HANDLE handle = _handle.exchange(INVALID_HANDLE_VALUE);
    if (handle != INVALID_HANDLE_VALUE)
    {
        PurgeComm(handle, PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR);
        CloseHandle(handle);
    }
    if (_reader.joinable())
    {
        _reader.join();
    }
}

bool SerialPort::Write(const std::string& data)
{
    if (data.empty())
    {
        return true;
    }
    HANDLE handle = _handle.load();
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    std::lock_guard<std::mutex> guard(_writeMutex);
    DWORD written = 0;
    const BOOL result = WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    return result != FALSE && written == data.size();
}

void SerialPort::SetDataHandler(DataHandler handler)
{
    std::lock_guard<std::mutex> guard(_handlerMutex);
    _handler = std::move(handler);
}

bool SerialPort::IsOpen() const noexcept
{
    return _handle.load() != INVALID_HANDLE_VALUE;
}

void SerialPort::ReaderLoop()
{
    std::array<char, 1024> buffer{};
    while (_running.load())
    {
        HANDLE handle = _handle.load();
        if (handle == INVALID_HANDLE_VALUE)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
        if (!ok)
        {
            if (!_running.load())
            {
                break;
            }
            const auto error = GetLastError();
            if (error == ERROR_OPERATION_ABORTED)
            {
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (bytesRead == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        DataHandler handlerCopy;
        {
            std::lock_guard<std::mutex> guard(_handlerMutex);
            handlerCopy = _handler;
        }
        if (handlerCopy)
        {
            handlerCopy(std::string(buffer.data(), buffer.data() + bytesRead));
        }
    }
}

bool SerialPort::Configure(unsigned long baudRate)
{
    HANDLE handle = _handle.load();
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle, &dcb))
    {
        return false;
    }
    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    return SetCommState(handle, &dcb) != FALSE;
}
