// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/design/design_islands.h"

#include "ayu/ui/design/design_system.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "styles/palette.h"

namespace AyuDesign {
namespace {

constexpr auto kShadowLayers = 4;
constexpr auto kIslandShadowOpacity = 0.06;
constexpr auto kPillShadowOpacity = 0.1;

void PaintPaperEdge(QPainter &p, QRect rect, int radius) {
	const auto border = CollageBorder();
	const auto offset = CollageShadowOffset();
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgRipple);
	p.drawRoundedRect(rect.translated(offset, offset), radius, radius);
	p.setBrush(st::inputBorderFg);
	p.drawRoundedRect(
		rect.marginsAdded({ border, border, border, border }),
		radius + border,
		radius + border);
}

} // namespace

void PaintSoftShadow(
		QPainter &p,
		QRect rect,
		int radius,
		float64 opacity) {
	if (rect.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(Qt::NoBrush);
	for (auto layer = 0; layer != kShadowLayers; ++layer) {
		const auto spread = layer + 0.5;
		auto color = QColor(0, 0, 0);
		color.setAlphaF(opacity * (kShadowLayers - layer) / kShadowLayers);
		p.setPen(QPen(color, 1.));
		p.drawRoundedRect(
			QRectF(rect).adjusted(-spread, 1. - spread, spread, 1. + spread),
			radius + spread,
			radius + spread);
	}
}

void PaintIslandShadow(QPainter &p, QRect island) {
	if (island.isEmpty()) {
		return;
	} else if (CollageMode()) {
		PaintPaperEdge(p, island, IslandRadius());
		return;
	}
	PaintSoftShadow(p, island, IslandRadius(), kIslandShadowOpacity);
}

void PaintPillShadow(QPainter &p, QRect pill, int radius) {
	if (pill.isEmpty()) {
		return;
	} else if (CollageMode()) {
		PaintPaperEdge(p, pill, std::min(radius, pill.height() / 2));
		return;
	}
	PaintSoftShadow(p, pill, radius, kPillShadowOpacity);
}

void PaintPillSurface(
		QPainter &p,
		QRect pill,
		int radius,
		const QBrush &brush) {
	if (pill.isEmpty()) {
		return;
	}
	const auto limited = std::min(radius, pill.height() / 2);
	PaintPillShadow(p, pill, limited);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawRoundedRect(pill, limited, limited);
}

IslandCorners::IslandCorners(
	not_null<QWidget*> parent,
	Fn<void(QPainter&, QRect)> paintBackdrop)
: _paintBackdrop(std::move(paintBackdrop)) {
	for (auto &corner : _corners) {
		corner.widget = base::make_unique_q<Ui::RpWidget>(parent);
		const auto raw = corner.widget.get();
		raw->setAttribute(Qt::WA_TransparentForMouseEvents);
		raw->hide();
		raw->paintRequest() | rpl::on_next([=, &corner](QRect) {
			paintCorner(corner);
		}, raw->lifetime());
	}
}

IslandCorners::~IslandCorners() = default;

void IslandCorners::setIsland(QRect island) {
	if (_island == island) {
		return;
	}
	_island = island;
	updateGeometry();
}

void IslandCorners::setVisible(bool visible) {
	if (_visible == visible) {
		return;
	}
	_visible = visible;
	updateGeometry();
}

void IslandCorners::bindVisibility(not_null<Ui::RpWidget*> target) {
	target->shownValue(
	) | rpl::on_next([=](bool shown) {
		setVisible(shown);
	}, _lifetime);
}

void IslandCorners::raise() {
	for (const auto &corner : _corners) {
		corner.widget->raise();
	}
}

void IslandCorners::refresh() {
	for (auto &corner : _corners) {
		corner.cache = QImage();
		corner.widget->update();
	}
}

void IslandCorners::updateGeometry() {
	const auto size = IslandRadius();
	const auto visible = _visible
		&& (_island.width() >= 2 * size)
		&& (_island.height() >= 2 * size);
	const auto left = _island.x();
	const auto top = _island.y();
	const auto right = _island.x() + _island.width() - size;
	const auto bottom = _island.y() + _island.height() - size;
	const auto rects = std::array<QRect, 4>{ {
		QRect(left, top, size, size),
		QRect(right, top, size, size),
		QRect(left, bottom, size, size),
		QRect(right, bottom, size, size),
	} };
	for (auto i = 0; i != int(_corners.size()); ++i) {
		auto &corner = _corners[i];
		corner.cache = QImage();
		corner.widget->setGeometry(rects[i]);
		corner.widget->setVisible(visible);
		corner.widget->update();
	}
}

void IslandCorners::paintCorner(Corner &corner) {
	const auto widget = corner.widget.get();
	const auto geometry = widget->geometry();
	const auto ratio = style::DevicePixelRatio();
	if (corner.cache.size() != geometry.size() * ratio) {
		corner.cache = QImage(
			geometry.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		corner.cache.setDevicePixelRatio(ratio);
		corner.cache.fill(Qt::transparent);
		auto q = QPainter(&corner.cache);
		q.translate(-geometry.topLeft());
		_paintBackdrop(q, geometry);
		PaintIslandShadow(q, _island);
		auto hq = PainterHighQualityEnabler(q);
		const auto radius = IslandRadius();
		q.setCompositionMode(QPainter::CompositionMode_Clear);
		q.setPen(Qt::NoPen);
		q.setBrush(Qt::black);
		q.drawRoundedRect(_island, radius, radius);
	}
	auto p = QPainter(widget);
	p.drawImage(0, 0, corner.cache);
}

} // namespace AyuDesign
