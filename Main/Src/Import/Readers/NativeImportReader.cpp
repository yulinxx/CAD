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
    SY_INFOF("[NativeImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    // 检测是否为 3D 格式 (.syx)
    QFileInfo fi(context.sourcePath);
    bool is3D = (fi.suffix().toLower() == QStringLiteral("syx"));
    Fio::FileFormat format = is3D ? Fio::FileFormat::Native3D : Fio::FileFormat::Native;

    SY_INFOF("[NativeImportReader] Importing native format: %s (format=%d)",
        is3D ? "3D (.syx)" : "2D (.sy)",
        static_cast<int>(format));

    // 主链路：中立 IR 导入（parseToIR → FioEntityConverter）
    ImportResult result;
    QString errMsg;
    if (tryImportViaIR(context, format, outEntities, false, &result, &errMsg))
    {
        return result;
    }
    SY_WARNF("[NativeImportReader] IR path unavailable, falling back to legacy: %s",
        errMsg.isEmpty() ? "no entities" : errMsg.toUtf8().constData());

    // 回退路径：旧版 importFile
    return readViaLegacy(context, format, outEntities);
}

QString NativeImportReader::successMessage(Fio::FileFormat format) const
{
    return format == Fio::FileFormat::Native3D ? QStringLiteral("SanYi 3D Native import successful")
                                               : QStringLiteral("SanYi 2D Native import successful");
}