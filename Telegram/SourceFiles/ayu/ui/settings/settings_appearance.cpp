// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_appearance.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ui/boxes/font_selector.h"
#include "ayu/ui/components/avatar_corners_preview.h"
#include "ayu/ui/components/icon_picker.h"
#include "ayu/ui/design/design_system.h"
#include "ayu/ui/design/design_themes.h"
#include "ayu/ui/design/design_widgets.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_effects.h"
#include "ayu/ui/settings/settings_main.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}
		return true;
	}
	return false;
}

void BuildDesign(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	const auto controller = builder.controller();
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle({
		.id = u"ayu/design"_q,
		.title = tr::ayu_DesignHeader(),
		.keywords = { u"theme"_q, u"design"_q },
	});

	const auto themes = AyuDesign::Themes();
	auto themeTitles = std::vector<QString>();
	auto themeSelection = 0;
	for (const auto &theme : themes) {
		if (theme.id == AyuDesign::CurrentThemeId()) {
			themeSelection = int(themeTitles.size());
		}
		themeTitles.push_back(theme.title);
	}
	ayu.addChooseButton({
		.id = u"ayu/designTheme"_q,
		.title = rpl::single(u"Тема оформления"_q),
		.boxTitle = rpl::single(u"Тема оформления"_q),
		.initialSelection = themeSelection,
		.options = std::move(themeTitles),
		.setter = [=](int index) {
			if (index >= 0 && index < int(themes.size())) {
				AyuDesign::ApplyTheme(themes[index].id);
			}
		},
		.keywords = { u"theme"_q, u"collage"_q, u"коллаж"_q },
	});

	const auto accents = AyuDesign::Accents();
	auto accentTitles = std::vector<QString>();
	auto accentSelection = 0;
	for (const auto &accent : accents) {
		if (accent.id == AyuDesign::CurrentAccentId()) {
			accentSelection = int(accentTitles.size());
		}
		accentTitles.push_back(accent.title);
	}
	ayu.addChooseButton({
		.id = u"ayu/designAccent"_q,
		.title = tr::ayu_DesignAccent(),
		.boxTitle = tr::ayu_DesignAccent(),
		.initialSelection = accentSelection,
		.options = std::move(accentTitles),
		.setter = [=](int index) {
			if (index >= 0 && index < int(accents.size())) {
				AyuDesign::ApplyAccent(accents[index].id);
			}
		},
		.keywords = { u"accent"_q, u"color"_q, u"colour"_q },
	});

	builder.addSectionButton({
		.title = tr::ayu_EffectsPageTitle(),
		.targetSection = AyuEffectsId(),
		.icon = { &st::menuIconPowerUsage },
		.keywords = { u"effects"_q, u"spotlight"_q, u"animations"_q },
	});
	ayu.addToggle({
		.id = u"ayu/designGlass"_q,
		.title = tr::ayu_DesignGlass(),
		.getter = [=] { return settings->designGlass(); },
		.setter = [=](bool enabled) {
			AyuSettings::getInstance().setDesignGlass(enabled);
			ShowRestartPrompt(controller);
		},
	});
	ayu.addChooseButton({
		.id = u"ayu/designGlassTint"_q,
		.title = tr::ayu_DesignGlassTint(),
		.boxTitle = tr::ayu_DesignGlassTint(),
		.initialSelection = settings->designGlassTint(),
		.options = {
			tr::ayu_DesignGlassTintDense(tr::now),
			tr::ayu_DesignGlassTintNormal(tr::now),
			tr::ayu_DesignGlassTintClear(tr::now),
		},
		.setter = [=](int index) {
			AyuSettings::getInstance().setDesignGlassTint(index);
		},
	});
	ayu.addChooseButton({
		.id = u"ayu/designGlassBlur"_q,
		.title = tr::ayu_DesignGlassBlur(),
		.boxTitle = tr::ayu_DesignGlassBlur(),
		.initialSelection = settings->designGlassBlur(),
		.options = {
			tr::ayu_DesignGlassBlurLight(tr::now),
			tr::ayu_DesignGlassBlurNormal(tr::now),
			tr::ayu_DesignGlassBlurStrong(tr::now),
		},
		.setter = [=](int index) {
			AyuSettings::getInstance().setDesignGlassBlur(index);
		},
	});
	ayu.addChooseButton({
		.id = u"ayu/designDensity"_q,
		.title = tr::ayu_DesignDensity(),
		.boxTitle = tr::ayu_DesignDensity(),
		.initialSelection = settings->designDensity(),
		.options = {
			tr::ayu_DesignDensityCompact(tr::now),
			tr::ayu_DesignDensityNormal(tr::now),
			tr::ayu_DesignDensityComfortable(tr::now),
		},
		.setter = [=](int index) {
			AyuSettings::getInstance().setDesignDensity(index);
			ShowRestartPrompt(controller);
		},
	});
	ayu.addChooseButton({
		.id = u"ayu/designCorners"_q,
		.title = tr::ayu_DesignCorners(),
		.boxTitle = tr::ayu_DesignCorners(),
		.initialSelection = settings->designCorners(),
		.options = {
			tr::ayu_DesignCornersTelegram(tr::now),
			tr::ayu_DesignCornersSharp(tr::now),
			tr::ayu_DesignCornersSoft(tr::now),
			tr::ayu_DesignCornersRound(tr::now),
		},
		.setter = [=](int index) {
			AyuSettings::getInstance().setDesignCorners(index);
			ShowRestartPrompt(controller);
		},
	});
	ayu.addChooseButton({
		.id = u"ayu/designMotion"_q,
		.title = tr::ayu_DesignMotion(),
		.boxTitle = tr::ayu_DesignMotion(),
		.initialSelection = settings->designMotion(),
		.options = {
			tr::ayu_DesignMotionFull(tr::now),
			tr::ayu_DesignMotionReduced(tr::now),
			tr::ayu_DesignMotionOff(tr::now),
		},
		.setter = [](int index) {
			AyuSettings::getInstance().setDesignMotion(index);
			AyuDesign::ApplyMotion();
		},
	});

	constexpr auto kOpacityMin = 50;
	constexpr auto kOpacityStep = 5;
	ayu.addSlider({
		.id = u"ayu/windowOpacity"_q,
		.title = tr::ayu_DesignWindowOpacity(),
		.steps = (100 - kOpacityMin) / kOpacityStep + 1,
		.current = (settings->windowOpacity() - kOpacityMin) / kOpacityStep,
		.indexToValue = [](int index) {
			return kOpacityMin + index * kOpacityStep;
		},
		.onChanged = nullptr,
		.onFinalChanged = [](int index) {
			AyuSettings::getInstance().setWindowOpacity(
				kOpacityMin + index * kOpacityStep);
			AyuDesign::ApplyWindowOpacityToAll();
		},
		.formatLabel = [](int index) {
			return QString::number(kOpacityMin + index * kOpacityStep)
				+ u"%"_q;
		},
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_DesignAbout());
	builder.addSkip();
}

void BuildAppIcon(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/appIcon"_q,
		.title = tr::ayu_AppIconHeader(),
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(ctx.container),
			.margin = st::settingsButtonNoIcon.padding,
		};
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	builder.addDivider();
	builder.addSkip();
	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationBadge"_q,
		.title = tr::ayu_HideNotificationBadge(),
		.getter = &AyuSettings::hideNotificationBadge,
		.setter = &AyuSettings::setHideNotificationBadge,
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_HideNotificationBadgeDescription());
	builder.addSkip();
