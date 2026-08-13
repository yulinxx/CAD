#include "StlImportReader.h"

#include "Engine3D/Loader/StlLoader.h"
#include "FileIO/FileIOManager.h"
#include "Import/FioEntityConverter.h"
#include "Log/SyLogger.h"

ImportResult StlImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    std::string pathStr = context.sourcePath.toUtf8().toStdString();

    // ===== 主链路：中立 IR 导入（parseToIR → FioEntityConverter） =====
    {
        Fio::FileIOManager fileIO;
        Fio::FioParseResult ir;
        char irErrBuf[1024] = { 0 };
        bool irOk = fileIO.importToIR(pathStr.c_str(), Fio::FileFormat::STL, &ir, irErrBuf, sizeof(irErrBuf));

        if (irOk && ir.entityCount > 0)
        {
            auto converted = FioEntityConverter::convertAll(ir);
            if (!converted.empty())
            {
                outEntities.clear();
                outEntities.reserve(converted.size());
                for (auto& e : converted)
                {
                    outEntities.emplace_back(std::move(e));
                }

                SY_INFOF("[StlImportReader] IR path succeeded: entities=%zu", outEntities.size());
                return ImportResult::ok(QStringLiteral("STL import successful"), static_cast<int>(outEntities.size()));
            }
        }
        SY_WARNF("[StlImportReader] IR path unavailable, falling back to legacy: %s",
            irErrBuf[0] ? irErrBuf : "no entities");
    }

    // ===== 回退路径：旧版直接 StlLoader =====
    {
        std::string error;
        auto mesh = Eg::StlLoader::load(pathStr, error);

        if (!mesh)
        {
            QString msg = QString::fromStdString(error);
            SY_ERRORF(
                "[StlImportReader] Failed to load STL file: %s (path=%s)", msg.toUtf8().constData(), pathStr.c_str());
            return ImportResult::fail(msg, ImportErrorType::ParseFailed);
        }

        SY_INFOF("[StlImportReader] STL loaded successfully (legacy): %s, triangles=%zu, verts=%zu",
            mesh->name(),
            mesh->triangleCount(),
            mesh->vertices.size());

        outEntities.push_back(std::move(mesh));
        return ImportResult::ok(QStringLiteral("STL import successful: 1 mesh entity"), 1);
    }
}