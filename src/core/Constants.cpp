#include "Constants.h"

namespace Hearthlight {

QString userAgent()
{
    return QStringLiteral("hearthlight/%1 (contact: https://github.com/hearthlight/launcher)")
        .arg(QString::fromLatin1(kAppVersion));
}

} // namespace Hearthlight
