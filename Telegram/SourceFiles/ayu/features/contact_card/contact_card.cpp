// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/contact_card/contact_card.h"

#include "lang_auto.h"
#include "apiwrap.h"
#include "ayu/features/ai/ai.h"
#include "ayu/features/local_store/local_store.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_birthday.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QRegularExpression>
#include <QtWidgets/QApplication>

namespace AyuFeatures::ContactCard {
namespace {

constexpr auto kSecondsInDay = 24 * 60 * 60;
constexpr auto kDossierPages = 3;
constexpr auto kDossierPageSize = 100;
constexpr auto kDossierTokens = 2048;
constexpr auto kSmallBudget = 9000;
constexpr auto kLargeBudget = 30000;
constexpr auto kBirthdayCheck = 30 * 60 * crl::time(1000);

struct Card {
	QString notes;
	QString tags;
	TimeId lastSent = 0;
	QString name;
	QString dossier;
	TimeId dossierAt = 0;
	QString birthday;
	int birthdayYear = 0;
	bool dossierConsent = false;
};

struct Line {
	MsgId id = 0;
	TimeId date = 0;
	bool out = false;
	QString text;
};

using Key = std::pair<uint64, uint64>;

[[nodiscard]] QString StoreName() {
	return u"contact_cards"_q;
}

[[nodiscard]] QString Read(const nlohmann::json &card, const char *key) {
	return QString::fromStdString(card.value(key, std::string()));
}

[[nodiscard]] std::map<Key, Card> Load() {
	auto result = std::map<Key, Card>();
	const auto json = LocalStore::Read(StoreName());
	for (const auto &[user, peers] : json.items()) {
		if (!peers.is_object()) {
			continue;
		}
		for (const auto &[peer, card] : peers.items()) {
			if (!card.is_object()) {
				continue;
			}
			auto key = Key();
			try {
				key = Key(std::stoull(user), std::stoull(peer));
			} catch (...) {
				continue;
			}
			result[key] = Card{
				.notes = Read(card, "notes"),
				.tags = Read(card, "tags"),
				.lastSent = card.value("lastSent", TimeId()),
				.name = Read(card, "name"),
				.dossier = Read(card, "dossier"),
				.dossierAt = card.value("dossierAt", TimeId()),
				.birthday = Read(card, "birthday"),
				.birthdayYear = card.value("birthdayYear", 0),
				.dossierConsent = card.value("dossierConsent", false),
			};
		}
	}
	return result;
}

[[nodiscard]] std::map<Key, Card> &All() {
	static auto result = Load();
	return result;
}

void Save() {
	auto json = nlohmann::json::object();
	for (const auto &[key, card] : All()) {
		json[std::to_string(key.first)][std::to_string(key.second)] = {
			{ "notes", card.notes.toStdString() },
			{ "tags", card.tags.toStdString() },
			{ "lastSent", card.lastSent },
			{ "name", card.name.toStdString() },
			{ "dossier", card.dossier.toStdString() },
			{ "dossierAt", card.dossierAt },
			{ "birthday", card.birthday.toStdString() },
			{ "birthdayYear", card.birthdayYear },
			{ "dossierConsent", card.dossierConsent },
		};
	}
	LocalStore::Write(StoreName(), json);
}

[[nodiscard]] Key KeyFor(not_null<PeerData*> peer) {
	return Key(peer->session().userId().bare, peer->id.value);
}

[[nodiscard]] QString FormatBirthday(const Data::Birthday &birthday) {
	auto result = QString::number(birthday.day()).rightJustified(2, u'0');
	result.append(u'.');
	result.append(QString::number(birthday.month()).rightJustified(2, u'0'));
	if (birthday.year()) {
		result.append(u'.');
		result.append(QString::number(birthday.year()));
	}
	return result;
}

[[nodiscard]] QString LastSentText(TimeId lastSent) {
	if (!lastSent) {
		return tr::ayu_ContactCardLastSentNever(tr::now);
	}
	const auto days = (base::unixtime::now() - lastSent) / kSecondsInDay;
	return days
		? tr::ayu_ContactCardLastSentDays(tr::now, lt_count, days)
		: tr::ayu_ContactCardLastSentToday(tr::now);
}

[[nodiscard]] QString DossierText(const Card &card) {
	if (card.dossier.isEmpty()) {
		return u"Досье ещё не собрано. ИИ прочитает последние сообщения и выпишет факты о собеседнике: даты, семью, работу, интересы и договорённости."_q;
	}
	const auto when = base::unixtime::parse(card.dossierAt);
	return card.dossier
		+ u"\n\nСобрано "_q
		+ when.toString(u"dd.MM.yyyy HH:mm"_q);
}

[[nodiscard]] QString SystemPrompt(const QString &name) {
	return u"Ты помогаешь вести личные заметки о собеседнике по имени «%1". По переписке выпиши только факты о нём (не о владельце, чьи сообщения подписаны «Я»). Группы: «🎂 Даты», «👨‍👩‍👧 Семья и близкие», «💼 Работа и учёба», «❤️ Интересы и вкусы», «🤝 Договорённости и обещания», «📝 Прочее». В каждой группе — короткие пункты, каждый с новой строки и с «• ». Пустые группы пропускай. Ничего не выдумывай; если фактов нет, напиши «Пока ничего не известно». Отвечай по-русски, без вступлений. Самой последней строкой напиши день рождения собеседника в виде «ДР: ДД.ММ», если он точно известен из переписки, иначе «ДР: -»."_q.arg(name);
}

[[nodiscard]] QString Transcript(
		std::vector<Line> lines,
		const QString &name,
		int budget) {
	ranges::sort(lines, ranges::greater(), &Line::id);
	auto picked = QStringList();
	auto used = 0;
	for (const auto &line : lines) {
		const auto when = base::unixtime::parse(line.date);
		const auto text = when.toString(u"dd.MM.yy"_q)
			+ u" "_q
			+ (line.out ? u"Я"_q : name)
			+ u": "_q
			+ line.text.simplified();
		if (used + text.size() > budget) {
			break;
		}
		used += text.size();
		picked.push_back(text);
	}
	std::reverse(picked.begin(), picked.end());
	return picked.join(u'\n');
}

void ParseLines(
		not_null<PeerData*> peer,
		const MTPmessages_Messages &result,
		std::vector<Line> &into,
		MsgId &lastId,
		int &count) {
	const auto list = [&]() -> const QVector<MTPMessage>* {
		switch (result.type()) {
		case mtpc_messages_messages: {
			const auto &data = result.c_messages_messages();
			peer->owner().processUsers(data.vusers());
			return &data.vmessages().v;
		}
		case mtpc_messages_messagesSlice: {
			const auto &data = result.c_messages_messagesSlice();
			peer->owner().processUsers(data.vusers());
			return &data.vmessages().v;
		}
		case mtpc_messages_channelMessages: {
			const auto &data = result.c_messages_channelMessages();
			peer->owner().processUsers(data.vusers());
			return &data.vmessages().v;
		}
		}
		return nullptr;
	}();
	count = list ? int(list->size()) : 0;
	if (!list) {
		return;
	}
	for (const auto &message : *list) {
		message.match([&](const MTPDmessage &data) {
			const auto id = MsgId(data.vid().v);
			if (!lastId || id < lastId) {
				lastId = id;
			}
			const auto text = qs(data.vmessage());
			if (!text.trimmed().isEmpty()) {
				into.push_back({
					.id = id,
					.date = data.vdate().v,
					.out = data.is_out(),
					.text = text,
				});
			}
		}, [&](const MTPDmessageService &data) {
			const auto id = MsgId(data.vid().v);
			if (!lastId || id < lastId) {
				lastId = id;
			}
		}, [](const MTPDmessageEmpty &) {
		});
	}
}

[[nodiscard]] std::pair<QString, QString> SplitBirthday(QString text) {
	static const auto expression = QRegularExpression(
		u"ДР:\\s*(\\d{1,2})\\.(\\d{1,2})"_q);
	auto lines = text.split(u'\n');
	auto birthday = QString();
	while (!lines.isEmpty() && lines.back().trimmed().isEmpty()) {
		lines.pop_back();
	}
	if (!lines.isEmpty() && lines.back().trimmed().startsWith(u"ДР:"_q)) {
		const auto match = expression.match(lines.back());
		if (match.hasMatch()) {
			const auto day = match.captured(1).toInt();
			const auto month = match.captured(2).toInt();
			if (day >= 1 && day <= 31 && month >= 1 && month <= 12) {
				birthday = QString::number(day).rightJustified(2, u'0')
					+ u'.'
					+ QString::number(month).rightJustified(2, u'0');
			}
		}
		lines.pop_back();
	}
	return { lines.join(u'\n').trimmed(), birthday };
}

void CollectDossier(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		not_null<Ui::FlatLabel*> label) {
	const auto key = KeyFor(peer);
	const auto name = peer->name();
	const auto lines = std::make_shared<std::vector<Line>>();
	const auto budget = (Ai::Current().provider == Ai::Provider::Groq)
		? kSmallBudget
		: kLargeBudget;
	const auto finish = [=] {
		if (lines->empty()) {
			box->showToast(u"В переписке пока нет текстовых сообщений."_q);
			return;
		}
		box->showToast(u"ИИ читает переписку…"_q);
		label->setText(u"Собираю досье…"_q);
		Ai::Ask(Ai::Request{
			.system = SystemPrompt(name),
			.user = Transcript(*lines, name, budget),
			.maxTokens = kDossierTokens,
		}, crl::guard(box, [=](QString text) {
			const auto [dossier, birthday] = SplitBirthday(text);
			auto &card = All()[key];
			card.name = name;
			card.dossier = dossier;
			card.dossierAt = base::unixtime::now();
			if (!birthday.isEmpty()) {
				card.birthday = birthday;
			}
			Save();
			label->setText(DossierText(card));
			box->showToast(u"Досье готово."_q);
		}), crl::guard(box, [=](QString error) {
			label->setText(DossierText(All()[key]));
			box->showToast(error);
		}));
	};
	const auto request = std::make_shared<Fn<void(MsgId, int)>>();
	*request = [=](MsgId offsetId, int page) {
		peer->session().api().request(MTPmessages_GetHistory(
			peer->input(),
			MTP_int(offsetId.bare),
			MTP_int(0),
			MTP_int(0),
			MTP_int(kDossierPageSize),
			MTP_int(0),
			MTP_int(0),
			MTP_long(0)
		)).done(crl::guard(box, [=](const MTPmessages_Messages &result) {
			auto lastId = MsgId();
			auto count = 0;
			ParseLines(peer, result, *lines, lastId, count);
			if (count >= kDossierPageSize
				&& lastId
				&& page + 1 < kDossierPages) {
				(*request)(lastId, page + 1);
			} else {
				finish();
				*request = nullptr;
			}
		})).fail(crl::guard(box, [=](const MTP::Error &) {
			box->showToast(u"Не удалось загрузить переписку."_q);
			*request = nullptr;
		})).send();
	};
	(*request)(MsgId(), 0);
}

void StartDossier(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		not_null<Ui::FlatLabel*> label) {
	if (!Ai::Ready()) {
		box->showToast(u"Сначала добавь ключ ИИ."_q);
		Ai::ShowSettings(controller->uiShow());
		return;
	}
	const auto key = KeyFor(peer);
	if (All()[key].dossierConsent) {
		CollectDossier(box, controller, peer, label);
		return;
	}
	const auto provider = Ai::ProviderName(Ai::Current().provider);
	const auto weak = QPointer<Ui::GenericBox>(box.get());
	controller->show(Ui::MakeConfirmBox({
		.text = u"Последние сообщения переписки с «%1» (до %2) будут отправлены в %3, чтобы ИИ выписал факты. Сами сообщения нигде не сохраняются, досье хранится только на этом компьютере. Продолжить?"_q
			.arg(peer->name())
			.arg(kDossierPages * kDossierPageSize)
			.arg(provider),
		.confirmed = [=](Fn<void()> close) {
			close();
			All()[key].dossierConsent = true;
			Save();
			if (const auto strong = weak.data()) {
				CollectDossier(strong, controller, peer, label);
			}
		},
		.confirmText = u"Отправить"_q,
	}));
}

void FillBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	box->setTitle(rpl::single(peer->name()));
	box->setWidth(st::boxWideWidth);

