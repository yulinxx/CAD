#include "Model/Spec/WindowSpec.h"
#include "Platform/WindowFrame.h"

namespace AA
{
#if defined(Q_OS_WIN)
    std::unique_ptr<IWindowFrame> createWindowsWindowFrame();
#else
    std::unique_ptr<IWindowFrame> createQtWindowFrame();
#endif

    std::unique_ptr<IWindowFrame> createWindowFrame()
    {
#if defined(Q_OS_WIN)
        return createWindowsWindowFrame();
#else
        return createQtWindowFrame();
#endif
    }
}  // namespace AA
