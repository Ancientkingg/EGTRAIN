#ifndef COUNTTEXT_H
#define COUNTTEXT_H

#include <QString>

/// A count with the noun that fits it, such as "1 error" or "2 errors".
inline QString countText(long long count, const QString& singular, const QString& plural) {
	return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? singular : plural);
}

#endif // COUNTTEXT_H
