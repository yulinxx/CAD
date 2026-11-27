#pragma once

#include <QWidget>

class QFrame;
class QToolButton;
class QVBoxLayout;

namespace RD
{
    class IShellHost;
}

namespace MS
{
    // 工具栏语义名 → Commands 命令 id；没有对应命令的返回空串。
    // 左侧栏与工具栏共用这一份映射，避免两处漂移。
    QString commandIdForToolId(const QString& toolId);

    // 左侧工具栏：窄竖条（~40px），全部为图标按钮。
    // 结构：顶部分隔线 + 主工具区（含组合菜单按钮）+ Stretch + 分隔线
    //       + 底部折叠区（Layer/Log）+ Expand/Contract。
    // 组合按钮使用 QToolButton::MenuButtonPopup，点击主体触发当前子项，点击右侧小三角展开子菜单；
    // 主按钮图标跟随当前选中的子项，与旧版 MenuToolBarButton 行为一致。
    class LeftToolBar : public QWidget
    {
        Q_OBJECT

    public:
        // host 用于取共享命令动作：工具按钮的勾选/图标/置灰由命令状态统一驱动。
        explicit LeftToolBar(RD::IShellHost* host = nullptr, QWidget* parent = nullptr);
        ~LeftToolBar() override = default;

    signals:
        // 按钮点击发出；toolId 取值示例：
        //   "select", "pan",
        //   "pen.line", "pen.ellipse", ..., "pen.point",
        //   "qrcode", "barcode",
        //   "artLibrary",
        //   "array", "circularArray",
        //   "offset",
        //   "bool.weld", "bool.intersect", ..., "bool.subBA",
        //   "nodeEdit",
        //   "chamfer.round", "chamfer.linear",
        //   "startPoint",
        //   "link.lead", "link.bridge", "link.micro", "link.coedge",
        //   "bmp.vector", "bmp.trim", "bmp.proc",
        //   "trim",
        //   "log", "layer"
        void sigToolButtonClicked(const QString& toolId);

    public slots:
        // 与左侧内容面板的实际显隐同步 Log/Layer 按钮的 checked 状态；传空串表示全部取消。
        void setPanelToolActive(const QString& toolId);

    private slots:
        void onExpandToggle();

    private:
        void buildTopButtons(QVBoxLayout* lay);
        void buildBottomButtons(QVBoxLayout* lay);

    private:
        RD::IShellHost* m_host = nullptr;
        QWidget* m_topSection = nullptr;
        QWidget* m_bottomSection = nullptr;
        QFrame* m_topLine = nullptr;      // 顶部分隔线，随主工具区一起显隐
        QFrame* m_bottomLine = nullptr;   // 底部折叠区上方的分隔线，随折叠区一起显隐
        QToolButton* m_expandBtn = nullptr;
        QToolButton* m_logBtn = nullptr;
        QToolButton* m_layerBtn = nullptr;
        QWidget* m_bottomSpacer = nullptr;   // 源项目底部固定 37px 留白，收起时一起隐藏
        bool m_bBottomExpanded = true;
    };
}  // namespace MS
