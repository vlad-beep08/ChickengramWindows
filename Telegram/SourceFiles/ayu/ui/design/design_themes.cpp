// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/design/design_themes.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/design/design_system.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "base/zlib_help.h"
#include "data/data_wall_paper.h"
#include "settings.h"
#include "ui/style/style_palette_colorizer.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "window/themes/window_theme_editor_box.h"
#include "styles/palette.h"

#include <QtCore/QRegularExpression>

namespace AyuDesign {
namespace {

constexpr auto kWebWallPaperIntensity = 38;
constexpr auto kDarkLightness = 128;
constexpr auto kWallPaperRetryDelay = crl::time(2000);
constexpr auto kAccentHueThreshold = 15;
constexpr auto kAccentMinChroma = 20;
constexpr auto kAccentThemeFile = "tdata/chickengram-accent.tdesktop-theme"_cs;

[[nodiscard]] QString ThemePath(const QString &id) {
	return u":/gui/chickengram/themes/%1.tdesktop-theme"_q.arg(id);
}

[[nodiscard]] std::optional<QColor> AccentColor(const QString &id) {
	if (id.isEmpty()) {
		return std::nullopt;
	}
	for (const auto &accent : Accents()) {
		if (accent.id == id && accent.color.isValid()) {
			return accent.color;
		}
	}
	return std::nullopt;
}

[[nodiscard]] bool IgnoredAccentKey(const QString &key) {
	static const auto prefixes = std::array{
		u"historyPeer"_q,
		u"call"_q,
		u"groupCall"_q,
		u"mediaview"_q,
		u"stickerPanPremium"_q,
		u"premium"_q,
		u"wallet"_q,
		u"statisticsChart"_q,
		u"settingsIconBg"_q,
		u"msgFile1"_q,
		u"msgFile2"_q,
		u"msgFile3"_q,
		u"msgFile4"_q,
	};
	return ranges::any_of(prefixes, [&](const QString &prefix) {
		return key.startsWith(prefix);
	});
}

[[nodiscard]] QByteArray ColorizePalette(
		const QByteArray &content,
		const style::colorizer &colorizer) {
	static const auto regex = QRegularExpression(
		u"^\\s*([A-Za-z0-9_]+)\\s*:\\s*#([0-9a-fA-F]{6})"_q,
		QRegularExpression::MultilineOption);
	const auto text = QString::fromUtf8(content);
	auto result = QString();
	result.reserve(text.size());
	auto position = 0;
	auto matches = regex.globalMatch(text);
	while (matches.hasNext()) {
		const auto match = matches.next();
		if (IgnoredAccentKey(match.captured(1))) {
			continue;
		}
		const auto color = QColor(u"#"_q + match.captured(2));
		const auto chroma = std::max({ color.red(), color.green(), color.blue() })
			- std::min({ color.red(), color.green(), color.blue() });
		if (chroma < kAccentMinChroma) {
			continue;
		}
		const auto changed = style::colorize(color, colorizer);
		if (!changed) {
			continue;
		}
		const auto start = match.capturedStart(2);
		result.append(text.mid(position, start - position));
		result.append(changed->toRgb().name().mid(1));
		position = start + match.capturedLength(2);
	}
	result.append(text.mid(position));
	return result.toUtf8();
}

[[nodiscard]] QByteArray PackTheme(
		const Window::Theme::ParsedTheme &parsed) {
	zlib::FileToWrite zip;
	zip_fileinfo info = { { 0, 0, 0, 0, 0, 0 }, 0, 0, 0 };
	const auto write = [&](const std::string &name, const QByteArray &data) {
		zip.openNewFile(
			name.c_str(),
			&info,
			nullptr,
			0,
			nullptr,
			0,
			nullptr,
			Z_DEFLATED,
			Z_DEFAULT_COMPRESSION);
		zip.writeInFile(data.constData(), data.size());
		zip.closeFile();
	};
	if (!parsed.background.isEmpty()) {
		write(
			std::string(parsed.tiled ? "tiled" : "background")
				+ (parsed.isPng ? ".png" : ".jpg"),
			parsed.background);
	}
	write("colors.tdesktop-theme", parsed.palette);
	zip.close();
	return (zip.error() == ZIP_OK) ? zip.result() : QByteArray();
}

[[nodiscard]] QString PrepareThemePath(const ThemeInfo &theme) {
	const auto accent = AccentColor(CurrentAccentId());
	if (!accent || theme.swatches.size() < 2) {
		return theme.path;
	}
	auto file = QFile(theme.path);
	if (!file.open(QIODevice::ReadOnly)) {
		return theme.path;
	}
	auto colorizer = style::colorizer();
	colorizer.hueThreshold = kAccentHueThreshold;
	theme.swatches[1].getHsv(
		&colorizer.was.hue,
		&colorizer.was.saturation,
		&colorizer.was.value);
	accent->getHsv(
		&colorizer.now.hue,
		&colorizer.now.saturation,
		&colorizer.now.value);
	auto object = Window::Theme::Object();
	object.pathAbsolute = theme.path;
	object.content = file.readAll();
	auto parsed = Window::Theme::ParseTheme(object, false, false);
	if (parsed.palette.isEmpty()) {
		return theme.path;
	}
	parsed.palette = ColorizePalette(parsed.palette, colorizer);
	const auto content = PackTheme(parsed);
	if (content.isEmpty()) {
		return theme.path;
	}
	const auto path = cWorkingDir() + kAccentThemeFile.utf16();
	auto output = QFile(path);
	if (!output.open(QIODevice::WriteOnly)
		|| output.write(content) != content.size()) {
		return theme.path;
	}
	return path;
}

bool ApplyThemeFile(const ThemeInfo &theme) {
	if (!Window::Theme::Apply(PrepareThemePath(theme))) {
		return false;
	}
	Window::Theme::KeepApplied();
	return true;
}

[[nodiscard]] QString OurThemeId() {
	return u"web-dark-orange"_q;
}

[[nodiscard]] QString CollageThemeId() {
	return u"collage"_q;
}

[[nodiscard]] std::optional<ThemeInfo> FindTheme(const QString &id) {
	for (const auto &theme : Themes()) {
		if (theme.id == id) {
			return theme;
		}
	}
	return std::nullopt;
}

[[nodiscard]] ThemeInfo ChosenTheme() {
	if (const auto theme = FindTheme(CurrentThemeId())) {
		return *theme;
	}
	return *FindTheme(OurThemeId());
}

void ApplyWallPaper(const ThemeInfo &theme) {
	if (theme.wallpaper.empty()) {
		return;
	}
	Window::Theme::Background()->set(
		Data::DefaultWallPaper().withBackgroundColors(
			theme.wallpaper
		).withPatternIntensity(theme.wallpaperIntensity));
}

} // namespace

std::vector<ThemeInfo> Themes() {
	return {
		{
			.id = OurThemeId(),
			.title = tr::ayu_DesignThemeWebDarkOrange(tr::now),
			.path = ThemePath(OurThemeId()),
			.swatches = { QColor(0x21, 0x21, 0x21), QColor(0xdc, 0xa0, 0x6c), QColor(0x9f, 0x5e, 0x1d) },
			.wallpaper = { QColor(0x4f, 0x5b, 0xd5), QColor(0x96, 0x2f, 0xbf), QColor(0xdd, 0x6c, 0xb9), QColor(0xfe, 0xc4, 0x96) },
			.wallpaperIntensity = -kWebWallPaperIntensity,
		},
		{
			.id = CollageThemeId(),
			.title = u"Коллаж"_q,
			.path = ThemePath(CollageThemeId()),
			.swatches = { QColor(0x08, 0x08, 0x08), QColor(0xc1, 0x12, 0x1f), QColor(0xe8, 0xe1, 0xd1) },
		},
	};
}

QString CurrentThemeId() {
	return AyuSettings::getInstance().designTheme();
}

bool ApplyTheme(const QString &id) {
	const auto theme = FindTheme(id);
	if (!theme || !ApplyThemeFile(*theme)) {
		return false;
	}
	ApplyWallPaper(*theme);
	AyuSettings::getInstance().setDesignTheme(id);
	return true;
}

std::vector<AccentInfo> Accents() {
	return {
		{ QString(), tr::ayu_DesignAccentTheme(tr::now), QColor() },
		{ u"blue"_q, tr::ayu_DesignAccentBlue(tr::now), QColor(0x33, 0x90, 0xec) },
		{ u"purple"_q, tr::ayu_DesignAccentPurple(tr::now), QColor(0x87, 0x74, 0xe1) },
		{ u"green"_q, tr::ayu_DesignAccentGreen(tr::now), QColor(0x4f, 0xae, 0x4e) },
		{ u"orange"_q, tr::ayu_DesignAccentOrange(tr::now), QColor(0xe8, 0x8f, 0x3c) },
		{ u"red"_q, tr::ayu_DesignAccentRed(tr::now), QColor(0xe5, 0x53, 0x4b) },
		{ u"pink"_q, tr::ayu_DesignAccentPink(tr::now), QColor(0xe0, 0x5b, 0xa0) },
		{ u"cyan"_q, tr::ayu_DesignAccentCyan(tr::now), QColor(0x26, 0xb5, 0xc9) },
	};
}

QString CurrentAccentId() {
	return AyuSettings::getInstance().designAccent();
}

bool ApplyAccent(const QString &id) {
	AyuSettings::getInstance().setDesignAccent(id);
	const auto theme = ChosenTheme();
	if (CurrentThemeId() != theme.id) {
		return ApplyTheme(theme.id);
	}
	return ApplyThemeFile(theme);
}

void EnsureWebWallPaper() {
	const auto &paper = Window::Theme::Background()->paper();
	if (paper.isPattern()
		|| paper.document()
		|| Data::IsCustomWallPaper(paper)) {
		return;
	}
	static auto lastApplied = crl::time();
	const auto now = crl::now();
	if (lastApplied && (now - lastApplied) < kWallPaperRetryDelay) {
		return;
	}
	lastApplied = now;
	ApplyWallPaper(ChosenTheme());
}

void EnsureWebTheme() {
	auto &settings = Core::App().settings();
	if (settings.systemDarkModeEnabled()) {
		settings.setSystemDarkModeEnabled(false);
		Core::App().saveSettingsDelayed();
	}
	static auto watching = false;
	if (!watching) {
		watching = true;
		const auto lifetime = new rpl::lifetime();
		using Update = Window::Theme::BackgroundUpdate;
		Window::Theme::Background()->updates(
		) | rpl::filter([](const Update &update) {
			return (update.type == Update::Type::New);
		}) | rpl::on_next([] {
			crl::on_main([] {
				EnsureWebWallPaper();
			});
		}, *lifetime);
	}
	const auto light = (st::windowBg->c.lightness() >= kDarkLightness);
	const auto theme = ChosenTheme();
	if (CurrentThemeId() != theme.id || light) {
		ApplyTheme(theme.id);
		return;
	}
	EnsureWebWallPaper();
}

} // namespace AyuDesign
