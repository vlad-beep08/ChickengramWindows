// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/design/design_account_menu.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/admin_panel/admin_api.h"
#include "ayu/features/admin_panel/admin_window.h"
#include "ayu/ui/design/design_themes.h"
#include "boxes/add_contact_box.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "core/application.h"
#include "data/data_user.h"
#include "info/info_memento.h"
#include "info/stories/info_stories_widget.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_dc_options.h"
#include "ui/painter.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "ui/ui_utility.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace AyuDesign {
namespace {

constexpr auto kAccountUserpicSize = 24;

class AccountAction final : public Ui::Menu::Action {
public:
	AccountAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		not_null<QAction*> action,
		not_null<UserData*> user)
	: Action(parent, st, action, nullptr, nullptr)
	, _user(user) {
		_user->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			update();
		}, lifetime());
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Action::paintEvent(e);
		auto p = QPainter(this);
		const auto size = style::ConvertScale(kAccountUserpicSize);
		_user->paintUserpicLeft(
			p,
			_userpic,
			st().itemIconPosition.x(),
			(height() - size) / 2,
			width(),
			size);
	}

private:
	const not_null<UserData*> _user;
	Ui::PeerUserpicView _userpic;

};

void ShowInLeftColumn(
		not_null<Window::SessionController*> controller,
		object_ptr<Ui::BoxContent> box) {
	controller->content()->showLeftBox(std::move(box));
}

void FillMore(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller) {
	menu->addAction(tr::lng_create_group_title(tr::now), [=] {
		ShowInLeftColumn(
			controller,
			Box<GroupInfoBox>(controller, GroupInfoBox::Type::Group));
	}, &st::menuIconGroups);
	menu->addAction(tr::lng_create_channel_title(tr::now), [=] {
		ShowInLeftColumn(
			controller,
			Box<GroupInfoBox>(controller, GroupInfoBox::Type::Channel));
	}, &st::menuIconChannel);
	menu->addAction(tr::lng_menu_calls(tr::now), [=] {
		ShowInLeftColumn(controller, ::Calls::PrepareCallsBox(controller));
	}, &st::menuIconPhone);
	const auto session = &controller->session();
	menu->addAction(tr::ayu_GhostModeToggle(tr::now), [=] {
		auto &ghost = AyuSettings::ghost(session);
		ghost.setGhostModeEnabled(!ghost.isGhostModeActive());
	}, &st::ayuGhostIcon);
}

} // namespace

void ShowAccountMenu(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> anchor) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		anchor.get(),
		st::popupMenuWithIcons);
	menu->deleteOnHide(true);

	auto &domain = Core::App().domain();
	const auto current = &controller->session().account();
	for (const auto &account : domain.orderedAccounts()) {
		if (!account->sessionExists()) {
			continue;
		}
		const auto user = account->session().user();
		const auto raw = account.get();
		const auto action = Ui::Menu::CreateAction(
			menu->menu().get(),
			user->name(),
			[=] {
				if (raw != current) {
					Core::App().domain().maybeActivate(raw);
				}
			});
		menu->addAction(base::make_unique_q<AccountAction>(
			menu->menu(),
			menu->st().menu,
			action,
			user));
	}
	if (int(domain.accounts().size()) < domain.maxAccounts()) {
		menu->addAction(tr::lng_menu_add_account(tr::now), [=] {
			Core::App().setActivePrimaryWindow(&controller->window());
			Core::App().domain().addActivated(MTP::Environment{});
		}, &st::menuIconAddAccount);
	}
	menu->addSeparator();

	const auto self = controller->session().user();
	menu->addAction(tr::lng_menu_my_profile(tr::now), [=] {
		controller->showSection(Info::Stories::Make(self));
	}, &st::menuIconProfile);
	menu->addAction(tr::lng_saved_messages(tr::now), [=] {
		controller->showPeerHistory(self);
	}, &st::menuIconSavedMessages);
	menu->addAction(tr::lng_menu_contacts(tr::now), [=] {
		ShowInLeftColumn(controller, PrepareContactsBox(controller));
	}, &st::menuIconUserShow);
	menu->addAction(tr::lng_menu_settings(tr::now), [=] {
		controller->showSettings();
	}, &st::menuIconSettings);
	if (AyuFeatures::AdminPanel::Available()) {
		menu->addAction(u"Админка Чикена"_q, [] {
			AyuFeatures::AdminPanel::ShowWindow();
		}, &st::menuIconAdmin);
	}

	auto more = std::make_unique<Ui::PopupMenu>(
		menu,
		st::popupMenuWithIcons);
	FillMore(more.get(), controller);
	menu->addAction(
		tr::ayu_WebMenuMore(tr::now),
		std::move(more),
		&st::ayuMenuMoreIcon,
		&st::ayuMenuMoreIcon);

	menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopLeft);
	menu->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

} // namespace AyuDesign
