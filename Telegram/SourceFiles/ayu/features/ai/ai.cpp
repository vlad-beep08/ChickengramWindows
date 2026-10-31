#include "ayu/features/ai/ai.h"

#include "ayu/features/local_store/local_store.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#endif

namespace AyuFeatures::Ai {
namespace {

constexpr auto kTimeout = 90 * 1000;
constexpr auto kClaudeVersion = "2026-10-21";
constexpr auto kClaudeFallbackBeta = "server-side-fallback-2026-07-01";
constexpr auto kCheckTokens = 64;

[[nodiscard]] QString StoreName() {
	return u"ai"_q;
}

[[nodiscard]] QString ProviderId(Provider provider) {
	switch (provider) {
	case Provider::Groq: return u"groq"_q;
	case Provider::OpenAI: return u"openai"_q;
	case Provider::Claude: return u"claude"_q;
	case Provider::OpenRouter: return u"openrouter"_q;
	case Provider::Custom: return u"custom"_q;
	}
	Unexpected("Provider in AyuFeatures::Ai::ProviderId.");
}

[[nodiscard]] Provider ProviderFromId(const QString &id) {
	for (const auto provider : Providers()) {
		if (ProviderId(provider) == id) {
			return provider;
		}
	}
	return Provider::Groq;
}

[[nodiscard]] QByteArray Protect(const QByteArray &data) {
#ifdef Q_OS_WIN
	auto input = DATA_BLOB{
		DWORD(data.size()),
		reinterpret_cast<BYTE*>(const_cast<char*>(data.constData())),
	};
	auto output = DATA_BLOB();
	if (!CryptProtectData(
			&input,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			CRYPTPROTECT_UI_FORBIDDEN,
			&output)) {
		return QByteArray();
	}
	const auto result = QByteArray(
		reinterpret_cast<const char*>(output.pbData),
		int(output.cbData));
	LocalFree(output.pbData);
	return result;
#else
	return data;
#endif
}

[[nodiscard]] QByteArray Unprotect(const QByteArray &data) {
#ifdef Q_OS_WIN
	auto input = DATA_BLOB{
		DWORD(data.size()),
		reinterpret_cast<BYTE*>(const_cast<char*>(data.constData())),
	};
	auto output = DATA_BLOB();
	if (!CryptUnprotectData(
			&input,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			CRYPTPROTECT_UI_FORBIDDEN,
			&output)) {
		return QByteArray();
	}
	const auto result = QByteArray(
		reinterpret_cast<const char*>(output.pbData),
		int(output.cbData));
	LocalFree(output.pbData);
	return result;
#else
	return data;
#endif
}

[[nodiscard]] Config Load() {
	auto result = Config();
	const auto json = LocalStore::Read(StoreName());
	result.provider = ProviderFromId(
		QString::fromStdString(json.value("provider", std::string())));
	result.model = QString::fromStdString(json.value("model", std::string()));
	result.url = QString::fromStdString(json.value("url", std::string()));
	const auto stored = QByteArray::fromBase64(
		QByteArray::fromStdString(json.value("key", std::string())));
	result.key = stored.isEmpty()
		? QString()
		: QString::fromUtf8(Unprotect(stored));
	return result;
}

[[nodiscard]] Config &Cached() {
	static auto result = Load();
	return result;
}

[[nodiscard]] QNetworkAccessManager *Manager() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return result;
}

[[nodiscard]] QString Endpoint(const Config &config) {
	switch (config.provider) {
	case Provider::Groq:
		return u"https://api.groq.com/openai/v1/chat/completions"_q;
	case Provider::OpenAI:
		return u"https://api.openai.com/v1/chat/completions"_q;
	case Provider::Claude:
		return u"https://api.anthropic.com/v1/messages"_q;
	case Provider::OpenRouter:
		return u"https://openrouter.ai/api/v1/chat/completions"_q;
	case Provider::Custom: {
		auto url = config.url.trimmed();
		while (url.endsWith(QChar('/'))) {
			url.chop(1);
		}
		return url.endsWith(u"/chat/completions"_q)
			? url
			: (url + u"/chat/completions"_q);
	}
	}
	Unexpected("Provider in AyuFeatures::Ai::Endpoint.");
}

[[nodiscard]] QString ErrorText(
		const QJsonObject &root,
		int status,
		const QString &fallback) {
	const auto error = root.value(u"error"_q);
	const auto message = error.isObject()
		? error.toObject().value(u"message"_q).toString()
		: error.toString();
	if (status == 401 || status == 403) {
		return u"Ключ не подошёл: сервис ответил «%1»."_q.arg(
			message.isEmpty() ? QString::number(status) : message);
	} else if (status == 429) {
		return u"Слишком много запросов к ИИ — подожди минуту."_q;
	}
	return message.isEmpty()
		? (u"ИИ не ответил: "_q + fallback)
		: (u"ИИ ответил ошибкой: "_q + message);
}

[[nodiscard]] QString ClaudeText(const QJsonObject &root) {
	auto result = QString();
	for (const auto &block : root.value(u"content"_q).toArray()) {
		const auto object = block.toObject();
		if (object.value(u"type"_q).toString() == u"text"_q) {
			result += object.value(u"text"_q).toString();
		}
	}
	return result;
}

[[nodiscard]] QString ChatText(const QJsonObject &root) {
	const auto choices = root.value(u"choices"_q).toArray();
	if (choices.isEmpty()) {
		return QString();
	}
	return choices.first().toObject()
		.value(u"message"_q).toObject()
		.value(u"content"_q).toString();
}

[[nodiscard]] QNetworkRequest PrepareRequest(
		const Config &config,
		const Request &request,
		QByteArray &body) {
	auto result = QNetworkRequest(QUrl(Endpoint(config)));
	result.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	result.setTransferTimeout(kTimeout);
	const auto key = config.key.trimmed().toUtf8();
	auto json = QJsonObject();
	json.insert(u"model"_q, ModelOf(config));
	json.insert(u"max_tokens"_q, request.maxTokens);
	auto user = QJsonObject();
	user.insert(u"role"_q, u"user"_q);
	user.insert(u"content"_q, request.user);
	if (config.provider == Provider::Claude) {
		result.setRawHeader("x-api-key", key);
		result.setRawHeader("anthropic-version", kClaudeVersion);
		result.setRawHeader("anthropic-beta", kClaudeFallbackBeta);
		if (!request.system.isEmpty()) {
			json.insert(u"system"_q, request.system);
		}
		json.insert(u"messages"_q, QJsonArray{ user });
		json.insert(u"fallbacks"_q, u"default"_q);
	} else {
		if (!key.isEmpty()) {
			result.setRawHeader("Authorization", "Bearer " + key);
		}
		auto messages = QJsonArray();
		if (!request.system.isEmpty()) {
			auto system = QJsonObject();
			system.insert(u"role"_q, u"system"_q);
			system.insert(u"content"_q, request.system);
			messages.push_back(system);
		}
		messages.push_back(user);
		json.insert(u"messages"_q, messages);
	}
	body = QJsonDocument(json).toJson(QJsonDocument::Compact);
	return result;
}

[[nodiscard]] Ui::PasswordInput *AddKeyField(
		not_null<Ui::GenericBox*> box,
		const QString &value) {
	const auto &st = st::defaultInputField;
	auto container = object_ptr<Ui::RpWidget>(box);
	container->resize(container->width(), st.heightMin);
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		container.data(),
		st,
		rpl::single(u"Ключ API"_q),
		value);
	container->widthValue(
	) | rpl::on_next([=](int width) {
		field->resize(width, field->height());
		field->moveToLeft(0, 0);
	}, container->lifetime());
	box->addRow(std::move(container));
	return field;
}

