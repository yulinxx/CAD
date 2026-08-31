#include "NativeImportReader.h"

#include <QFileInfo>

#include "Log/SyLogger.h"

NativeImportReader::NativeImportReader()
    : ImportReaderBase(
          Fio::FileFormat::Native, { QStringLiteral("sy"), QStringLiteral("syx") }, QStringLiteral("SanYi Native"))
{
}

ImportResult NativeImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{

    // 检测是否为 3D 格式 (.syx)
    QFileInfo fi(context.sourcePath);
    bool is3D = (fi.suffix().toLower() == QStringLiteral("syx"));
    Fio::FileFormat format = is3D ? Fio::FileFormat::Native3D : Fio::FileFormat::Native;

    SY_INFOF("[NativeImportReader] Importing native format: %s (format=%d)",
        is3D ? "3D (.syx)" : "2D (.sy)",
        static_cast<int>(format));

    // 原生格式不走中立 IR：NativeParser 没有实现 parseToIR（protobuf 文档直接反序列化成
    // Engine 图元，没有中间的 IR 表达），先尝试 IR 只会白跑一次并留下误导性的失败日志。
    // 后续计划是把 .sy / .syx 整体迁到 Engine/Persistence，届时这个读取器会一并撤掉。
    return readViaLegacy(context, format, outEntities);
}

QString NativeImportReader::successMessage(Fio::FileFormat format) const
{
    return format == Fio::FileFormat::Native3D ? QStringLiteral("SanYi 3D Native import successful")
                                               : QStringLiteral("SanYi 2D Native import successful");
}