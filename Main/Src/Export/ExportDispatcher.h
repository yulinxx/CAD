#pragma once

#include <memory>
#include <vector>
#include <map>

#include <QString>
#include <QStringList>

#include "FileIO/FileFormat.h"
#include "ExportContext.h"
#include "ExportResult.h"
#include "IExportWriter.h"

/// 导出分发器：根据文件格式自动路由到对应的导出写入器
class ExportDispatcher
{
public:
    ExportDispatcher() = default;
    ~ExportDispatcher() = default;

    /// 注册一个导出写入器
    void registerWriter(std::unique_ptr<IExportWriter> writer);

    /// 根据文件路径自动检测格式并分发到对应写入器
    /// @param context 导出上下文
    /// @param entities 要导出的实体列表
    /// @return 导出结果
    ExportResult dispatch(const ExportContext& context,
        const Fio::VecSyEntityPtr& entities);

    /// 根据文件扩展名推断格式
    static Fio::FileFormat detectFormat(const QString& filePath);

    /// 获取所有支持的导出扩展名列表
    QStringList supportedExtensions() const;

    /// 检查指定路径是否可导出
    bool canExport(const QString& filePath) const;

    /// 获取指定格式的默认扩展名
    QString defaultExtension(Fio::FileFormat format) const;

private:
    /// 根据格式查找对应的写入器
    IExportWriter* findWriter(Fio::FileFormat format) const;

    /// 注册的写入器列表
    std::vector<std::unique_ptr<IExportWriter>> m_writers;
    /// 格式到写入器的映射
    std::map<Fio::FileFormat, IExportWriter*> m_formatMap;
};
