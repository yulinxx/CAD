#pragma once

#include <QMetaObject>
#include <QVector>

#include <utility>

class ConnectionGuard
{
public:
    ConnectionGuard() = default;

    explicit ConnectionGuard(QMetaObject::Connection conn)
    {
        if (conn)
        {
            m_connections.append(std::move(conn));
        }
    }

    ConnectionGuard(ConnectionGuard&& other) noexcept
        : m_connections(std::move(other.m_connections))
    {
    }

    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept
    {
        if (this != &other)
        {
            disconnectAll();
            m_connections = std::move(other.m_connections);
        }
        return *this;
    }

    ~ConnectionGuard()
    {
        disconnectAll();
    }

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

    void add(QMetaObject::Connection conn)
    {
        if (conn)
        {
            m_connections.append(std::move(conn));
        }
    }

    template<typename Func1, typename Func2>
    QMetaObject::Connection connect(const typename QtPrivate::FunctionPointer<Func1>::Object* sender,
        Func1 signal, const typename QtPrivate::FunctionPointer<Func2>::Object* receiver, Func2 slot)
    {
        auto conn = QObject::connect(sender, signal, receiver, slot);
        add(conn);
        return conn;
    }

    template<typename Func1, typename Func2>
    QMetaObject::Connection connect(const typename QtPrivate::FunctionPointer<Func1>::Object* sender,
        Func1 signal, Func2 slot)
    {
        auto conn = QObject::connect(sender, signal, slot);
        add(conn);
        return conn;
    }

    void disconnectAll()
    {
        for (auto& conn : m_connections)
        {
            QObject::disconnect(conn);
        }
        m_connections.clear();
    }

    bool isEmpty() const
    {
        return m_connections.isEmpty();
    }

    int count() const
    {
        return m_connections.size();
    }

private:
    QVector<QMetaObject::Connection> m_connections;
};