	const auto key = KeyFor(peer);
	const auto card = All()[key];

	auto info = QStringList();
	if (const auto username = peer->username(); !username.isEmpty()) {
		info.push_back(u"@"_q + username);
	}
	if (const auto user = peer->asUser()) {
		if (const auto birthday = user->birthday(); birthday.valid()) {
			info.push_back(tr::ayu_ContactCardBirthday(
				tr::now,
				lt_date,
				FormatBirthday(birthday)));
		} else if (!card.birthday.isEmpty()) {
			info.push_back(u"🎂 День рождения (из досье): "_q + card.birthday);
		}
	}
	info.push_back(LastSentText(card.lastSent));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(info.join(u'\n')),
		st::boxLabel));
	box->addSkip(st::boxPadding.bottom());

	const auto notes = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::newGroupDescription,
		Ui::InputField::Mode::MultiLine,
		tr::ayu_ContactCardNotes(),
		card.notes));
	const auto tags = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::ayu_ContactCardTags(),
		card.tags));

	if (peer->isUser()) {
		box->addSkip(st::boxPadding.bottom());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Ui::Text::Bold(u"🧠 Досье (ИИ)"_q)),
			st::boxLabel));
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(DossierText(card)),
			st::boxDividerLabel));
		label->setSelectable(true);
		box->addLeftButton(
			rpl::single(card.dossier.isEmpty()
				? u"Собрать досье"_q
				: u"Обновить досье"_q),
			[=] { StartDossier(box, controller, peer, label); });
	}

	box->setFocusCallback([=] {
		notes->setFocusFast();
	});
	box->addButton(tr::lng_settings_save(), [=] {
		auto &saved = All()[key];
		saved.notes = notes->getLastText().trimmed();
		saved.tags = tags->getLastText().trimmed();
		saved.name = peer->name();
		Save();
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

[[nodiscard]] Main::Session *FindSession(uint64 userId) {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			if (session->userId().bare == userId) {
				return session;
			}
		}
	}
	return nullptr;
}