#else
    builder.addDivider();
    builder.addSkip();
#endif
}

void BuildAvatarCorners(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();
	const auto controller = builder.controller();

	const auto mapRadius = [](int val)
	{
		if (val == 0) {
			return tr::ayu_AvatarCornersSquare(tr::now).toUpper();
		} else if (val == AyuUiSettings::kMaxAvatarCorners) {
			return tr::ayu_AvatarCornersCircle(tr::now).toUpper();
		}
		return QString::number(val);
	};

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto container = ctx.container;
		auto title = object_ptr<Ui::FlatLabel>(
			container,
			tr::ayu_AvatarCorners(),
			st::defaultSubsectionTitle);
		const auto titleRaw = title.data();

		const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				settings->avatarCornersValue() | rpl::map(mapRadius),
				st::settingsPremiumNewBadge),
			st::ayuBetaBadgePadding);
		badge->show();
		badge->setAttribute(Qt::WA_TransparentForMouseEvents);
		badge->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = st::ayuBetaBadgePadding.left();
			p.drawRoundedRect(badge->rect(), r, r);
		}, badge->lifetime());

		titleRaw->geometryValue() | rpl::on_next([=](QRect geometry) {
			badge->moveToLeft(
				geometry.x()
					+ titleRaw->textMaxWidth()
					+ st::settingsPremiumNewBadgePosition.x(),
				geometry.y()
					+ (geometry.height() - badge->height()) / 2);
		}, badge->lifetime());

		return {
			.widget = std::move(title),
			.margin = st::defaultSubsectionTitlePadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"ayu/avatarCorners"_q,
			.title = tr::ayu_AvatarCorners(tr::now),
		};
	});

	auto *previewRaw = static_cast<AvatarCornersPreview*>(nullptr);
	builder.add([&](const Builder::WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<AvatarCornersPreview>(
			ctx.container,
			controller);
		previewRaw = preview.data();
		const auto vMargin = st::settingsButtonNoIcon.padding
			- st::defaultDialogRow.padding;
		return {
			.widget = std::move(preview),
			.margin = QMargins(0, vMargin.top(), 0, vMargin.bottom()),
		};
	});

	ayu.addSlider({
		.id = u"ayu/avatarCornersSlider"_q,
		.title = rpl::single(QString()),
		.showTitle = false,
		.steps = AyuUiSettings::kMaxAvatarCorners + 1,
		.current = settings->avatarCorners(),
		.onChanged = [=](int val) {
			AyuSettings::getInstance().setAvatarCorners(val);
			if (previewRaw) {
				previewRaw->update();
			}
		},
		.onFinalChanged = [=](int val) {
			AyuSettings::getInstance().setAvatarCorners(val);
			ShowRestartPrompt(controller);
		},
	});

	ayu.addSettingToggle({
		.id = u"ayu/singleCornerRadius"_q,
		.title = tr::ayu_SingleCornerRadius(),
		.getter = &AyuSettings::singleCornerRadius,
		.setter = &AyuSettings::setSingleCornerRadius,
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_SingleCornerRadiusDescription());
	builder.addSkip();
}

