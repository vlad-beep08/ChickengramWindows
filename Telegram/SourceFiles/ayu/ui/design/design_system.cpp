// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/design/design_system.h"

#include "ayu/ayu_settings.h"
#include "base/algorithm.h"
#include "core/application.h"
#include "mainwindow.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/style/style_core_scale.h"
#include "window/window_controller.h"
#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_info.h"
#include "styles/style_info_profile_actions.h"
#include "styles/style_menu_icons.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtGui/QFontDatabase>

#include <array>

namespace AyuDesign {
namespace {

[[nodiscard]] auto WebIconPresets()
-> base::flat_map<const void*, const style::color*> & {
	static auto result = base::flat_map<const void*, const style::color*>();
	return result;
}

constexpr auto kMinWindowOpacity = 50;
constexpr auto kMaxWindowOpacity = 100;
constexpr auto kSelectedAccentMix = 0.12;
constexpr auto kDisabledAccentMix = 0.5;
constexpr auto kDangerSurfaceMix = 0.08;
constexpr auto kIslandMargin = 16;
constexpr auto kIslandRadius = 24;
constexpr auto kWebRowInset = 8;
constexpr auto kWebRowRadius = 16;
constexpr auto kWebRowPadding = 9;
constexpr auto kWebRowPhotoSkip = 8;
constexpr auto kWebSearchHeight = 42;
constexpr auto kWebColumnMaxWidth = 728;
constexpr auto kWebColumnMinWidth = 240;
constexpr auto kWebHeaderGap = 8;
constexpr auto kWebHeaderPadding = 4;
constexpr auto kWebComposerGap = 8;
constexpr auto kWebComposerRadius = 24;
constexpr auto kWebFiltersWidth = 104;
constexpr auto kWebBlurRadius = 25;
constexpr auto kWebSendFillPadding = 8;
constexpr auto kWebMenuRadius = 12;
constexpr auto kWebBoxRadius = 16;
constexpr auto kWebSearchIconLeft = 12;
constexpr auto kWebSearchIconSize = 24;
constexpr auto kWebSearchTextSkip = 8;
constexpr auto kWebCardMargin = 16;
constexpr auto kWebCardRadius = 20;
constexpr auto kWebCardGap = 16;
constexpr auto kWebBubbleRadiusLarge = 15;
constexpr auto kWebBubbleRadiusSmall = 6;
constexpr auto kWebDateFontSize = 12;
constexpr auto kWebTabInset = 3;
constexpr auto kWebUnreadHeight = 22;
constexpr auto kWebUnreadFontSize = 14;
constexpr auto kWebTabActiveAlpha = 0.14;
constexpr auto kCollageRadius = 2;
constexpr auto kCollageBubbleRadius = 3;
constexpr auto kCollageBorder = 2;
constexpr auto kCollageShadowOffset = 4;
constexpr auto kCollageTitleFontSize = 18;
constexpr auto kCollageSubsectionFontSize = 15;
constexpr auto kCollageMonoFontSize = 12;
constexpr auto kCollageServiceFontSize = 13;

auto ForcedFamily = QString();
auto BackdropGenerationValue = 0;

struct RadiusSet {
	int small = 0;
	int medium = 0;
	int large = 0;
	int window = 0;
	int bubble = 0;
};

struct RowMetrics {
	int height = 0;
	int photo = 0;
	int padding = 0;
	int nameTop = 0;
	int textTop = 0;
};

[[nodiscard]] RadiusSet RadiiFor(Corners corners) {
	switch (corners) {
	case Corners::Sharp: return { 2, 3, 4, 4, 4 };
	case Corners::Soft: return { 6, 10, 14, 12, 12 };
	case Corners::Round: return { 8, 14, 20, 16, 16 };
	case Corners::Telegram: break;
	}
	return { 3, 6, 10, 6, 16 };
}

[[nodiscard]] int Scaled(int value) {
	return style::ConvertScale(value);
}

// Style values are generated as non-const storage behind const references
// and filled once in style::StartManager(). Adjusting a curated set of them
// right after that call, before any widget reads them, lets the design
// tokens reach existing Telegram widgets without touching their code.
// Values are only changed at startup, so a restart applies new tokens.
template <typename Type>
[[nodiscard]] Type &Mutable(const Type &value) {
	return const_cast<Type&>(value);
}

void ApplyDensity() {
	const auto density = CurrentDensity();
	if (density == Density::Normal) {
		return;
	}
	const auto metrics = (density == Density::Compact)
		? RowMetrics{ 50, 38, 6, 6, 27 }
		: RowMetrics{ 72, 52, 10, 13, 40 };
	auto &row = Mutable(st::defaultDialogRow);
	const auto gap = row.nameLeft - row.padding.left() - row.photoSize;
	row.height = Scaled(metrics.height);
	row.photoSize = Scaled(metrics.photo);
	row.padding = QMargins(
		row.padding.left(),
		Scaled(metrics.padding),
		row.padding.right(),
		Scaled(metrics.padding));
	row.nameLeft = row.padding.left() + row.photoSize + gap;
	row.textLeft = row.nameLeft;
	row.nameTop = Scaled(metrics.nameTop);
	row.textTop = Scaled(metrics.textTop);
	Mutable(st::dialogsRowHeight) = row.height;
}

[[nodiscard]] RowMetrics WebRowMetrics() {
	switch (CurrentDensity()) {
	case Density::Compact: return { 60, 44, 8, 11, 33 };
	case Density::Comfortable: return { 80, 60, 10, 18, 44 };
	case Density::Normal: break;
	}
	return { 72, 54, kWebRowPadding, 15, 39 };
}

void ApplyWebLayout() {
	const auto iconRow = st::infoProfilePersonalChannelPadding.left();
	Mutable(st::infoProfileLabeledPadding).setLeft(iconRow);
	Mutable(st::infoProfileLabeledUsernamePadding).setLeft(iconRow);
	SetWebIconBackground(&st::infoIconInformation, st::historyPeerArchiveUserpicBg);
	SetWebIconBackground(&st::infoIconMediaLink, st::settingsIconBg3);
	SetWebIconBackground(&st::menuIconNotifications, st::settingsIconBg1);
	SetWebIconBackground(&st::menuIconPhone, st::settingsIconBg2);
	SetWebIconBackground(&st::menuIconUsername, st::settingsIconBg4);
	SetWebIconBackground(&st::menuIconIpAddress, st::settingsIconBg5);
	SetWebIconBackground(&st::menuIconAddress, st::settingsIconBg8);
	SetWebIconBackground(&st::infoIconMediaSaved, st::settingsIconBg4);
	const auto metrics = WebRowMetrics();
	const auto side = Scaled(kWebRowInset + kWebRowPadding);
	auto &row = Mutable(st::defaultDialogRow);
	row.height = Scaled(metrics.height);
	row.photoSize = Scaled(metrics.photo);
	row.padding = QMargins(
		side,
		Scaled(metrics.padding),
		side,
		Scaled(metrics.padding));
	row.nameLeft = side + row.photoSize + Scaled(kWebRowPhotoSkip);
	row.textLeft = row.nameLeft;
	row.nameTop = Scaled(metrics.nameTop);
	row.textTop = Scaled(metrics.textTop);
	Mutable(st::dialogsRowHeight) = row.height;
	Mutable(st::dialogsUnreadHeight) = Scaled(kWebUnreadHeight);
	Mutable(st::dialogsUnreadFont) = style::font(
		Scaled(kWebUnreadFontSize),
		st::normalFont->flags(),
		st::dialogsUnreadFont->family());
	Mutable(st::dialogsDateFont) = style::font(
		Scaled(kWebDateFontSize),
		st::dialogsDateFont->flags(),
		st::dialogsDateFont->family());

	auto &search = Mutable(st::dialogsFilter);
	const auto heightAdded = Scaled(kWebSearchHeight) - search.heightMin;
	search.textMargins = QMargins(
		Scaled(kWebSearchIconLeft + kWebSearchIconSize + kWebSearchTextSkip),
		search.textMargins.top() + heightAdded / 2,
		search.textMargins.right(),
		search.textMargins.bottom() + heightAdded - heightAdded / 2);
	search.heightMin = Scaled(kWebSearchHeight);
	search.borderRadius = search.heightMin / 2;
	search.border = Scaled(2);
	search.borderActive = Scaled(2);
	search.borderFgActive = st::activeLineFg;

	static const auto transparent = style::owned_color(QColor(0, 0, 0, 0));
	Mutable(st::windowFiltersWidth) = Scaled(kWebFiltersWidth);
	for (const auto button : {
			&st::windowFiltersButton,
			&st::windowFiltersButtonTextOnly,
			&st::windowFiltersButtonIconsOnly,
			&st::windowFiltersMainMenu }) {
		auto &mutableButton = Mutable(*button);
		mutableButton.textBg = transparent.color();
		mutableButton.textBgActive = transparent.color();
	}

	auto &send = Mutable(st::historySend);
	send.inner.icon = st::aiComposeSendButton.inner.icon;
	send.inner.iconOver = st::aiComposeSendButton.inner.iconOver;
	send.sendIconFillPadding = Scaled(kWebSendFillPadding);
	send.sendIconFg = st::windowFgActive;

	auto &compose = Mutable(st::defaultComposeControls);
	compose.radius = Scaled(kWebComposerRadius);
	compose.send = st::historySend;

	static const auto tabActive = style::complex_color([] {
		auto color = st::windowBgActive->c;
		color.setAlphaF(kWebTabActiveAlpha);
		return color;
	});
	for (const auto tabs : { &st::dialogsSearchTabs, &st::chatsFiltersTabs }) {
		auto &slider = Mutable(*tabs);
		const auto inset = Scaled(kWebTabInset);
		slider.barSnapToLabel = true;
		slider.barTop = inset;
		slider.barStroke = slider.height - 2 * inset;
		slider.barRadius = slider.barStroke / 2;
		slider.barFg = transparent.color();
		slider.barFgActive = tabActive.color();
	}

	Mutable(st::defaultPopupMenu).radius = Scaled(kWebMenuRadius);
	Mutable(st::popupMenuWithIcons).radius = Scaled(kWebMenuRadius);
	Mutable(st::boxRadius) = Scaled(kWebBoxRadius);
	Mutable(st::boxDividerHeight) = Scaled(kWebCardGap);

	const auto column = Scaled(kWebColumnMaxWidth);
	const auto around = 2 * st::msgPhotoSkip + 2 * st::msgMargin.left();
	Mutable(st::msgMaxWidth) = std::max(st::msgMaxWidth, column - around);
	Mutable(st::adaptiveChatWideWidth) = column + 2 * IslandMargin();

	Mutable(st::bubbleRadiusLarge) = Scaled(kWebBubbleRadiusLarge);
	Mutable(st::bubbleRadiusSmall) = Scaled(kWebBubbleRadiusSmall);
	Mutable(st::msgDateFont) = style::font(
		Scaled(kWebDateFontSize),
		st::msgDateFont->flags(),
		st::msgDateFont->family());
}

void ApplyCollage() {
	if (!CollageMode()) {
		return;
	}
	const auto radius = Scaled(kCollageRadius);
	Mutable(st::roundRadiusSmall) = radius;
	Mutable(st::roundRadiusLarge) = radius;
	Mutable(st::buttonRadius) = radius;
	Mutable(st::boxRadius) = radius;
	Mutable(st::defaultPopupMenu).radius = radius;
	Mutable(st::popupMenuWithIcons).radius = radius;
	Mutable(st::defaultImportantTooltip).radius = radius;
	Mutable(st::dialogsFilter).borderRadius = radius;
	Mutable(st::defaultComposeControls).radius = radius;
	Mutable(st::bubbleRadiusLarge) = Scaled(kCollageBubbleRadius);
	Mutable(st::bubbleRadiusSmall) = radius;
	Ui::SetAppliedBubbleRadius(kCollageBubbleRadius);
	for (const auto tabs : { &st::dialogsSearchTabs, &st::chatsFiltersTabs }) {
		Mutable(*tabs).barRadius = 0;
	}
	for (const auto scroll : {
			&st::defaultScrollArea,
			&st::defaultSolidScroll,
			&st::boxScroll,
			&st::historyScroll }) {
		Mutable(*scroll).round = 0;
	}

	const auto mono = [](int size) {
		return style::font(Scaled(size), style::FontFlags(), CollageMonoFamily());
	};
	Mutable(st::msgDateFont) = mono(kCollageMonoFontSize);
	Mutable(st::dialogsDateFont) = mono(kCollageMonoFontSize);
	Mutable(st::dialogsUnreadFont) = mono(kWebUnreadFontSize);
	Mutable(st::msgServiceFont) = mono(kCollageServiceFontSize);

	const auto headline = [](int size) {
		return style::font(
			Scaled(size),
			style::FontFlag::Bold,
			CollageHeadlineFamily());
	};
	Mutable(st::boxTitle).style.font = headline(kCollageTitleFontSize);
	Mutable(st::defaultSubsectionTitle).style.font = headline(
		kCollageSubsectionFontSize);
}

void ApplyCorners() {
	const auto corners = CurrentCorners();
	if (corners == Corners::Telegram) {
		return;
	}
	const auto radii = RadiiFor(corners);
	Mutable(st::roundRadiusSmall) = Scaled(radii.small);
	Mutable(st::roundRadiusLarge) = Scaled(radii.medium);
	Mutable(st::buttonRadius) = Scaled(radii.small);
	Mutable(st::boxRadius) = Scaled(radii.window);
	Mutable(st::defaultPopupMenu).radius = Scaled(radii.medium);
	Mutable(st::popupMenuWithIcons).radius = Scaled(radii.medium);
	Mutable(st::defaultImportantTooltip).radius = Scaled(radii.small);
	Ui::SetAppliedBubbleRadius(radii.bubble);
}

[[nodiscard]] QColor Color(const style::color &color) {
	return color->c;
}

} // namespace

Density CurrentDensity() {
	const auto value = AyuSettings::getInstance().designDensity();
	return (value >= 0 && value <= 2)
		? Density(value)
		: Density::Normal;
}

Corners CurrentCorners() {
	const auto value = AyuSettings::getInstance().designCorners();
	return (value >= 0 && value <= 3)
		? Corners(value)
		: Corners::Telegram;
}

Motion CurrentMotion() {
	const auto value = AyuSettings::getInstance().designMotion();
	return (value >= 0 && value <= 2)
		? Motion(value)
		: Motion::Full;
}

int IslandMargin() {
	return Scaled(kIslandMargin);
}

int IslandRadius() {
	return Scaled(CollageMode() ? kCollageRadius : kIslandRadius);
}

int WebRowInset() {
	return Scaled(kWebRowInset);
}

int WebRowRadius() {
	return Scaled(CollageMode() ? kCollageRadius : kWebRowRadius);
}

int WebColumnWidth(int available) {
	const auto inner = available - 2 * IslandMargin();
	const auto minimal = std::min(Scaled(kWebColumnMinWidth), available);
	return std::clamp(inner, minimal, Scaled(kWebColumnMaxWidth));
}

int WebHeaderGap() {
	return Scaled(kWebHeaderGap);
}

int WebHeaderPadding() {
	return Scaled(kWebHeaderPadding);
}

int WebComposerBottom() {
	return IslandMargin();
}

int WebComposerGap() {
	return Scaled(kWebComposerGap);
}

int WebComposerRadius() {
	return Scaled(CollageMode() ? kCollageRadius : kWebComposerRadius);
}

QRect WebFiltersIsland(QSize column) {
	const auto margin = IslandMargin();
	return QRect(
		margin,
		margin,
		column.width() - margin - margin / 2,
		column.height() - 2 * margin);
}

int WebBlurRadius() {
	return Scaled(kWebBlurRadius);
}

int WebSearchIconLeft() {
	return Scaled(kWebSearchIconLeft);
}

int WebCardMargin() {
	return Scaled(kWebCardMargin);
}

int WebCardRadius() {
	return Scaled(CollageMode() ? kCollageRadius : kWebCardRadius);
}

bool CollageMode() {
	static const auto result = (AyuSettings::getInstance().designTheme()
		== u"collage"_q);
	return result;
}

QString CollageHeadlineFamily() {
	return u"Oswald"_q;
}

QString CollageSerifFamily() {
	return u"PT Serif"_q;
}

QString CollageMonoFamily() {
	return u"PT Mono"_q;
}

int CollageBorder() {
	return Scaled(kCollageBorder);
}

int CollageShadowOffset() {
	return Scaled(kCollageShadowOffset);
}

void SetWebIconBackground(const void *key, const style::color &color) {
	WebIconPresets()[key] = &color;
}

const style::color &WebIconBackground(const void *key) {
	static const auto colors = std::array<const style::color*, 7>{
		&st::settingsIconBg4,
		&st::settingsIconBg2,
		&st::settingsIconBg6,
		&st::settingsIconBg3,
		&st::settingsIconBg1,
		&st::settingsIconBg5,
		&st::settingsIconBg8,
	};
	if (const auto preset = WebIconPresets().find(key)
		; preset != WebIconPresets().end()) {
		return *preset->second;
	}
	static auto assigned = base::flat_map<const void*, int>();
	const auto i = assigned.find(key);
	if (i != assigned.end()) {
		return *colors[i->second];
	}
	const auto index = int(assigned.size() % colors.size());
	assigned.emplace(key, index);
	return *colors[index];
}

int BackdropGeneration() {
	return BackdropGenerationValue;
}

void BumpBackdropGeneration() {
	++BackdropGenerationValue;
}

QRegion RoundedRegion(QSize size, int radius) {
	auto path = QPainterPath();
	const auto limited = std::min({ radius, size.width() / 2, size.height() / 2 });
	path.addRoundedRect(QRectF(QPointF(), QSizeF(size)), limited, limited);
	return QRegion(path.toFillPolygon().toPolygon());
}

QImage WebRowRippleMask(QSize size) {
	const auto radius = WebRowRadius();
	const auto inset = WebRowInset();
	return Ui::RippleAnimation::MaskByDrawer(size, false, [&](QPainter &p) {
		p.drawRoundedRect(
			QRect(QPoint(), size).marginsRemoved({ inset, 0, inset, 0 }),
			radius,
			radius);
	});
}

void PaintWebRowHighlight(QPainter &p, QRect row, const QBrush &brush) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = WebRowRadius();
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawRoundedRect(
		row.marginsRemoved({ WebRowInset(), 0, WebRowInset(), 0 }),
		radius,
		radius);
}

