#include "UiShell/Model/IProfileLoader.h"

#include "Model/Json/JsoncInternal.h"
#include "ProfileParser.h"
#include "ProfileValidator.h"

namespace RD
{
    namespace
    {
        // 导出只暴露 IProfileLoader；静态类保留为内部实现细节。
        class ProfileLoader final : public IProfileLoader
        {
        public:
            ProfileLoadResult loadFromJson(const QByteArray& utf8Json) override
            {
                return ProfileParser::loadFromJson(utf8Json);
            }

            QList<Diagnostic> validate(const ProfileModel& model) override
            {
                return ProfileValidator::validate(model);
            }

            JsoncStripResult stripComments(const QByteArray& input) const override
            {
                return stripJsoncComments(input);
            }

            void lineColumn(const QByteArray& utf8, int offset, int* line, int* column) const override
            {
                jsoncLineColumn(utf8, offset, line, column);
            }

            void destroy() override
            {
                delete this;
            }
        };
    }  // namespace

    IProfileLoader* createProfileLoader()
    {
        return new ProfileLoader;
    }
}  // namespace RD