[[nodiscard]] TextWithEntities GroqHelp() {
	auto result = TextWithEntities{
		u"Бесплатный ключ Groq: войди на "_q,
	};
	result.append(Ui::Text::Link(
		u"console.groq.com/keys"_q,
		u"https://console.groq.com/keys"_q));
	result.append(u", нажми «Create API Key», скопируй ключ и вставь его сюда."_q);
	return result;
}

} // namespace

Config Current() {
	return Cached();
}

void Save(const Config &config) {
	Cached() = config;
	const auto key = config.key.trimmed();
	const auto sealed = key.isEmpty()
		? QByteArray()
		: Protect(key.toUtf8()).toBase64();
	LocalStore::Write(StoreName(), {
		{ "provider", ProviderId(config.provider).toStdString() },
		{ "model", config.model.trimmed().toStdString() },
		{ "url", config.url.trimmed().toStdString() },
		{ "key", sealed.toStdString() },
	});
}

bool Ready() {
	const auto &config = Cached();
	return (config.provider == Provider::Custom)
		? !config.url.trimmed().isEmpty()
		: !config.key.trimmed().isEmpty();
}

std::vector<Provider> Providers() {
	return {
		Provider::Groq,
		Provider::OpenAI,
		Provider::Claude,
		Provider::OpenRouter,
		Provider::Custom,
	};
}

QString ProviderName(Provider provider) {
	switch (provider) {
	case Provider::Groq: return u"Groq (бесплатный)"_q;
	case Provider::OpenAI: return u"OpenAI"_q;
	case Provider::Claude: return u"Claude"_q;
	case Provider::OpenRouter: return u"OpenRouter"_q;
	case Provider::Custom: return u"Свой адрес (совместимый с OpenAI)"_q;
	}
	Unexpected("Provider in AyuFeatures::Ai::ProviderName.");
}

