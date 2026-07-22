#pragma once

#include <memory>
#include <vector>
#include <map>

#include <QString>
#include <QStringList>

#include "FileIO/FileFormat.h"
#include "IImportReader.h"

/// 导入分发器：根据文件格式自动路由到对应的导入读取器
class ImportDispatcher
{
public:
    ImportDispatcher() = default;
    ~ImportDispatcher() = default;

    /// 注册一个导入读取器
    void registerReader(std::unique_ptr<IImportReader> reader);

    /// 根据文件路径自动检测格式并分发到对应读取器
    /// @param context 导入上下文
    /// @param outEntities 输出：导入的实体列表
    /// @return 导入结果
    ImportResult dispatch(const ImportContext& context,
        Fio::VecSyEntityPtr& outEntities);

    /// 根据文件扩展名推断格式
    static Fio::FileFormat detectFormat(const QString& filePath);

    /// 获取所有支持的导入扩展名列表
    QStringList supportedExtensions() const;

    /// 检查指定路径是否可导入
    bool canImport(const QString& filePath) const;

private:
    /// 根据格式查找对应的读取器
    IImportReader* findReader(Fio::FileFormat format) const;

    /// 注册的读取器列表
    std::vector<std::unique_ptr<IImportReader>> m_readers;
    /// 格式到读取器的映射（快速查找）
    std::map<Fio::FileFormat, IImportReader*> m_formatMap;
};
