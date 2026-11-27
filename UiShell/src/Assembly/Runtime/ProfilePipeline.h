#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

#include "UiShell/Assembly/ShellServices.h"
#include "Model/Assembly/AssemblyInfo.h"
#include "Model/Profile/ProfileLoadResult.h"
#include "Model/Profile/ProfileModel.h"

namespace RD
{
    // profile 加载、诊断与计划生成
    class ProfilePipeline
    {
    public:
        // 解析 UTF-8 JSON
        bool loadFromJson(const QByteArray& utf8Json, const ShellServices& shellServices);

        // 读取文件后交给 loadFromJson
        bool loadFromFile(const QString& filePath, const ShellServices& shellServices);

        // 重新规划
        bool replan(const ShellServices& shellServices);

        const AssemblyInfo& info() const
        {
            return m_assemblyInfo;
        }

        const QList<Diagnostic>& diagnostics() const
        {
            return m_listDiagnostics;
        }

        // 用本一轮装配产生的诊断替换上一轮的装配期诊断；解析期与规划期诊断保持不变。
        void setAssemblyDiagnostics(QList<Diagnostic> diag);

        const QString& lastError() const
        {
            return m_strLastError;
        }

        const QString& profileDir() const
        {
            return m_strProfileDir;
        }

        const QByteArray& json() const
        {
            return m_strProfileJson;
        }

        bool hasModel() const
        {
            return m_profileModel.has_value();
        }

        // 最近一次成功规划时消费的开关版本,用于跳过重复的开关通知
        quint64 featureRevision() const
        {
            return m_featureRev;
        }

    private:
        void setFirstError();
        void clearInfo();

        // 按「解析期 → 规划期 → 装配期」重新合并 m_listDiagnostics，并刷新 lastError()
        void composeDiagnostics();

    private:
        AssemblyInfo m_assemblyInfo;

        QList<Diagnostic> m_parseDiagnostics;
        QList<Diagnostic> m_assemblyDiagnostics;
        QList<Diagnostic> m_listDiagnostics;

        QString m_strLastError;

        // profile 所在目录
        QString m_strProfileDir;

        // 最近一次成功读取的 profile 原文与解析结果：语言/开关变化后直接复用
        QByteArray m_strProfileJson;

        std::optional<ProfileModel> m_profileModel;
        quint64 m_featureRev = 0;
    };
}  // namespace RD