int RadiusPx(Radius radius) {
	const auto radii = RadiiFor(CurrentCorners());
	switch (radius) {
	case Radius::Small: return Scaled(radii.small);
	case Radius::Medium: return Scaled(radii.medium);
	case Radius::Large: return Scaled(radii.large);
	case Radius::Window: return Scaled(radii.window);
	case Radius::Pill: return Scaled(radii.large) * 4;
	}
	return Scaled(radii.medium);
}

float64 DensityFactor() {
	switch (CurrentDensity()) {
	case Density::Compact: return 0.8;
	case Density::Comfortable: return 1.2;
	case Density::Normal: break;
	}
	return 1.;
}

int Dense(int scaledPixels) {
	return int(base::SafeRound(scaledPixels * DensityFactor()));
}

int SpacePx(Space space) {
	const auto base = [&] {
		switch (space) {
		case Space::XXS: return 2;
		case Space::XS: return 4;
		case Space::S: return 8;
		case Space::M: return 12;
		case Space::L: return 16;
		case Space::XL: return 24;
		}
		return 8;
	}();
	return Dense(Scaled(base));
}

crl::time DurationMs(Duration duration) {
	if (anim::Disabled()) {
		return 0;
	}
	const auto base = [&] {
		switch (duration) {
		case Duration::Fast: return crl::time(120);
		case Duration::Normal: return crl::time(200);
		case Duration::Slow: return crl::time(320);
		}
		return crl::time(200);
	}();
	switch (CurrentMotion()) {
	case Motion::Off: return 0;
	case Motion::Reduced: return base / 2;
	case Motion::Full: break;
	}
	return base;
}

