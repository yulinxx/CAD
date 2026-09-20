#pragma once

#include "SqliteRepositoryBase.h"
#include "Persistence/Models/DialogStateRecord.h"

#include <string>
#include <vector>

/**
 * @brief 对话框状态仓储 — 封装 dialog_states 表的 CRUD 操作
 *
 * 用于保存和读取对话框的界面数据（如用户配置、窗口位置、展开状态等）。
 * state_json 字段存储任意对话框状态的 JSON 序列化字符串。
 */
class DialogStateRepository : public SqliteRepositoryBase
{
public:
    explicit DialogStateRepository(Eg::Database& database);

    /// 保存或更新对话框状态（存在则更新，不存在则插入）
    bool save(const DialogStateRecord& record);

    /// 根据对话框标识和文档ID加载状态
    /// @param dialogKey 对话框唯一标识
    /// @param documentId 文档ID（为空则加载全局状态）
    DialogStateRecord load(const std::string& dialogKey,
        const std::string& documentId = "");

    /// 加载指定文档的所有对话框状态
    std::vector<DialogStateRecord> loadByDocument(const std::string& documentId);

    /// 加载所有对话框状态
    std::vector<DialogStateRecord> loadAll();

    /// 删除指定对话框的状态
    bool remove(const std::string& dialogKey, const std::string& documentId = "");

    /// 删除指定文档的所有对话框状态
    int removeByDocument(const std::string& documentId);

private:
    DialogStateRecord rowToRecord(const std::map<std::string, std::string>& row) const;
    std::map<std::string, std::string> recordToRow(const DialogStateRecord& rec) const;
};