void CheckBirthdays() {
	const auto today = QDate::currentDate();
	const auto mark = QString::number(today.day()).rightJustified(2, u'0')
		+ u'.'
		+ QString::number(today.month()).rightJustified(2, u'0');
	auto changed = false;
	for (auto &[key, card] : All()) {
		if (card.birthday != mark || card.birthdayYear == today.year()) {
			continue;
		}
		const auto session = FindSession(key.first);
		const auto window = session ? session->tryResolveWindow() : nullptr;
		if (!window) {
			continue;
		}
		card.birthdayYear = today.year();
		changed = true;
		window->showToast(u"🎂 Сегодня день рождения у «%1» (из досье)."_q
			.arg(card.name.isEmpty() ? u"собеседника"_q : card.name));
		QApplication::alert(window->widget().get());
	}
	if (changed) {
		Save();
	}
}

} // namespace

void RememberSent(not_null<History*> history) {
	if (!history->peer->isUser()) {
		return;
	}
	auto &card = All()[KeyFor(history->peer)];
	card.lastSent = base::unixtime::now();
	card.name = history->peer->name();
	Save();
}

void Show(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(FillBox, controller, peer));
}

void StartBirthdayReminders() {
	static auto timer = std::unique_ptr<base::Timer>();
	if (timer) {
		return;
	}
	timer = std::make_unique<base::Timer>(CheckBirthdays);
	timer->callEach(kBirthdayCheck);
}

} // namespace AyuFeatures::ContactCard