float64 OpacityValue(Opacity opacity) {
	switch (opacity) {
	case Opacity::Disabled: return 0.4;
	case Opacity::Secondary: return 0.65;
	case Opacity::Scrim: return 0.5;
	case Opacity::Overlay: return 0.92;
	}
	return 1.;
}

QColor Mix(QColor from, QColor to, float64 progress) {
	const auto part = std::clamp(progress, 0., 1.);
	const auto mix = [&](int a, int b) {
		return int(base::SafeRound(a + (b - a) * part));
	};
	return QColor(
		mix(from.red(), to.red()),
		mix(from.green(), to.green()),
		mix(from.blue(), to.blue()),
		mix(from.alpha(), to.alpha()));
}

QColor Background(Role role, State state) {
	switch (role) {
	case Role::Surface:
		switch (state) {
		case State::Hover: return Color(st::windowBgOver);
		case State::Pressed: return Color(st::windowBgRipple);
		case State::Selected: return Mix(
			Color(st::windowBgOver),
			Color(st::windowBgActive),
			kSelectedAccentMix);
		case State::Normal:
		case State::Disabled: break;
		}
		return Color(st::windowBg);
	case Role::Raised:
		switch (state) {
		case State::Hover: return Color(st::menuBgOver);
		case State::Pressed: return Color(st::menuBgRipple);
		case State::Selected: return Mix(
			Color(st::menuBgOver),
			Color(st::windowBgActive),
			kSelectedAccentMix);
		case State::Normal:
		case State::Disabled: break;
		}
		return Color(st::menuBg);
	case Role::Accent:
		switch (state) {
		case State::Hover:
		case State::Selected: return Color(st::activeButtonBgOver);
		case State::Pressed: return Color(st::activeButtonBgRipple);
		case State::Disabled: return Mix(
			Color(st::activeButtonBg),
			Color(st::windowBg),
			kDisabledAccentMix);
		case State::Normal: break;
		}
		return Color(st::activeButtonBg);
	case Role::Neutral:
		switch (state) {
		case State::Hover:
		case State::Selected: return Color(st::lightButtonBgOver);
		case State::Pressed: return Color(st::lightButtonBgRipple);
		case State::Normal:
		case State::Disabled: break;
		}
		return Color(st::lightButtonBg);
	case Role::Danger:
		switch (state) {
		case State::Hover:
		case State::Selected: return Color(st::attentionButtonBgOver);
		case State::Pressed: return Color(st::attentionButtonBgRipple);
		case State::Normal:
		case State::Disabled: break;
		}
		return Mix(
			Color(st::windowBg),
			Color(st::attentionButtonFg),
			kDangerSurfaceMix);
	}
	return Color(st::windowBg);
}

