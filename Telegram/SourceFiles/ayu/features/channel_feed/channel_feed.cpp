#include "ayu/features/channel_feed/channel_feed.h"

#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mtproto/sender.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace AyuFeatures::ChannelFeed {
namespace {

constexpr auto kChannels = 40;
constexpr auto kPerChannel = 12;
constexpr auto kParallel = 3;
constexpr auto kDays = 3;
constexpr auto kMaxPosts = 150;
constexpr auto kTextLines = 4;
constexpr auto kSecondsInDay = 24 * 60 * 60;

enum class Filter {
	All,
	Media,
	NoAds,
};

struct Post {
	FullMsgId id;
	TimeId date = 0;
};

struct State {
	explicit State(not_null<Main::Session*> session)
	: api(&session->mtp()) {
	}

	MTP::Sender api;
	std::vector<not_null<History*>> queue;
	std::vector<Post> posts;
	int running = 0;
	int finished = 0;
	int total = 0;
	Filter filter = Filter::All;
	Fn<void()> rebuild;
	Fn<void()> next;
};

[[nodiscard]] QString MediaLabel(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	} else if (media->photo()) {
		return u"📷 Фото"_q;
	} else if (const auto document = media->document()) {
		if (document->isVideoFile() || document->isAnimation()) {
			return u"🎬 Видео"_q;
		} else if (document->isVoiceMessage() || document->isAudioFile()) {
			return u"🎵 Аудио"_q;
		}
		return u"📎 Файл"_q;
	} else if (media->poll()) {
		return u"📊 Опрос"_q;
	}
	return u"🔗 Вложение"_q;
}

[[nodiscard]] bool LooksLikeAd(not_null<HistoryItem*> item) {
	const auto text = item->originalText().text.toLower();
	static const auto markers = std::array{
		u"#реклама"_q,
		u"на правах рекламы"_q,
		u"erid"_q,
		u"#ad "_q,
		u"#промо"_q,
		u"партнёрский материал"_q,
		u"партнерский материал"_q,
	};
	for (const auto &marker : markers) {
		if (text.contains(marker)) {
			return true;
		}
	}
	for (const auto &line : text.split(QChar('\n'))) {
		if (line.trimmed() == u"реклама"_q) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString DateText(TimeId date) {
	const auto when = base::unixtime::parse(date);
	const auto now = QDateTime::currentDateTime();
	if (when.date() == now.date()) {
		return when.toString(u"HH:mm"_q);
	} else if (when.date() == now.date().addDays(-1)) {
		return u"вчера "_q + when.toString(u"HH:mm"_q);
	}
	return when.toString(u"dd.MM HH:mm"_q);
}

class PostCard final : public Ui::AbstractButton {
public:
	PostCard(QWidget *parent, not_null<HistoryItem*> item);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] int textLeft() const;
	[[nodiscard]] int textWidth(int outer) const;

	const not_null<PeerData*> _peer;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _name;
	Ui::Text::String _text;
	QString _date;
	QString _media;

};

PostCard::PostCard(QWidget *parent, not_null<HistoryItem*> item)
: AbstractButton(parent)
, _peer(item->history()->peer)
, _date(DateText(item->date()))
, _media(MediaLabel(item)) {
	_name.setText(st::semiboldTextStyle, _peer->name());
	_text.setText(st::defaultTextStyle, item->originalText().text.trimmed());
	_peer->loadUserpic();
	_peer->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

int PostCard::textLeft() const {
	return st::boxRowPadding.left()
		+ st::contactsPhotoSize
		+ (st::boxOptionListSkip / 2);
}

int PostCard::textWidth(int outer) const {
	return std::max(outer - textLeft() - st::boxRowPadding.right(), 1);
}

int PostCard::resizeGetHeight(int newWidth) {
	const auto skip = st::boxOptionListSkip / 2;
	auto result = skip + st::semiboldFont->height;
	if (!_media.isEmpty()) {
		result += st::normalFont->height;
	}
	if (!_text.isEmpty()) {
		result += std::min(
			_text.countHeight(textWidth(newWidth)),
			kTextLines * st::defaultTextStyle.font->height);
	}
	return std::max(result, skip + st::contactsPhotoSize) + skip;
}

void PostCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	if (isOver()) {
		p.fillRect(rect(), st::windowBgOver);
	}
	const auto skip = st::boxOptionListSkip / 2;
	_peer->paintUserpicLeft(
		p,
		_userpic,
		st::boxRowPadding.left(),
		skip,
		width(),
		st::contactsPhotoSize);

	const auto left = textLeft();
	const auto available = textWidth(width());
	const auto dateWidth = st::normalFont->width(_date);
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		left + available - dateWidth,
		skip + st::normalFont->ascent,
		_date);
	p.setPen(st::windowBoldFg);
	_name.drawElided(p, left, skip, std::max(available - dateWidth - skip, 1));

	auto top = skip + st::semiboldFont->height;
	if (!_media.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(left, top + st::normalFont->ascent, _media);
		top += st::normalFont->height;
	}
	if (!_text.isEmpty()) {
		p.setPen(st::windowFg);
		_text.drawElided(p, left, top, available, kTextLines);
	}
}