QString DefaultModel(Provider provider) {
	switch (provider) {
	case Provider::Groq: return u"llama-3.3-70b-versatile"_q;
	case Provider::OpenAI: return u"gpt-4o-mini"_q;
	case Provider::Claude: return u"claude-opus-5-5"_q;
	case Provider::OpenRouter: return u"openrouter/auto"_q;
	case Provider::Custom: return QString();
	}
	Unexpected("Provider in AyuFeatures::Ai::DefaultModel.");
}

QString ModelOf(const Config &config) {
	const auto model = config.model.trimmed();
	return model.isEmpty() ? DefaultModel(config.provider) : model;
}

void Ask(
		const Config &config,
		Request request,
		Fn<void(QString)> done,
		Fn<void(QString)> fail) {
	if (config.provider == Provider::Custom) {
		if (config.url.trimmed().isEmpty()) {
			fail(u"Укажи адрес сервиса в настройках ИИ."_q);
			return;
		}
	} else if (config.key.trimmed().isEmpty()) {
		fail(u"Сначала добавь ключ ИИ: Настройки → Чикенграм → ИИ."_q);
		return;
	}
	auto body = QByteArray();
	const auto http = PrepareRequest(config, request, body);
	const auto claude = (config.provider == Provider::Claude);
	const auto reply = Manager()->post(http, body);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto root = QJsonDocument::fromJson(reply->readAll()).object();
		if (reply->error() != QNetworkReply::NoError) {
			fail(ErrorText(root, status, reply->errorString()));
			return;
		} else if (claude
			&& root.value(u"stop_reason"_q).toString() == u"refusal"_q) {
			fail(u"Модель отказалась отвечать на этот запрос."_q);
			return;
		}
		const auto text = (claude ? ClaudeText(root) : ChatText(root))
			.trimmed();
		if (text.isEmpty()) {
			fail(u"ИИ прислал пустой ответ."_q);
		} else {
			done(text);
		}
	});
}

void Ask(Request request, Fn<void(QString)> done, Fn<void(QString)> fail) {
	Ask(Current(), std::move(request), std::move(done), std::move(fail));
}

void SettingsBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(u"ИИ в Чикенграме"_q));
	box->setWidth(st::boxWideWidth);

	const auto config = Current();
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(u"Нужен для досье на собеседника. Ключ хранится только на этом компьютере, в зашифрованном виде, и уходит только выбранному сервису."_q),
		st::boxLabel));
	box->addSkip(st::boxOptionListSkip);

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		int(config.provider));
	for (const auto provider : Providers()) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				int(provider),
				ProviderName(provider),
				st::defaultBoxCheckbox),
			style::margins(
				st::boxRowPadding.left(),
				0,
				st::boxRowPadding.right(),
				st::boxOptionListSkip / 2));
	}
	box->addSkip(st::boxOptionListSkip / 2);

	const auto key = AddKeyField(box, config.key);
	const auto model = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(u"Модель (пусто — по умолчанию)"_q),
		config.model));
	const auto url = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::InputField>>(
			box,
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				rpl::single(u"Адрес, например http://localhost:11434/v1"_q),
				config.url)));
	url->toggleOn(group->value() | rpl::map([](int value) {
		return (value == int(Provider::Custom));
	}));
	box->addSkip(st::boxOptionListSkip / 2);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		group->value() | rpl::map([](int value) {
			const auto fallback = DefaultModel(Provider(value));
			return fallback.isEmpty()
				? u"Для своего адреса укажи модель вручную."_q
				: (u"Модель по умолчанию: "_q + fallback);
		}),
		st::boxDividerLabel));
	box->addSkip(st::boxOptionListSkip / 2);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(GroqHelp()),
		st::boxDividerLabel));

	const auto collect = [=] {
		auto result = Config();
		result.provider = Provider(group->current());
		result.key = key->getLastText().trimmed();
		result.model = model->getLastText().trimmed();
		result.url = url->entity()->getLastText().trimmed();
		return result;
	};
	box->setFocusCallback([=] {
		key->setFocusFast();
	});
	box->addButton(rpl::single(u"Сохранить"_q), [=] {
		Save(collect());
		box->showToast(u"Настройки ИИ сохранены."_q);
		box->closeBox();
	});
	box->addLeftButton(rpl::single(u"Проверить"_q), [=] {
		box->showToast(u"Спрашиваю ИИ…"_q);
		Ask(collect(), Request{
			.user = u"Ответь одним словом по-русски: работает."_q,
			.maxTokens = kCheckTokens,
		}, crl::guard(box, [=](QString text) {
			box->showToast(u"✅ ИИ отвечает: "_q + text.left(80));
		}), crl::guard(box, [=](QString error) {
			box->showToast(u"❌ "_q + error);
		}));
	});
	box->addButton(rpl::single(u"Отмена"_q), [=] {
		box->closeBox();
	});
}

void ShowSettings(std::shared_ptr<Ui::Show> show) {
	show->showBox(Box(SettingsBox));
}

} // namespace AyuFeatures::Ai
