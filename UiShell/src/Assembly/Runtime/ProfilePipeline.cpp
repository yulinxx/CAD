#include "Assembly/Runtime/ProfilePipeline.h"

#include <QFile>
#include <QFileInfo>

#include <utility>

#include "Platform/WindowFrame.h"
#include "Model/Assembly/AssemblyPlanner.h"
#include "Model/Profile/ProfileParser.h"

namespace RD
{
    bool ProfilePipeline::loadFromJson(const QByteArray& utf8Json, const ShellServices& shellServices)
    {
        m_strLastError.clear();

        // 缓存
        m_strProfileJson = utf8Json;

        // 站点可能用 --profile 换成外置 ui.json：在规划前让客户目录按这份
        // profile 补充登记菜单命令，否则新命令会被 AssemblyPlanner 过滤掉。
        // 该接口是可选的；core 不认识任何客户类型。
        if (shellServices.cmdProfileRegistrar)
            shellServices.cmdProfileRegistrar->registerFromProfile(utf8Json);

        const ProfileLoadResult result = ProfileParser::loadFromJson(utf8Json);
        m_parseDiagnostics = result.diagnostics;
        m_assemblyDiagnostics.clear();

        if (!result.isOk())
        {
            m_profileModel.reset();
            clearInfo();
            composeDiagnostics();
            return false;
        }

        m_profileModel = *result.profileModel;
        return replan(shellServices);
    }

    bool ProfilePipeline::loadFromFile(const QString& filePath, const ShellServices& shellServices)
    {
        m_strProfileDir = QFileInfo(filePath).absolutePath();

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            Diagnostic d;
            d.level = Diagnostic::Level::Error;
            d.strJsonPath = QStringLiteral("$");
            d.strMsg = QStringLiteral("cannot open profile '%1'").arg(filePath);
            m_parseDiagnostics = { d };
            m_assemblyDiagnostics.clear();
            m_profileModel.reset();
            m_strProfileJson.clear();
            clearInfo();
            composeDiagnostics();
            return false;
        }
        return loadFromJson(file.readAll(), shellServices);
    }

    bool ProfilePipeline::replan(const ShellServices& shellServices)
    {
        if (!m_profileModel)
            return false;

        // 先取版本再取快照
        const quint64 revision = shellServices.featureFlags ? shellServices.featureFlags->revision() : 0;
        FeatureFlagSet flags;
        if (shellServices.featureFlags)
            flags = shellServices.featureFlags->snapshot();

        m_assemblyInfo = planAssembly(
            *m_profileModel, flags, shellServices.cmdCatalog, shellServices.text);
        m_featureRev = revision;
        composeDiagnostics();
        return true;
    }

    void ProfilePipeline::setAssemblyDiagnostics(QList<Diagnostic> diag)
    {
        m_assemblyDiagnostics = std::move(diag);
        composeDiagnostics();
    }

    void ProfilePipeline::composeDiagnostics()
    {
        m_listDiagnostics = m_parseDiagnostics;

        // 规划器跳过的配置项（如菜单引用了未知命令）以 Warning 形式进入诊断，宿主才看得到。
        for (const QString& warning : m_assemblyInfo.strListWarnings)
        {
            Diagnostic d;
            d.level = Diagnostic::Level::Warning;
            d.strJsonPath = QStringLiteral("$");
            d.strMsg = warning;
            m_listDiagnostics.append(std::move(d));
        }

        m_listDiagnostics.append(m_assemblyDiagnostics);
        setFirstError();
    }

    void ProfilePipeline::setFirstError()
    {
        for (const Diagnostic& diagnostic : m_listDiagnostics)
        {
            if (diagnostic.level == Diagnostic::Level::Error)
            {
                m_strLastError = diagnostic.strJsonPath + QStringLiteral(": ") + diagnostic.strMsg;
                return;
            }
        }

        m_strLastError.clear();
    }

    void ProfilePipeline::clearInfo()
    {
        m_assemblyInfo = AssemblyInfo();
        m_featureRev = 0;
    }
}  // namespace RD