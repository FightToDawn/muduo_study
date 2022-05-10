// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Exception.h"
#include "muduo/base/CurrentThread.h"

namespace muduo
{

Exception::Exception(string msg)
  : message_(std::move(msg)),
    stack_(CurrentThread::stackTrace(/*demangle=*/false)) // 构造函数里添加记录调用堆栈的功能
{
}

}  // namespace muduo
