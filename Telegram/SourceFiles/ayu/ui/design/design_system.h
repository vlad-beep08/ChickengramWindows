// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace AyuDesign {

enum class Density {
	Compact = 0,
	Normal = 1,
	Comfortable = 2,
};

enum class Corners {
	Telegram = 0,
	Sharp = 1,
	Soft = 2,
	Round = 3,
};

enum class Motion {
	Full = 0,
	Reduced = 1,
	Off = 2,
};

enum class Radius {
	Small,
	Medium,
	Large,
	Window,
	Pill,
};

enum class Space {
	XXS,
	XS,
	S,
	M,
	L,
	XL,
};

enum class Duration {
	Fast,
	Normal,
	Slow,
};

enum class Opacity {
	Disabled,
	Secondary,
	Scrim,
	Overlay,
};

enum class Role {
	Surface,
	Raised,
	Accent,
	Neutral,
	Danger,
};

enum class State {
	Normal,
	Hover,
	Pressed,
	Selected,
	Disabled,
};

[[nodiscard]] Density CurrentDensity();
[[nodiscard]] Corners CurrentCorners();
[[nodiscard]] Motion CurrentMotion();
[[nodiscard]] constexpr bool WebLayout() {
	return true;
}
[[nodiscard]] bool CollageMode();
[[nodiscard]] QString CollageHeadlineFamily();
[[nodiscard]] QString CollageSerifFamily();
[[nodiscard]] QString CollageMonoFamily();
[[nodiscard]] int CollageBorder();
[[nodiscard]] int CollageShadowOffset();

[[nodiscard]] int IslandMargin();
[[nodiscard]] int IslandRadius();
[[nodiscard]] int WebRowInset();
[[nodiscard]] int WebRowRadius();
[[nodiscard]] int WebColumnWidth(int available);
[[nodiscard]] int WebHeaderGap();
[[nodiscard]] int WebHeaderPadding();
[[nodiscard]] int WebComposerBottom();
[[nodiscard]] int WebComposerGap();
[[nodiscard]] int WebComposerRadius();
[[nodiscard]] QRegion RoundedRegion(QSize size, int radius);
[[nodiscard]] QRect WebFiltersIsland(QSize column);
[[nodiscard]] int WebBlurRadius();
[[nodiscard]] int WebSearchIconLeft();
[[nodiscard]] int WebCardMargin();
[[nodiscard]] int WebCardRadius();
[[nodiscard]] const style::color &WebIconBackground(const void *key);
void SetWebIconBackground(const void *key, const style::color &color);
[[nodiscard]] int BackdropGeneration();
void BumpBackdropGeneration();
[[nodiscard]] QImage WebRowRippleMask(QSize size);
void PaintWebRowHighlight(QPainter &p, QRect row, const QBrush &brush);

[[nodiscard]] int RadiusPx(Radius radius);
[[nodiscard]] int SpacePx(Space space);
[[nodiscard]] float64 DensityFactor();
[[nodiscard]] int Dense(int scaledPixels);
[[nodiscard]] crl::time DurationMs(Duration duration);
[[nodiscard]] float64 OpacityValue(Opacity opacity);

[[nodiscard]] QColor Background(Role role, State state);
[[nodiscard]] QColor Foreground(Role role, State state);
[[nodiscard]] QColor Mix(QColor from, QColor to, float64 progress);

[[nodiscard]] QString PrepareFontFamily(const QString &custom);
[[nodiscard]] const QString &ForcedFontFamily();
[[nodiscard]] bool PrivacyMode();
void ApplyStyleOverrides();
void ApplyMotion();
void ApplyWindowOpacity(not_null<QWidget*> window);
void ApplyWindowOpacityToAll();

} // namespace AyuDesign
