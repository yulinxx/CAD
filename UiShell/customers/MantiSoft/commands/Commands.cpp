#include "Commands.h"

#include "UiDialogs/AboutDialogs.h"
#include "UiDialogs/SampleDialogs.h"

namespace MS
{
    namespace
    {
        // 示例算法：参数来自 dialog.sample.params 的结果或面板直接给的 args，算法本身与界面无关。
        void runSample(const QJsonObject& args)
        {
            AA::samples::runSampleCommand(QString::fromLatin1(AA::samples::kSampleRunCmdId), args);
        }

        struct LegacyCommand
        {
            const char* id;
            const char* title;
            const char* icon;          // 相对 ICON_DIR 的资源路径，空串表示无图标
            const char* shortcut = ""; // 空串表示无快捷键；需要时直接填 "Ctrl+X"
            bool isMode = false;       // 工具模式命令：同一时刻只有一个处于选中态
        };
    }  // namespace

    Commands::Commands()
    {
        add(QStringLiteral("test.edit.undo"), [this](const QJsonObject&) {
            if (m_hasDocument) m_hasDocument = false;  // 关闭文档状态重置
            }).title(QStringLiteral("command.edit.undo")).shortcut(QStringLiteral("Ctrl+Z"));
        add(QStringLiteral("test.edit.redo"), [this](const QJsonObject&) {
            // Redo：在本客户端暂无实现业务，仅记录状态变化
            }).title(QStringLiteral("command.edit.redo")).shortcut(QStringLiteral("Ctrl+Y"));
        add(QString::fromLatin1(AA::samples::kSampleRunCmdId), runSample).shortcut(QStringLiteral("F5"));

        const QString ICON_DIR = QStringLiteral(":/UiMantiSoft/profile/assets/icons/");

        // 带 handler / 可用条件的命令保留独立登记，不进数据表。
        // File
        add(QStringLiteral("file.new"), [this](const QJsonObject&) { m_hasDocument = true; })
            .icon(ICON_DIR + QStringLiteral("svg/Menu/New.svg"))
            .shortcut(QStringLiteral("Ctrl+N"));

        add(QStringLiteral("file.open"), [this](const QJsonObject&) { m_hasDocument = true; })
            .icon(ICON_DIR + QStringLiteral("svg/Menu/Open.svg"))
            .shortcut(QStringLiteral("Ctrl+O"));

        add(QStringLiteral("file.save"), [this](const QJsonObject&) {
            // 保存文档：实际保存由模式/业务模块实现；shell 会在 invoke 后自动 refreshCommands()
            })
            .icon(ICON_DIR + QStringLiteral("svg/Menu/Save.svg"))
            .shortcut(QStringLiteral("Ctrl+S"))
            .enabledWhen([this]() { return m_hasDocument; });

        add(QStringLiteral("file.saveAs"), [this](const QJsonObject&) {
            // 另存为：实际另存由模式/业务模块实现
            })
            .shortcut(QStringLiteral("Ctrl+Shift+S"));

        add(QStringLiteral("file.saveAll"), [this](const QJsonObject&) {
            // 全部保存：实际全部保存由模式/业务模块实现
            });

        static const LegacyCommand kLegacyCommands[] = {
            // File
            { "file.close",      "Close",                "svg/Menu/Closure.svg",      "Ctrl+W" },
            { "file.saveAs",     "Save As",              "svg/Menu/SaveAs.svg",       "Ctrl+Shift+S" },
            { "file.saveAll",    "Save All",             "svg/Menu/SaveAll.svg" },
            { "file.import",     "Import",               "svg/Menu/Import.svg" },
            { "file.export",     "Export",               "svg/Menu/Export.svg" },
            { "file.send",       "Document Management",  "svg/Menu/SendToJobManager.svg" },
            { "file.graphLib",   "My graphics libary",   "svg/GraphicsLibrary.svg" },
            { "file.collect",    "My Favorite",          "svg/Menu/Collect.svg" },
            { "file.fileOption", "My Configuration",     "svg/Menu/MyConfiguration.svg" },
            { "file.exit",       "Exit",                 "svg/Menu/Quit.svg" },

            // Edit
            { "edit.cut",           "Cut",            "svg/Menu/Cut.svg",            "Ctrl+X" },
            { "edit.copy",          "Copy",           "svg/Menu/Copy.svg",           "Ctrl+C" },
            { "edit.paste",         "Paste",          "svg/Menu/PasteAtCursorPosition.svg", "Ctrl+V" },
            { "edit.delete",        "Delete",         "svg/Menu/Delete.svg",         "Delete" },
            { "edit.selectAll",     "Select All",     "svg/Menu/SelectAll.svg",     "Ctrl+A" },
            { "edit.selectReverse", "Invert Select",  "svg/Menu/ReverseElection.svg" },
            { "edit.group.group",    "Group",       "svg/Group.svg" },
            { "edit.group.unGroup",  "UnGroup",     "svg/Disband.svg" },
            { "edit.group.unGroupAll", "UnGroup All", "svg/DisbandAll.svg" },
            { "edit.mirror.vertical", "Vertical Mirror", "svg/VerticalMirror.svg" },
            { "edit.mirror.horizon",  "Horizon Mirror",  "svg/HorizontalMirroring.svg" },
            { "edit.mirror.angle",    "Angle Mirror",    "svg/AngularMirroring.svg" },

            // 旋转/翻转 无图标
            { "edit.rotate.left",  "Rotate 90 Anticlockwise", "" },
            { "edit.rotate.right", "Rotate 90 Clockwise",     "" },
            { "edit.rotate.flipH", "Horizon Flip",            "" },
            { "edit.rotate.flipV", "Vertical Flip",           "" },

            { "edit.align.left",     "Left-Aligned",        "svg/AlignLeft.svg" },
            { "edit.align.right",    "Right-Aligned",       "svg/AlignRight.svg" },
            { "edit.align.top",      "Top-Aligned",         "svg/AlignTop.svg" },
            { "edit.align.bottom",   "Bottom-Aligned",      "svg/AlignBottom.svg" },
            { "edit.align.horizon",  "Horizontal-Centered", "svg/HorizontallyCentered.svg" },
            { "edit.align.vertical", "Vertical-Centered",   "svg/VerticallyCentered.svg" },
            { "edit.align.center",   "Center-Aligned",      "svg/CenterAlignmentGeneral.svg" },

            { "edit.move.center", "Move Shapes To Center Of Page",        "svg/MoveToCenterOfPage.svg" },
            { "edit.move.lb",     "Move Shapes To Bottom-Left Corner",    "svg/MoveToTheBottomLeftCornerOfThePage.svg" },
            { "edit.move.rb",     "Move Shapes To Bottom-Right Corner",   "svg/MoveToTheBottomRightCornerOfThePage.svg" },
            { "edit.move.lt",     "Move Shapes To Top-Left Corner",       "svg/MoveToTheTopLeftCornerOfThePage.svg" },
            { "edit.move.rt",     "Move Shapes To Top-Right Corner",      "svg/MoveToTheTopRightCornerOfThePage.svg" },
            { "edit.move.left",   "Move Shapes To Left Side",             "svg/MoveLeftGeneral.svg" },
            { "edit.move.right",  "Move Shapes To Right Side",            "svg/MoveRightGeneral.svg" },
            { "edit.move.top",    "Move Shapes To Top Side",              "svg/MoveTopGeneral.svg" },
            { "edit.move.bottom", "Move Shapes To Bottom Side",           "svg/MoveBottomGeneral.svg" },

            { "edit.equi.width",      "Width Equal",             "svg/MandatoryEqualWidth.svg" },
            { "edit.equi.height",     "Height Equal",            "svg/MandatoryContour.svg" },
            { "edit.equi.hCenter",    "H-Center Distribute",     "svg/VerticalCenterEquidistant.svg" },
            { "edit.equi.hDistribute", "H-Distribute",           "svg/HorizontalCenterIsometric.svg" },
            { "edit.equi.vSpace",     "Vertical Space Equal",    "svg/VerticallyEquallySpaced.svg" },
            { "edit.equi.hSpace",     "Horizon Space Equal",     "svg/HorizontallyEquallySpaced.svg" },

            { "edit.zoom.page",     "Zoom To Page",  "svg/ZoomInToPage.svg",     "Z" },
            { "edit.zoom.selected", "Zoom Selected", "svg/PartialZoom.svg" },
            { "edit.zoom.in",       "Zoom In",       "svg/Enlarge.svg" },
            { "edit.zoom.out",      "Zoom Out",      "svg/ZoomOut.svg" },
            { "edit.zoom.all",      "Show All",      "svg/FitToCanvasRegular.svg", "X" },

            { "edit.moveHead.center", "Move The Cutting Head To The Center",     "svg/MoveToCenterOfPage.svg" },
            { "edit.moveHead.lb",     "Move The Cutting Head To The Bottom Left","svg/MoveToBottomLeft.svg" },
            { "edit.moveHead.rb",     "Move The Cutting Head To The Bottom-Right","svg/MoveToBottomRight.svg" },
            { "edit.moveHead.lt",     "Move The Cutting Head To The Top-Left",   "svg/MoveToUpperLeftCorner.svg" },
            { "edit.moveHead.rt",     "Move The Cutting Head To The Top Right",  "svg/MoveToTopRightCorner.svg" },

            { "edit.measure",     "Measure",           "svg/Measurement.svg" },
            { "edit.showSpecial", "Show Special Item", "svg/ShowOpenGraph.svg" },

            { "edit.pathOption.cutSeq",   "Show cut sequence",     "" },
            { "edit.pathOption.idlePath", "Show fast feed path",   "" },
            { "edit.pathOption.cutDir",   "Show cut direction",    "" },
            { "edit.pathOption.showAll",  "Show/Hide all",         "" },
            { "edit.options", "Options", "svg/Menu/FirstChoice.svg" },

            // Draw
            { "draw.select",          "Select Shapes",      "svg/Menu/Choose3.svg",      "A", true },
            { "draw.line.line",       "Line",               "svg/Pen.svg",               "L", true },
            { "draw.line.ellipse",    "Ellipse",            "svg/Ellipse.svg",            "E", true },
            { "draw.line.circle",     "Circle",             "svg/Circle.svg",             "C", true },
            { "draw.line.polygon",    "Polygon",            "svg/Polygon.svg",            "G", true },
            { "draw.line.rectangle",  "Rectangle",          "svg/Rectangle.svg",          "R", true },
            { "draw.line.star",       "Star",               "svg/Star.svg",               "S", true },
            { "draw.line.point",      "Point",              "svg/Point.svg",              "P", true },
            { "draw.text",            "Text",               "svg/Text.svg",               "T" },
            { "draw.qrcode",          "QR",                 "svg/Menu/QRCode3.svg",       "Q" },
            { "draw.array.matrix",    "Matrix Array",       "svg/RealArray.svg" },
            { "draw.array.circle",    "Circular Array",     "svg/CircularRow.svg" },
            { "draw.offset",          "Offset",             "svg/Menu/GraphicOffset3.svg" },
            { "draw.bool.weld",       "Weld",               "svg/FusionWelding.svg" },
            { "draw.bool.intersect",  "Boolean Intersection", "svg/BooleanAndOperations.svg" },
            { "draw.bool.union",      "Boolean Union",      "svg/BooleanSumOperation.svg" },
            { "draw.bool.subAB",      "Boolean Substraction(A-B)", "svg/BooleanSubtractionAB.svg" },
            { "draw.bool.subBA",      "Boolean Substraction(B-A)", "svg/BooleanSubtractionBA.svg" },
            { "draw.nodeEdit",        "Node Edit",          "svg/Menu/NodeEditing3.svg" },
            { "draw.chamfer.round",   "Round Chamfer",      "svg/Round.svg" },
            { "draw.chamfer.linear",  "Linear Chamfer",     "svg/Chamfer.svg" },
            { "draw.startPoint",      "Edit Start Point",   "svg/Menu/MachiningStartDirectionEdit3.svg" },
            { "draw.link.lead",       "Lead Line",          "svg/Lead.svg" },
            { "draw.link.bridge",     "Bridge",             "svg/Bridge.svg" },
            { "draw.link.micro",      "Mirco Joint",        "svg/MicroEven.svg" },
            { "draw.link.coedge",     "CoEdge",             "svg/TotalOfEdge.svg" },
            { "draw.bitmap.vec",      "Image Vectorization","svg/BitmapEdge.svg" },
            { "draw.bitmap.trim",     "Image Trimming",     "svg/BitmapClipping.svg" },
            { "draw.bitmap.process",  "Image Processing",   "svg/ImageProcessingRegular.svg" },
            { "draw.grid",            "Generate Correction Lines", "svg/Menu/GenerateGridlines3.svg" },

            // Setting
            { "setting.system",     "System Settings",   "svg/Menu/setting/SystemSettings.svg" },
            { "setting.file",       "File Setting",      "svg/Menu/setting/FileSettings.svg" },
            { "setting.factory",    "Factory Settings",  "svg/Menu/setting/DeviceSettings.svg" },
            { "setting.user",       "User Setting",      "svg/Menu/setting/UserParameters.svg" },

            // Handle
            { "handle.text2Curve", "Trans Text To Path",        "svg/Menu/solution/TextConversion.svg" },
            { "handle.textSplit",  "Text Split",                "svg/Menu/solution/TextSplit.svg" },
            { "handle.vec2Bmp",    "Vector To Bitmap",          "svg/Menu/solution/VectorTranspositionMap.svg" },
            { "handle.vecSmooth",  "Vector Smooth",             "svg/Menu/solution/CurveSmoothing.svg" },
            { "handle.pathPlan",   "Path Planning",             "svg/Menu/solution/PathOptimization.svg" },
            { "handle.itemHandle", "Item Handle",               "svg/Menu/solution/MergeConnectedLines.svg" },
            { "handle.nest",       "Nest",                      "svg/Nest/NestingMenuBarB.svg" },

            // Debug（旧项目全部 QIcon()，保持无图标）
            { "debug.monitor",     "Controller monitoring",             "" },
            { "debug.backlash",    "Machine Backlash Compensation Test","" },

            // Function
            { "function.rotary",       "Rotary Function",      "svg/Menu/function/RotationFunction.svg" },
            { "function.license",      "License Management",   "svg/Menu/function/LicenseManagement.svg" },
            { "function.feed",         "Feeding",              "svg/Menu/function/FeedProcessing.svg" },
            { "function.overCut",      "Oversize Cutting",     "svg/Menu/function/SuperFormatCutting.svg" },
            { "function.multiShift",   "Multi-Heads Shift",    "svg/Menu/function/DoubleHeadShift.svg" },
            { "function.plc",          "PLC",                  "svg/Menu/function/PLCFunction.svg" },
            { "function.wireless",     "Wireless Panel",       "svg/Menu/function/WirelessHandle.svg" },

            // Simulation
            { "simulation.soft",   "Software Simulation", "svg/Menu/SoftwareSimulation.svg" },
            { "simulation.motion", "Motion Simulation",   "svg/Menu/MachiningSimulation.svg" },

            // Help
            { "help.about",        "About MantiSoft",       "svg/Menu/help/AboutMantiSOFT.svg" },
            { "help.notes",        "My Notes",              "svg/Menu/help/MyNotes.svg" },
            { "help.feedback",     "Problem Feedback",      "svg/Menu/help/HelpAndFeedback.svg" },
            { "help.help",         "Help",                  "svg/Menu/help/HelpSystem.svg", "F1" },
            { "help.checkUpdate",  "Check Updates",         "svg/Menu/help/CheckForUpdates.svg" },
            { "help.menuManage",   "Menu Management",       "svg/Menu/help/MenuManagementItems.svg" },
        };

        for (const LegacyCommand& cmd : kLegacyCommands)
        {
            const QString id = QString::fromLatin1(cmd.id);
            const QString iconPath = (cmd.icon && cmd.icon[0])
                ? (ICON_DIR + QString::fromLatin1(cmd.icon)) : QString();

            // 模式命令执行时只切换当前工具；勾选态由 checkedWhen 统一推导，
            // 菜单、顶部工具栏、左侧栏因此共享同一份高亮与互斥状态。
            AA::CommandTable::Handler handler;
            if (cmd.isMode)
                handler = [this, id](const QJsonObject&) { m_activeTool = id; };
            // 文档状态控制：new/open 置 true，close/exit 重置
            if (id == QStringLiteral("file.close"))
                handler = [this](const QJsonObject&) { m_hasDocument = false; };
            if (id == QStringLiteral("file.exit"))
                handler = [this](const QJsonObject&) { m_hasDocument = false; };

            // 先登记基础元数据；titleKey 统一用 "command.<id>" 格式，便于翻译文件按 context=command 组织。
            auto& reg = add(id, handler)
                .title(QStringLiteral("command.") + id)
                .icon(iconPath);

            if (cmd.shortcut && cmd.shortcut[0])
                reg.shortcut(QString::fromLatin1(cmd.shortcut));

            if (cmd.isMode)
                reg.checkedWhen([this, id]() { return m_activeTool == id; });
        }
    }
}  // namespace MS