#include "ayu/features/admin_panel/admin_api.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QTcpSocket>

namespace AyuFeatures::AdminPanel {
namespace {

constexpr auto kServer = "chickenbot";
constexpr auto kRemotePort = 8765;
constexpr auto kLocalPort = 18765;
constexpr auto kRequestTimeout = 25 * 1000;
constexpr auto kPortAttempts = 50;
constexpr auto kPortRetryDelay = 300;

struct Waiter {
	Fn<void()> done;
	Fn<void(QString)> fail;
};

struct State {
	QString key;
	QPointer<QProcess> tunnel;
	bool connecting = false;
	std::vector<Waiter> waiting;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] QNetworkAccessManager *Manager() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return result;
}

[[nodiscard]] QString SshProgram() {
	const auto system = QDir::fromNativeSeparators(
		qEnvironmentVariable("SystemRoot")) + u"/System32/OpenSSH/ssh.exe"_q;
	return QFile::exists(system) ? system : u"ssh"_q;
}

[[nodiscard]] QString KeyPath() {
	return QDir::homePath() + u"/.ssh/chickenbot_oracle"_q;
}

[[nodiscard]] QStringList SshArguments() {
	return {
		u"-i"_q,
		QDir::toNativeSeparators(KeyPath()),
		u"-o"_q,
		u"BatchMode=yes"_q,
		u"-o"_q,
		u"ConnectTimeout=15"_q,
		u"-o"_q,
		u"StrictHostKeyChecking=accept-new"_q,
	};
}

void Finish(const QString &error) {
	auto &state = Current();
	state.connecting = false;
	for (const auto &waiter : base::take(state.waiting)) {
		if (error.isEmpty()) {
			waiter.done();
		} else {
			waiter.fail(error);
		}
	}
}

[[nodiscard]] bool TunnelAlive() {
	const auto &tunnel = Current().tunnel;
	return tunnel && (tunnel->state() != QProcess::NotRunning);
}

void WaitPort(int attempt) {
	const auto socket = new QTcpSocket(QCoreApplication::instance());
	QObject::connect(socket, &QTcpSocket::connected, [=] {
		socket->deleteLater();
		Finish(QString());
	});
	QObject::connect(socket, &QTcpSocket::errorOccurred, [=] {
		socket->deleteLater();
		if (!TunnelAlive()) {
			Finish(u"Туннель к серверу закрылся. Проверь интернет "
				"и что бот работает."_q);
		} else if (attempt >= kPortAttempts) {
			Finish(u"Панель на сервере не отвечает."_q);
		} else {
			QTimer::singleShot(kPortRetryDelay, [=] {
				WaitPort(attempt + 1);
			});
		}
	});
	socket->connectToHost(u"127.0.0.1"_q, kLocalPort);
}

void StartTunnel() {
	auto &state = Current();
	if (TunnelAlive()) {
		WaitPort(0);
		return;
	}
	const auto process = new QProcess(QCoreApplication::instance());
	state.tunnel = process;
	QObject::connect(
		process,
		qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
		process,
		&QObject::deleteLater);
	auto arguments = SshArguments();
	arguments
		<< u"-N"_q
		<< u"-o"_q
		<< u"ExitOnForwardFailure=yes"_q
		<< u"-o"_q
		<< u"ServerAliveInterval=30"_q
		<< u"-L"_q
		<< u"127.0.0.1:%1:127.0.0.1:%2"_q.arg(kLocalPort).arg(kRemotePort)
		<< QString::fromLatin1(kServer);
	process->start(SshProgram(), arguments);
	WaitPort(0);
}

void FetchKey() {
	const auto process = new QProcess(QCoreApplication::instance());
	QObject::connect(
		process,
		qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
		[=](int code, QProcess::ExitStatus status) {
			const auto key = QString::fromUtf8(
				process->readAllStandardOutput()).trimmed();
			const auto error = QString::fromUtf8(
				process->readAllStandardError()).trimmed();
			process->deleteLater();
			if (status != QProcess::NormalExit || code || key.isEmpty()) {
				Finish(u"Не получилось подключиться к серверу по SSH "
					"(Host chickenbot в ~/.ssh/config)"_q
					+ (error.isEmpty() ? QString() : (u": "_q + error.left(200))));
				return;
			}
			Current().key = key;
			StartTunnel();
		});
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			process->deleteLater();
			Finish(u"Не нашёл ssh.exe — нужен OpenSSH для Windows."_q);
		}
	});
	auto arguments = SshArguments();
	arguments
		<< QString::fromLatin1(kServer)
		<< u"cat ~/ChickenBot/admin_key.txt"_q;
	process->start(SshProgram(), arguments);
}

