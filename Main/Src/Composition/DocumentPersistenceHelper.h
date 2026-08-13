#pragma once

#include <QString>
#include <string>

class PersistenceService;

/**
 * @brief 文档持久化辅助工具 — 统一管理导入/导出时的文档记录写入
 *
 * 将原先散落在 ApplicationCompositionRoot 和 FileOperationRegistry 中的
 * 文档记录保存逻辑收口到此处，消除重复代码。
 */
class DocumentPersistenceHelper
{
public:
    /// 导入/打开文件后记录文档信息（更新 lastOpenedAt）
    static void recordImport(PersistenceService* persistence, const QString& filePath, int entityCount);

    /// 导出/保存文件后记录文档信息（更新 lastSavedAt）
    static void recordExport(PersistenceService* persistence, const std::string& filePath, int entityCount);
};