QColor Foreground(Role role, State state) {
	if (state == State::Disabled) {
		return Color(st::windowSubTextFg);
	}
	switch (role) {
	case Role::Accent: return Color(st::activeButtonFg);
	case Role::Neutral: return Color(st::lightButtonFg);
	case Role::Danger: return Color(st::attentionButtonFg);
	case Role::Surface:
	case Role::Raised: break;
	}
	return Color(st::windowFg);
}

QString PrepareFontFamily(const QString &custom) {
	const auto collage = QDir(u":/gui/chickengram/fonts/collage/"_q).entryInfoList(
		{ u"*.ttf"_q });
	for (const auto &file : collage) {
		QFontDatabase::addApplicationFont(file.absoluteFilePath());
	}
	auto loaded = false;
	const auto files = QDir(u":/gui/chickengram/fonts/"_q).entryInfoList(
		{ u"*.ttf"_q });
	for (const auto &file : files) {
		if (QFontDatabase::addApplicationFont(file.absoluteFilePath()) >= 0) {
			loaded = true;
		}
	}
	if (!loaded) {
		return custom;
	}
	ForcedFamily = u"Chickengram Sans"_q;
	return ForcedFamily;
}

const QString &ForcedFontFamily() {
	return ForcedFamily;
}

bool PrivacyMode() {
	return AyuSettings::getInstance().privacyMode();
}

void ApplyStyleOverrides() {
	ApplyDensity();
	ApplyWebLayout();
	ApplyCorners();
	ApplyCollage();
}

void ApplyMotion() {
	const auto off = (CurrentMotion() == Motion::Off);
	const auto animations = PowerSaving::Flags(PowerSaving::kAnimations);
	const auto current = PowerSaving::Current();
	const auto updated = off
		? (current | animations)
		: (current & ~animations);
	if (updated != current) {
		PowerSaving::Set(updated);
		Core::App().saveSettingsDelayed();
	}
}

void ApplyWindowOpacity(not_null<QWidget*> window) {
	const auto value = std::clamp(
		AyuSettings::getInstance().windowOpacity(),
		kMinWindowOpacity,
		kMaxWindowOpacity);
	const auto opacity = value / float64(kMaxWindowOpacity);
	if (window->windowOpacity() != opacity) {
		window->setWindowOpacity(opacity);
	}
}

void ApplyWindowOpacityToAll() {
	Core::App().enumerateWindows([](not_null<Window::Controller*> window) {
		ApplyWindowOpacity(window->widget().get());
	});
}

} // namespace AyuDesign