void Collect(
		not_null<PeerData*> peer,
		const MTPmessages_Messages &result,
		not_null<State*> state) {
	const auto list = [&]() -> const QVector<MTPMessage>* {
		switch (result.type()) {
		case mtpc_messages_messages: {
			const auto &data = result.c_messages_messages();
			peer->owner().processUsers(data.vusers());
			peer->owner().processChats(data.vchats());
			return &data.vmessages().v;
		}
		case mtpc_messages_messagesSlice: {
			const auto &data = result.c_messages_messagesSlice();
			peer->owner().processUsers(data.vusers());
			peer->owner().processChats(data.vchats());
			return &data.vmessages().v;
		}
		case mtpc_messages_channelMessages: {
			const auto &data = result.c_messages_channelMessages();
			peer->owner().processUsers(data.vusers());
			peer->owner().processChats(data.vchats());
			return &data.vmessages().v;
		}
		}
		return nullptr;
	}();
	if (!list) {
		return;
	}
	const auto since = base::unixtime::now() - kDays * kSecondsInDay;
	auto groups = base::flat_set<uint64>();
	for (const auto &message : *list) {
		const auto item = peer->owner().addNewMessage(
			message,
			MessageFlags(),
			NewMessageType::Existing);
		if (!item || item->isService() || item->date() < since) {
			continue;
		}
		const auto group = item->groupId();
		if (group) {
			if (groups.contains(group.value)) {
				continue;
			}
			groups.emplace(group.value);
		}
		state->posts.push_back({ item->fullId(), item->date() });
	}
}

void FillBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	box->setTitle(rpl::single(u"📰 Лента каналов"_q));
	box->setWidth(st::boxWideWidth);

	const auto state = box->lifetime().make_state<State>(session);
	const auto tabs = box->addRow(
		object_ptr<Ui::SettingsSlider>(box, st::dialogsSearchTabs),
		style::margins(
			st::boxRowPadding.left(),
			0,
			st::boxRowPadding.right(),
			st::boxOptionListSkip / 2));
	tabs->setSections({ u"Все"_q, u"С медиа"_q, u"Без рекламы"_q });
	tabs->setActiveSectionFast(0);
	const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		st::boxDividerLabel));
	const auto list = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());

	state->rebuild = [=] {
		auto posts = state->posts;
		ranges::sort(posts, ranges::greater(), &Post::date);
		list->clear();
		auto shown = 0;
		for (const auto &post : posts) {
			if (shown >= kMaxPosts) {
				break;
			}
			const auto item = session->data().message(post.id);
			if (!item
				|| (state->filter == Filter::Media && !item->media())
				|| (state->filter == Filter::NoAds && LooksLikeAd(item))) {
				continue;
			}
			const auto id = post.id;
			const auto card = list->add(object_ptr<PostCard>(list, item));
			card->setClickedCallback([=] {
				if (const auto item = session->data().message(id)) {
					controller->showMessage(item);
				}
			});
			++shown;
		}
		list->resizeToWidth(list->width());
		const auto loading = (state->finished < state->total);
		status->setText(loading
			? u"Загружаю посты: %1 из %2 каналов…"_q
				.arg(state->finished)
				.arg(state->total)
			: !shown
			? u"За последние %1 дня новых постов нет."_q.arg(kDays)
			: u"Посты за %1 дня из %2 каналов, новые сверху."_q
				.arg(kDays)
				.arg(state->total));
	};

	tabs->sectionActivated(
	) | rpl::on_next([=](int index) {
		state->filter = Filter(index);
		state->rebuild();
	}, tabs->lifetime());

	const auto indexed = session->data().chatsList()->indexed();
	for (const auto &row : *indexed) {
		const auto history = row->history();
		if (history && history->peer->isBroadcast()) {
			state->queue.push_back(history);
		}
	}
	ranges::sort(state->queue, ranges::greater(), [](not_null<History*> h) {
		return h->chatListTimeId();
	});
	if (int(state->queue.size()) > kChannels) {
		state->queue.erase(state->queue.begin() + kChannels, state->queue.end());
	}
	ranges::reverse(state->queue);
	state->total = int(state->queue.size());

	state->next = [=] {
		while (state->running < kParallel && !state->queue.empty()) {
			const auto history = state->queue.back();
			state->queue.pop_back();
			++state->running;
			const auto peer = history->peer;
			const auto finish = [=] {
				--state->running;
				++state->finished;
				state->rebuild();
				state->next();
			};
			state->api.request(MTPmessages_GetHistory(
				peer->input(),
				MTP_int(0),
				MTP_int(0),
				MTP_int(0),
				MTP_int(kPerChannel),
				MTP_int(0),
				MTP_int(0),
				MTP_long(0)
			)).done([=](const MTPmessages_Messages &result) {
				Collect(peer, result, state);
				finish();
			}).fail([=] {
				finish();
			}).send();
		}
	};
	state->rebuild();
	state->next();

	box->addButton(rpl::single(u"Закрыть"_q), [=] {
		box->closeBox();
	});
}

} // namespace

void Show(not_null<Window::SessionController*> controller) {
	controller->content()->showLeftBox(Box(FillBox, controller));
}

} // namespace AyuFeatures::ChannelFeed
