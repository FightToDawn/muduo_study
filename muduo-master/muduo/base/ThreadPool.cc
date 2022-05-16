// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/ThreadPool.h"

#include "muduo/base/Exception.h"

#include <assert.h>
#include <stdio.h>

using namespace muduo;

ThreadPool::ThreadPool(const string& nameArg)
  : mutex_(),
    notEmpty_(mutex_),
    notFull_(mutex_),
    name_(nameArg),
    maxQueueSize_(0),
    running_(false)
{
}

ThreadPool::~ThreadPool()
{
  if (running_)
  {
    stop();
  }
}
// 线程池类 会先setMaxQueueSize 然后调用本函数 start   本函数只是开启对应数量的线程  等待 有run调用 推送任务进来 立即执行该任务
void ThreadPool::start(int numThreads)
{
  assert(threads_.empty());//开始时一定是空的
  running_ = true;
  threads_.reserve(numThreads);//开辟空间
  for (int i = 0; i < numThreads; ++i)
  {
    char id[32];
    snprintf(id, sizeof id, "%d", i+1);
    threads_.emplace_back(new muduo::Thread(
          std::bind(&ThreadPool::runInThread, this), name_+id));//线程容器增加线程
    threads_[i]->start();//开启线程 执行ThreadPool::runInThread
  }
  if (numThreads == 0 && threadInitCallback_)//如果线程数量为0 而且threadInitCallback_不为空 则执行threadInitCallback_
  {
    threadInitCallback_();
  }
}
// 退出线程池 只是停止了任务队列的添加和取用 正在跑的线程函数 是控制不了的
void ThreadPool::stop()
{
  {
  MutexLockGuard lock(mutex_);
  running_ = false; //先于notifyAll 设置running_ 方便wait立即退出 并检查running_退出
  notEmpty_.notifyAll();
  notFull_.notifyAll();
  }
  for (auto& thr : threads_)
  {
    thr->join();
  }
}

size_t ThreadPool::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return queue_.size();
}
// 任务队列 增加一个任务
void ThreadPool::run(Task task)
{
  if (threads_.empty())
  {
    task();
  }
  else
  {
    MutexLockGuard lock(mutex_);
    while (isFull() && running_) //必须在循环里调用 condition.wait
    {
      notFull_.wait();//如果当前任务队列是满的 就先等待
    }
    if (!running_) return;
    assert(!isFull());

    queue_.push_back(std::move(task));//使用移动语义
    notEmpty_.notify();
  }
}
// 从任务队列里取任务 如果队列为空 就等待
ThreadPool::Task ThreadPool::take()
{
  MutexLockGuard lock(mutex_);//多线程调用本函数 所以需要加锁
  // always use a while-loop, due to spurious wakeup
  while (queue_.empty() && running_)
  {
    notEmpty_.wait();//等待notEmpty_.notify或notifyAll 收到该信号 说明任务队列不空里  立即取用任务 执行之
  }
  Task task;
  if (!queue_.empty())
  {
    task = queue_.front();
    queue_.pop_front();
    if (maxQueueSize_ > 0)
    {
      notFull_.notify();//通知当前队列有空位了 
    }
  }
  return task;
}

bool ThreadPool::isFull() const
{
  mutex_.assertLocked();
  return maxQueueSize_ > 0 && queue_.size() >= maxQueueSize_;
}
// 线程池 各个线程的线程函数 等待run推送任务进来 执行之
void ThreadPool::runInThread()
{
  try
  {
    if (threadInitCallback_)//如果threadInitCallback_不为空 则先执行 threadInitCallback_
    {
      threadInitCallback_();
    }
    while (running_)
    {
      Task task(take());//取到一个任务回调 执行之
      if (task)
      {
        task();
      }
    }
  }
  catch (const Exception& ex)
  {
    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
    abort();
  }
  catch (const std::exception& ex)
  {
    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    abort();
  }
  catch (...)
  {
    fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
    throw; // rethrow
  }
}