void BuildAppearance(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle(tr::ayu_CategoryAppearance());

	ayu.addSettingToggle({
		.id = u"ayu/materialSwitches"_q,
		.altIds = { u"ayu/newSwitchStyle"_q },
		.title = tr::ayu_MaterialSwitches(),
		.getter = &AyuSettings::materialSwitches,
		.setter = &AyuSettings::setMaterialSwitches,
	});
	ayu.addSettingToggle({
		.id = u"ayu/disableCustomBackgrounds"_q,
		.altIds = { u"ayu/customThemes"_q },
		.title = tr::ayu_DisableCustomBackgrounds(),
		.getter = &AyuSettings::disableCustomBackgrounds,
		.setter = &AyuSettings::setDisableCustomBackgrounds,
	});
	ayu.addSettingToggle({
		.id = u"ayu/hidePremiumStatuses"_q,
		.title = tr::ayu_HidePremiumStatuses(),
		.getter = &AyuSettings::hidePremiumStatuses,
		.setter = &AyuSettings::setHidePremiumStatuses,
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"ayu/monoFont"_q,
		.title = tr::ayu_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::ayu_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			AyuUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					AyuSettings::getInstance().setMonoFont(font);
				});
		},
	});

	ayu.addSectionDivider();
}

void BuildChatFolders(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_ChatFoldersHeader());

	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationCounters"_q,
		.altIds = { u"ayu/tabCounter"_q },
		.title = tr::ayu_HideNotificationCounters(),
		.getter = &AyuSettings::hideNotificationCounters,
		.setter = &AyuSettings::setHideNotificationCounters,
	});
	ayu.addSettingToggle({
		.id = u"ayu/hideAllChatsFolder"_q,
		.altIds = { u"ayu/hideAllChats"_q },
		.title = tr::ayu_HideAllChats(),
		.getter = &AyuSettings::hideAllChatsFolder,
		.setter = &AyuSettings::setHideAllChatsFolder,
	});
	ayu.addSettingToggle({
		.id = u"ayu/designSidebarTools"_q,
		.title = tr::ayu_DesignSidebarTools(),
		.getter = &AyuSettings::designSidebarTools,
		.setter = &AyuSettings::setDesignSidebarTools,
		.keywords = { u"saved"_q, u"bookmarks"_q, u"sidebar"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/designSectionsButton"_q,
		.title = tr::ayu_DesignSectionsButton(),
		.getter = &AyuSettings::designSectionsButton,
		.setter = &AyuSettings::setDesignSectionsButton,
		.keywords = { u"sections"_q, u"folders"_q },
	});

	ayu.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_TrayElementsHeader());

	ayu.addSettingToggle({
		.id = u"ayu/showGhostToggleInTray"_q,
		.title = tr::ayu_EnableGhostModeTray(),
		.getter = &AyuSettings::showGhostToggleInTray,
		.setter = &AyuSettings::setShowGhostToggleInTray,
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	ayu.addSettingToggle({
		.id = u"ayu/showStreamerToggleInTray"_q,
		.title = tr::ayu_EnableStreamerModeTray(),
		.getter = &AyuSettings::showStreamerToggleInTray,
		.setter = &AyuSettings::setShowStreamerToggleInTray,
	});
#endif

	ayu.addSectionDivider();
}

const auto kMeta = BuildHelper({
	.id = AyuAppearance::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryAppearance,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildDesign(builder, ayu);
	BuildAppIcon(builder, ayu);
	BuildAvatarCorners(builder, ayu);
	BuildAppearance(builder, ayu);
	BuildChatFolders(builder, ayu);
	BuildTrayElements(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuAppearance::title() {
	return tr::ayu_CategoryAppearance();
}

AyuAppearance::AyuAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuAppearanceId() {
	return AyuAppearance::Id();
}

} // namespace Settings
