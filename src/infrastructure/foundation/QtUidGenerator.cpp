#include "infrastructure/foundation/QtUidGenerator.h"

#include <QUuid>

namespace PersonOS::Infrastructure {

Domain::Uid QtUidGenerator::next()
{
    const auto text = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    return Domain::Uid::parse(text).value();
}

} // namespace PersonOS::Infrastructure