void Connect(Fn<void()> done, Fn<void(QString)> fail) {
	auto &state = Current();
	state.waiting.push_back({ std::move(done), std::move(fail) });
	if (state.connecting) {
		return;
	}
	state.connecting = true;
	if (!QFile::exists(KeyPath())) {
		Finish(u"Нет SSH-ключа %1 — панель работает только на ПК "
			"владельца бота."_q.arg(QDir::toNativeSeparators(KeyPath())));
	} else if (state.key.isEmpty()) {
		FetchKey();
	} else {
		StartTunnel();
	}
}

using RawDone = Fn<void(QByteArray data, QString disposition)>;

void Send(
		const QString &path,
		const QByteArray &body,
		bool post,
		RawDone done,
		Fn<void(QString)> fail,
		bool retried) {
	auto request = QNetworkRequest(QUrl(
		u"http://127.0.0.1:%1/api/%2"_q.arg(kLocalPort).arg(path)));
	request.setRawHeader("X-Admin", "1");
	request.setRawHeader("X-Admin-Key", Current().key.toUtf8());
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setTransferTimeout(kRequestTimeout);
	const auto reply = post
		? Manager()->post(request, body)
		: Manager()->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto error = reply->error();
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto data = reply->readAll();
		const auto resend = [=] {
			Send(path, body, post, done, fail, true);
		};
		if (!retried && error == QNetworkReply::ConnectionRefusedError) {
			Connect(resend, fail);
			return;
		} else if (!retried && status == 401) {
			Current().key = QString();
			Connect(resend, fail);
			return;
		}
		if (error == QNetworkReply::NoError) {
			done(data, QString::fromUtf8(
				reply->rawHeader("Content-Disposition")));
			return;
		}
		const auto document = QJsonDocument::fromJson(data);
		const auto message = document.isObject()
			? document.object().value(u"error"_q).toString()
			: QString();
		fail(message.isEmpty()
			? (u"Сервер не ответил: "_q + reply->errorString())
			: message);
	});
}

void Request(
		const QString &path,
		const QByteArray &body,
		bool post,
		RawDone done,
		Fn<void(QString)> fail) {
	const auto send = [=] {
		Send(path, body, post, done, fail, false);
	};
	if (Current().key.isEmpty()) {
		Connect(send, fail);
	} else {
		send();
	}
}

[[nodiscard]] RawDone ParseJson(Fn<void(QJsonObject)> done) {
	return [=](QByteArray data, QString) {
		const auto document = QJsonDocument::fromJson(data);
		done(document.isObject() ? document.object() : QJsonObject());
	};
}

} // namespace

bool Available() {
	return QFile::exists(KeyPath());
}

void Get(
		const QString &path,
		Fn<void(QJsonObject)> done,
		Fn<void(QString)> fail) {
	Request(path, QByteArray(), false, ParseJson(std::move(done)), fail);
}

void Post(
		const QString &path,
		const QJsonObject &body,
		Fn<void(QJsonObject)> done,
		Fn<void(QString)> fail) {
	Request(
		path,
		QJsonDocument(body).toJson(QJsonDocument::Compact),
		true,
		ParseJson(std::move(done)),
		std::move(fail));
}

void Download(
		const QString &path,
		Fn<void(QByteArray data, QString name)> done,
		Fn<void(QString)> fail) {
	Request(path, QByteArray(), false, [=](
			QByteArray data,
			QString disposition) {
		const auto match = QRegularExpression(
			u"filename=\"([^\"]+)\""_q).match(disposition);
		done(data, match.hasMatch() ? match.captured(1) : u"chicken.db.gz"_q);
	}, std::move(fail));
}

} // namespace AyuFeatures::AdminPanel
