#pragma once

namespace RD
{
    // 跨 DLL 版本号
    constexpr int kUiApiVersion = 3;

    // 跨 DLL 对象释放：释放在 DLL 内完成，避免跨 CRT 直接 delete
    // 析构函数 protected，只能经 destroy()/ApiPtr 释放；派生类同样声明析构函数为 protected
    struct IApiObject
    {
        virtual void destroy() = 0;

    protected:
        virtual ~IApiObject() = default;
    };

    // 构时调用接口的 destroy()。
    template<typename T>
    class ApiPtr
    {
    public:
        ApiPtr() = default;

        explicit ApiPtr(T* object)
            : m_object(object)
        {
        }

        ~ApiPtr()
        {
            reset();
        }

        ApiPtr(const ApiPtr&) = delete;
        ApiPtr& operator=(const ApiPtr&) = delete;

        ApiPtr(ApiPtr&& other) noexcept
            : m_object(other.m_object)
        {
            other.m_object = nullptr;
        }

        ApiPtr& operator=(ApiPtr&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                m_object = other.m_object;
                other.m_object = nullptr;
            }
            return *this;
        }

        T* get() const
        {
            return m_object;
        }

        T* operator->() const
        {
            return m_object;
        }

        T& operator*() const
        {
            return *m_object;
        }

        explicit operator bool() const
        {
            return m_object != nullptr;
        }

        void reset(T* object = nullptr)
        {
            if (object == m_object)
                return;

            T* previous = m_object;
            m_object = object;
            if (previous)
                previous->destroy();
        }

    private:
        T* m_object = nullptr;
    };
}  // namespace RD
