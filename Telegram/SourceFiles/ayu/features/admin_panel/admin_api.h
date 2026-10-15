#pragma once

#include <QtCore/QJsonObject>

namespace AyuFeatures::AdminPanel {

[[nodiscard]] bool Available();

void Get(
	const QString &path,
	Fn<void(QJsonObject)> done,
	Fn<void(QString)> fail);
void Post(
	const QString &path,
	const QJsonObject &body,
	Fn<void(QJsonObject)> done,
	Fn<void(QString)> fail);
void Download(
	const QString &path,
	Fn<void(QByteArray data, QString name)> done,
	Fn<void(QString)> fail);

} // namespace AyuFeatures::AdminPanel
