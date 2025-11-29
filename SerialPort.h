/*------------------------------------------------------------------------
名称：串口通信模块
说明：封装 Windows 串口打开、关闭与异步读写能力
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-29
备注：无
------------------------------------------------------------------------*/
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

/// <summary>封装底层串口句柄并回调收到的数据。</summary>
class SerialPort
{
public:
    using DataHandler = std::function<void(const std::string&)>;

    SerialPort();
    ~SerialPort();

    /// <summary>尝试打开串口。</summary>
    bool Open(const std::wstring& portName, unsigned long baudRate);

    /// <summary>关闭串口并停止读取线程。</summary>
    void Close();

    /// <summary>写入字节流。</summary>
    bool Write(const std::string& data);

    /// <summary>设置数据回调。</summary>
    void SetDataHandler(DataHandler handler);

    /// <summary>查询串口是否处于打开状态。</summary>
    bool IsOpen() const noexcept;

private:
    void ReaderLoop();
    bool Configure(unsigned long baudRate);

private:
    std::atomic<HANDLE> _handle;
    std::thread _reader;
    std::atomic<bool> _running;
    DataHandler _handler;
    std::mutex _handlerMutex;
    std::mutex _writeMutex;
};
