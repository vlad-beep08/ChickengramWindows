// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/command_palette/command_palette.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/features/ai/ai.h"
#include "ayu/features/bookmarks/bookmarks.h"
#include "ayu/features/channel_feed/channel_feed.h"
#include "ayu/features/contact_card/contact_card.h"
#include "ayu/features/data_transfer/data_transfer.h"
#include "ayu/features/drafts_history/drafts_history.h"
#include "ayu/features/hidden_chats/hidden_chats.h"
#include "ayu/features/keyword_alerts/keyword_alerts.h"
#include "ayu/ui/design/design_sections.h"
#include "ayu/ui/design/design_themes.h"
#include "ayu/ui/design/design_widgets.h"
#include "base/event_filter.h"
#include "boxes/add_contact_box.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "core/application.h"
#include "ayu/ui/settings/settings_main.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "storage/storage_domain.h"
#include "ui/text/text_entity.h"
#include "settings/settings_common.h"
#include "settings/sections/settings_local_storage.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_privacy_security.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QKeyEvent>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QTextEdit>

namespace AyuFeatures::CommandPalette {
namespace {

constexpr auto kChatResultsLimit = 6;

struct Command {
	QString title;
	Fn<void()> run;
	QString hotkey;
};

[[nodiscard]] std::vector<Command> Commands(
		not_null<Window::SessionController*> controller) {
	auto result = std::vector<Command>();
	const auto add = [&](
			QString title,
			Fn<void()> run,
			QString hotkey = QString()) {
		result.push_back({ std::move(title), std::move(run), std::move(hotkey) });
	};
	const auto session = &controller->session();
	const auto history = controller->activeChatCurrent().history();

	add(tr::ayu_PaletteSearch(tr::now), [=] {
		GlobalSearch(controller);
	}, u"Ctrl+Shift+F"_q);
	add(tr::ayu_PaletteNewChat(tr::now), [=] {
		NewChat(controller);
	}, u"Ctrl+N"_q);
	add(tr::lng_saved_messages(tr::now), [=] {
		controller->showPeerHistory(session->user());
	});
	add(tr::lng_menu_my_profile(tr::now), [=] {
		controller->showPeerInfo(session->user());
	});
	add(tr::lng_menu_settings(tr::now), [=] {
		controller->showSettings();
	});
	add(tr::lng_settings_section_notify(tr::now), [=] {
		controller->showSettings(Settings::NotificationsId());
	});
	add(tr::lng_settings_section_privacy(tr::now), [=] {
		controller->showSettings(Settings::PrivacySecurityId());
	});
	add(tr::lng_settings_manage_local_storage(tr::now), [=] {
		controller->showSettings(Settings::LocalStorageId());
	});
	add(tr::lng_menu_contacts(tr::now), [=] {
		controller->content()->showLeftBox(PrepareContactsBox(controller));
	});
	add(tr::lng_menu_calls(tr::now), [=] {
		controller->content()->showLeftBox(
			::Calls::PrepareCallsBox(controller));
	});
	add(tr::lng_create_group_title(tr::now), [=] {
		controller->content()->showLeftBox(
			Box<GroupInfoBox>(controller, GroupInfoBox::Type::Group));
	});
	add(tr::lng_create_channel_title(tr::now), [=] {
		controller->content()->showLeftBox(
			Box<GroupInfoBox>(controller, GroupInfoBox::Type::Channel));
	});
	if (AyuDesign::HasMissingWebSections(session)) {
		add(tr::ayu_PaletteSections(tr::now), [=] {
			AyuDesign::AddWebSections(controller);
		});
	}
	for (const auto &theme : AyuDesign::Themes()) {
		const auto id = theme.id;
		add(u"Тема оформления: "_q + theme.title, [=] {
			AyuDesign::ApplyTheme(id);
		});
	}
	for (const auto &accent : AyuDesign::Accents()) {
		const auto id = accent.id;
		add(tr::ayu_DesignAccent(tr::now) + u": "_q + accent.title, [=] {
			AyuDesign::ApplyAccent(id);
		});
	}
	const auto current = &session->account();
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		const auto raw = account.get();
		if (raw == current || !raw->sessionExists()) {
			continue;
		}
		const auto name = raw->session().user()->name();
		add(tr::ayu_PaletteSwitchAccount(tr::now) + u": "_q + name, [=] {
			Core::App().domain().maybeActivate(raw);
		});
	}
	if (Core::App().domain().local().hasLocalPasscode()) {
		add(tr::ayu_PaletteLock(tr::now), [] {
			Core::App().lockByPasscode();
		});
	}
	add(tr::ayu_PaletteAyuSettings(tr::now), [=] {
		controller->showSettings(Settings::AyuMainId());
	});
	add(tr::ayu_PrivacyMode(tr::now), [=] {
		auto &settings = AyuSettings::getInstance();
		settings.setPrivacyMode(!settings.privacyMode());
		controller->showToast(settings.privacyMode()
			? tr::ayu_PalettePrivacyOn(tr::now)
			: tr::ayu_PalettePrivacyOff(tr::now));
	});
	add(tr::ayu_PaletteGhostMode(tr::now), [=] {
		auto &ghost = AyuSettings::ghost(session);
		ghost.setGhostModeEnabled(!ghost.isGhostModeActive());
		controller->showToast(ghost.isGhostModeActive()
			? tr::ayu_PaletteGhostModeOn(tr::now)
			: tr::ayu_PaletteGhostModeOff(tr::now));
	});
	add(tr::ayu_AllBookmarksMenuText(tr::now), [=] {
		Bookmarks::ShowBox(controller);
	});
	if (history) {
		add(tr::ayu_BookmarksMenuText(tr::now), [=] {
			Bookmarks::ShowBox(controller, history->peer->id);
		});
		add(tr::ayu_DraftsHistoryMenuText(tr::now), [=] {
			DraftsHistory::ShowBox(controller, history);
		});
		add(tr::ayu_ContactCardMenuText(tr::now), [=] {
			ContactCard::Show(controller, history->peer);
		});
		add((HiddenChats::IsHidden(history)
			? tr::ayu_HiddenChatsUnhideChat(tr::now)
			: tr::ayu_HiddenChatsHideChat(tr::now)), [=] {
			HiddenChats::Toggle(controller, history);
		});
	}
	add((HiddenChats::IsLocked()
		? tr::ayu_HiddenChatsShow(tr::now)
		: tr::ayu_HiddenChatsLock(tr::now)), [=] {
		HiddenChats::RequestUnlock(controller);
	});
	add(tr::ayu_PalettePanic(tr::now), [] {
		HiddenChats::Panic();
	}, u"Ctrl+Shift+H"_q);
	add(tr::ayu_KeywordAlertsTitle(tr::now), [=] {
		KeywordAlerts::ShowEditBox(controller);
	});
	add(u"Лента каналов"_q, [=] {
		ChannelFeed::Show(controller);
	});
	add(u"ИИ: ключ и сервис"_q, [=] {
		Ai::ShowSettings(controller->uiShow());
	});
	add(tr::ayu_DataExport(tr::now), [=] {
		DataTransfer::Export(controller);
	});
	add(tr::ayu_DataImport(tr::now), [=] {
		DataTransfer::Import(controller);
	});
	return result;
}

void FillBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::ayu_PaletteTitle());
	box->setWidth(st::boxWideWidth);

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::ayu_PalettePlaceholder()));
	const auto chats = box->verticalLayout()->add(
		object_ptr<Ui::VerticalLayout>(box->verticalLayout()));

	struct Row {
		QString search;
		Fn<void()> run;
		not_null<Ui::SlideWrap<AyuDesign::ListRow>*> wrap;
	};
	struct State {
		std::vector<Row> rows;
		int chatRows = 0;
		int selected = -1;
	};
	const auto state = std::make_shared<State>();
	const auto content = box->verticalLayout();
	for (auto &command : Commands(controller)) {
		const auto wrap = content->add(
			object_ptr<Ui::SlideWrap<AyuDesign::ListRow>>(
				content,
				object_ptr<AyuDesign::ListRow>(
					content,
					command.title,
					command.hotkey)));
		wrap->toggle(true, anim::type::instant);
		const auto run = command.run;
		wrap->entity()->setClickedCallback([=] {
			box->closeBox();
			run();
		});
		state->rows.push_back({
			.search = command.title.toLower(),
			.run = run,
			.wrap = wrap,
		});
	}

	const auto visible = [=] {
		auto result = std::vector<int>();
		for (auto i = 0; i != int(state->rows.size()); ++i) {
			if (state->rows[i].wrap->toggled()) {
				result.push_back(i);
			}
		}
		return result;
	};
	const auto select = [=](int index) {
		const auto count = int(state->rows.size());
		if (state->selected >= 0 && state->selected < count) {
			state->rows[state->selected].wrap->entity()->setSelected(false);
		}
		state->selected = index;
		if (index >= 0 && index < count) {
			state->rows[index].wrap->entity()->setSelected(true);
		}
	};
	const auto selectFirst = [=] {
		const auto indices = visible();
		select(indices.empty() ? -1 : indices.front());
	};
	const auto move = [=](int delta) {
		const auto indices = visible();
		if (indices.empty()) {
			return;
		}
		const auto i = ranges::find(indices, state->selected);
		const auto position = (i != end(indices))
			? int(i - begin(indices)) + delta
			: (delta > 0 ? 0 : int(indices.size()) - 1);
		select(indices[std::clamp(position, 0, int(indices.size()) - 1)]);
	};
	const auto runSelected = [=] {
		const auto indices = visible();
		const auto i = ranges::find(indices, state->selected);
		const auto index = (i != end(indices))
			? state->selected
			: (indices.empty() ? -1 : indices.front());
		if (index < 0) {
			return;
		}
		const auto run = state->rows[index].run;
		box->closeBox();
		run();
	};

	const auto refreshChats = [=](const QString &query) {
		select(-1);
		state->rows.erase(
			begin(state->rows),
			begin(state->rows) + state->chatRows);
		state->chatRows = 0;
		chats->clear();
		if (query.isEmpty()) {
			chats->resizeToWidth(chats->width());
			return;
		}
		const auto words = TextUtilities::PrepareSearchWords(query);
		const auto data = &controller->session().data();
		auto histories = std::vector<not_null<History*>>();
		const auto collect = [&](not_null<Dialogs::IndexedList*> list) {
			for (const auto &row : list->filtered(words)) {
				if (int(histories.size()) >= kChatResultsLimit) {
					break;
				}
				const auto history = row->history();
				if (!history
					|| ranges::contains(histories, history, [](
						not_null<History*> entry) {
						return entry.get();
					})
					|| (HiddenChats::IsLocked()
						&& HiddenChats::IsHidden(history))) {
					continue;
				}
				histories.push_back(history);
			}
		};
		collect(data->chatsList()->indexed());
		collect(data->contactsList());
		auto added = std::vector<Row>();
		for (const auto history : histories) {
			const auto peer = history->peer;
			const auto user = peer->asUser();
			const auto kind = (user && user->isBot())
				? tr::ayu_PaletteKindBot(tr::now)
				: user
				? tr::ayu_PaletteKindChat(tr::now)
				: peer->isBroadcast()
				? tr::ayu_PaletteKindChannel(tr::now)
				: tr::ayu_PaletteKindGroup(tr::now);
			const auto wrap = chats->add(
				object_ptr<Ui::SlideWrap<AyuDesign::ListRow>>(
					chats,
					object_ptr<AyuDesign::ListRow>(
						chats,
						peer->name(),
						kind)));
			wrap->toggle(true, anim::type::instant);
			const auto run = [=] {
				controller->showPeerHistory(history);
			};
			wrap->entity()->setClickedCallback([=] {
				box->closeBox();
				run();
			});
			added.push_back({ .search = QString(), .run = run, .wrap = wrap });
		}
		state->chatRows = int(added.size());
		state->rows.insert(
			begin(state->rows),
			std::make_move_iterator(begin(added)),
			std::make_move_iterator(end(added)));
		chats->resizeToWidth(chats->width());
	};

	field->changes(
	) | rpl::on_next([=] {
		const auto query = field->getLastText().trimmed().toLower();
		refreshChats(query);
		for (auto i = state->chatRows; i != int(state->rows.size()); ++i) {
			const auto &row = state->rows[i];
			row.wrap->toggle(
				query.isEmpty() || row.search.contains(query),
				anim::type::instant);
		}
		selectFirst();
	}, field->lifetime());
	field->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers) {
		runSelected();
	}, field->lifetime());
	base::install_event_filter(field->rawTextEdit(), [=](
			not_null<QEvent*> event) {
		if (event->type() != QEvent::KeyPress) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(event.get())->key();
		if (key == Qt::Key_Up || key == Qt::Key_Down) {
			move(key == Qt::Key_Up ? -1 : 1);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
	selectFirst();

	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

[[nodiscard]] bool IsTextInput(QWidget *widget) {
	return qobject_cast<QTextEdit*>(widget)
		|| qobject_cast<QLineEdit*>(widget)
		|| qobject_cast<QPlainTextEdit*>(widget)
		|| qobject_cast<QAbstractSpinBox*>(widget);
}

} // namespace

void Show(not_null<Window::SessionController*> controller) {
	controller->show(Box(FillBox, controller));
}

void GlobalSearch(not_null<Window::SessionController*> controller) {
	controller->content()->hideLeftColumn();
	controller->searchMessages(QString(), Dialogs::Key());
}

void NewChat(not_null<Window::SessionController*> controller) {
	controller->content()->showLeftBox(PrepareContactsBox(controller));
}

void InstallGlobalHotkey() {
	static auto installed = false;
	if (installed) {
		return;
	}
	installed = true;
	base::install_event_filter(QCoreApplication::instance(), [](
			not_null<QEvent*> event) {
		if (event->type() != QEvent::KeyPress) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(event.get());
		const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
		if (key->key() != Qt::Key_K
			|| modifiers != Qt::ControlModifier
			|| key->isAutoRepeat()
			|| IsTextInput(QApplication::focusWidget())) {
			return base::EventFilterResult::Continue;
		}
		const auto window = Core::App().activeWindow();
		const auto controller = window ? window->sessionController() : nullptr;
		if (!controller) {
			return base::EventFilterResult::Continue;
		}
		Show(controller);
		return base::EventFilterResult::Cancel;
	});
}

} // namespace AyuFeatures::CommandPalette
