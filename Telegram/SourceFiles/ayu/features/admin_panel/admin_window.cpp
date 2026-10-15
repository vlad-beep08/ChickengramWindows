#include "ayu/features/admin_panel/admin_window.h"

#include "ayu/features/admin_panel/admin_api.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "settings/settings_common.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/layer_widget.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>
#include <QtGui/QCursor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>

namespace AyuFeatures::AdminPanel {
namespace {

using Layout = Ui::VerticalLayout;

constexpr auto kForeverAfter = qint64(50) * 365 * 86400;
constexpr auto kSearchDelay = crl::time(500);
constexpr auto kMaxAmount = qint64(1'000'000'000'000'000);

struct Option {
	QString key;
	QString title;
};

using Choice = std::shared_ptr<rpl::variable<QString>>;

struct Series {
	QString name;
	std::vector<std::optional<double>> values;
	not_null<const style::color*> color;
	Fn<QString(double)> format;
};

struct ListQuery {
	QString q;
	QString sort;
	QString dir = u"desc"_q;
	int page = 0;
};

[[nodiscard]] const QLocale &Locale() {
	static const auto result = QLocale(QLocale::Russian, QLocale::Russia);
	return result;
}

[[nodiscard]] QJsonValue At(const QJsonObject &object, const char *key) {
	return object.value(QString::fromLatin1(key));
}

[[nodiscard]] QJsonObject Obj(const QJsonObject &object, const char *key) {
	return At(object, key).toObject();
}

[[nodiscard]] QJsonArray Arr(const QJsonObject &object, const char *key) {
	return At(object, key).toArray();
}

[[nodiscard]] double Num(const QJsonValue &value) {
	return value.isString() ? value.toString().toDouble() : value.toDouble();
}

[[nodiscard]] QString Raw(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString();
	} else if (value.isBool()) {
		return value.toBool() ? u"true"_q : u"false"_q;
	} else if (!value.isDouble()) {
		return QString();
	}
	const auto number = value.toDouble();
	return (number == std::floor(number) && std::abs(number) < 9e15)
		? QString::number(qint64(number))
		: QString::number(number, 'g', 12);
}

[[nodiscard]] QString Str(const QJsonObject &object, const char *key) {
	return Raw(At(object, key));
}

[[nodiscard]] QString FmtNumber(double value) {
	const auto abs = std::abs(value);
	if (value == std::floor(value) && abs < 9e15) {
		return Locale().toString(qlonglong(value));
	}
	const auto digits = (abs >= 1000) ? 0 : (abs >= 1) ? 2 : 6;
	auto result = Locale().toString(value, 'f', digits);
	if (digits > 0) {
		while (result.endsWith(QChar(u'0'))) {
			result.chop(1);
		}
		if (result.endsWith(Locale().decimalPoint())) {
			result.chop(1);
		}
	}
	return result;
}

[[nodiscard]] QString Fmt(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString();
	} else if (value.isBool()) {
		return value.toBool() ? u"да"_q : u"нет"_q;
	} else if (value.isDouble()) {
		return FmtNumber(value.toDouble());
	}
	return u"—"_q;
}

[[nodiscard]] QString Short(double value) {
	const auto abs = std::abs(value);
	const auto cut = [&](double divider, const QString &unit) {
		const auto part = value / divider;
		return Locale().toString(part, 'f', (std::abs(part) < 10) ? 1 : 0)
			+ u" "_q
			+ unit;
	};
	if (abs >= 1e9) {
		return cut(1e9, u"млрд"_q);
	} else if (abs >= 1e6) {
		return cut(1e6, u"млн"_q);
	} else if (abs >= 1e5) {
		return cut(1e3, u"тыс"_q);
	}
	return FmtNumber(value);
}

[[nodiscard]] QString ShortValue(const QJsonValue &value) {
	return value.isString() ? value.toString() : Short(value.toDouble());
}

[[nodiscard]] QString Usd(const QJsonValue &value) {
	return Fmt(value) + u" $"_q;
}

[[nodiscard]] QString Eur(const QJsonValue &value) {
	return Fmt(value) + u" €"_q;
}

[[nodiscard]] QString Money(const QJsonValue &value, const QString &currency) {
	return (currency == u"coins"_q)
		? Eur(value)
		: (currency == u"grains"_q)
		? Usd(value)
		: Fmt(value);
}

[[nodiscard]] QString CurrencyName(const QString &currency) {
	return (currency == u"coins"_q)
		? u"Евро"_q
		: (currency == u"grains"_q)
		? u"Доллары"_q
		: currency;
}

[[nodiscard]] qint64 Now() {
	return QDateTime::currentSecsSinceEpoch();
}

[[nodiscard]] QString Date(const QJsonValue &value, bool withTime = true) {
	const auto seconds = qint64(Num(value));
	if (seconds <= 0) {
		return u"—"_q;
	}
	return QDateTime::fromSecsSinceEpoch(seconds).toString(withTime
		? u"dd.MM.yyyy HH:mm"_q
		: u"dd.MM.yyyy"_q);
}

[[nodiscard]] QString Span(qint64 seconds) {
	seconds = std::max(seconds, qint64(0));
	const auto days = seconds / 86400;
	const auto hours = (seconds % 86400) / 3600;
	const auto minutes = (seconds % 3600) / 60;
	if (days) {
		return u"%1 дн %2 ч"_q.arg(days).arg(hours);
	} else if (hours) {
		return u"%1 ч %2 мин"_q.arg(hours).arg(minutes);
	}
	return u"%1 мин"_q.arg(minutes);
}

[[nodiscard]] QString Ago(const QJsonValue &value) {
	const auto seconds = qint64(Num(value));
	if (seconds <= 0) {
		return u"—"_q;
	}
	const auto diff = Now() - seconds;
	if (diff < 300) {
		return u"в сети"_q;
	} else if (diff < 2 * 86400) {
		return Span(diff) + u" назад"_q;
	}
	return Date(value, false);
}

[[nodiscard]] QString Size(double bytes) {
	const auto units = QStringList{
		u"Б"_q,
		u"КБ"_q,
		u"МБ"_q,
		u"ГБ"_q,
		u"ТБ"_q,
	};
	auto unit = 0;
	while (bytes >= 1024 && unit + 1 < int(units.size())) {
		bytes /= 1024;
		++unit;
	}
	return Locale().toString(bytes, 'f', unit ? 1 : 0) + u" "_q + units[unit];
}

[[nodiscard]] QString VipText(const QJsonValue &until) {
	return (Num(until) - Now() > kForeverAfter)
		? u"навсегда"_q
		: (u"до "_q + Date(until, false));
}

[[nodiscard]] QString DayLabel(const QString &day) {
	return day.mid(8, 2) + u"."_q + day.mid(5, 2);
}

[[nodiscard]] QString StripTags(QString text) {
	return text.remove(QRegularExpression(u"<[^>]+>"_q));
}

[[nodiscard]] QString Factor(double value) {
	return u"×"_q + FmtNumber(std::round(value * 1000.) / 1000.);
}

[[nodiscard]] bool LooksLikeTime(const QString &column, const QJsonValue &value) {
	static const auto names = QRegularExpression(
		u"(^|_)(at|until|seen|created|started|joined|time|last|expires"
		"|added|used|activated|day)($|_)"_q);
	return value.isDouble()
		&& value.toDouble() > 1e9
		&& value.toDouble() < 4e9
		&& names.match(column).hasMatch();
}

[[nodiscard]] QString Query(
		std::initializer_list<std::pair<QString, QString>> items) {
	auto result = QStringList();
	for (const auto &[key, value] : items) {
		result.push_back(key
			+ u"="_q
			+ QString::fromLatin1(QUrl::toPercentEncoding(value)));
	}
	return result.join(u"&"_q);
}

[[nodiscard]] std::vector<double> NiceTicks(double max) {
	if (!(max > 0)) {
		return { 0., 1. };
	}
	const auto raw = max / 4.;
	const auto magnitude = std::pow(10., std::floor(std::log10(raw)));
	auto step = magnitude * 10.;
	for (const auto multiplier : { 1., 2., 2.5, 5., 10. }) {
		if (multiplier * magnitude >= raw) {
			step = multiplier * magnitude;
			break;
		}
	}
	auto result = std::vector<double>();
	for (auto value = 0.; value < max + step * 0.999; value += step) {
		result.push_back(value);
	}
	return result;
}

[[nodiscard]] std::vector<std::optional<double>> Values(
		const QJsonArray &array) {
	auto result = std::vector<std::optional<double>>();
	result.reserve(array.size());
	for (const auto &value : array) {
		result.push_back(value.isNull()
			? std::nullopt
			: std::make_optional(Num(value)));
	}
	return result;
}

[[nodiscard]] style::margins RowPadding() {
	const auto skip = style::ConvertScale(4);
	return st::boxRowPadding + style::margins(0, skip, 0, skip);
}

not_null<Ui::FlatLabel*> AddText(
		not_null<Layout*> container,
		const QString &text,
		bool sub = false) {
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			text,
			sub ? st::defaultSubTextLabel : st::boxLabel),
		RowPadding());
	label->setSelectable(true);
	return label;
}

void AddTitle(not_null<Layout*> container, const QString &text) {
	Ui::AddSubsectionTitle(container, rpl::single(text));
}

void AddSection(not_null<Layout*> container, const QString &text) {
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	AddTitle(container, text);
}

not_null<Ui::SettingsButton*> AddButton(
		not_null<Layout*> container,
		const QString &text,
		Fn<void()> callback,
		bool danger = false) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		rpl::single(text),
		danger ? st::settingsAttentionButton : st::settingsButtonNoIcon));
	button->setClickedCallback(std::move(callback));
	return button;
}

not_null<Ui::SettingsButton*> AddRow(
		not_null<Layout*> container,
		const QString &text,
		const QString &label,
		Fn<void()> callback = nullptr) {
	const auto button = Settings::AddButtonWithLabel(
		container,
		rpl::single(text),
		rpl::single(label),
		st::settingsButtonNoIcon);
	if (callback) {
		button->setClickedCallback(std::move(callback));
	} else {
		button->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	return button;
}

not_null<Ui::InputField*> AddField(
		not_null<Layout*> container,
		const QString &placeholder,
		const QString &value = QString(),
		bool multiline = false) {
	return container->add(
		object_ptr<Ui::InputField>(
			container,
			multiline ? st::newGroupDescription : st::defaultInputField,
			(multiline
				? Ui::InputField::Mode::MultiLine
				: Ui::InputField::Mode::SingleLine),
			rpl::single(placeholder),
			value),
		RowPadding());
}

not_null<Ui::Checkbox*> AddCheck(
		not_null<Layout*> container,
		const QString &text,
		bool checked) {
	const auto skip = style::ConvertScale(8);
	return container->add(
		object_ptr<Ui::Checkbox>(
			container,
			text,
			checked,
			st::defaultBoxCheckbox),
		st::boxRowPadding + style::margins(0, skip, 0, skip));
}

Choice AddChoice(
		not_null<Layout*> container,
		const QString &title,
		std::vector<Option> options,
		const QString &initial,
		Fn<void(QString)> changed = nullptr) {
	const auto state = std::make_shared<rpl::variable<QString>>(initial);
	auto label = state->value() | rpl::map([=](const QString &key) {
		for (const auto &option : options) {
			if (option.key == key) {
				return option.title;
			}
		}
		return key;
	});
	const auto button = Settings::AddButtonWithLabel(
		container,
		rpl::single(title),
		std::move(label),
		st::settingsButtonNoIcon);
	button->setClickedCallback([=] {
		const auto menu = Ui::CreateChild<Ui::PopupMenu>(
			button.get(),
			st::defaultPopupMenu);
		for (const auto &option : options) {
			const auto key = option.key;
			menu->addAction(option.title, [=] {
				if (state->current() == key) {
					return;
				}
				*state = key;
				if (changed) {
					changed(key);
				}
			});
		}
		menu->popup(QCursor::pos());
	});
	return state;
}

class Chart final : public Ui::RpWidget {
public:
	Chart(
		QWidget *parent,
		QStringList days,
		std::vector<Series> series,
		bool line);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] QRectF plot() const;
	void paintBars(QPainter &p, const QRectF &rect, double top, double band);
	void paintLine(QPainter &p, const QRectF &rect, double top, double band);
	void paintCaption(QPainter &p, const QRectF &rect);

	const QStringList _days;
	const std::vector<Series> _series;
	const bool _line = false;
	std::vector<double> _ticks;
	int _selected = -1;

};

Chart::Chart(
	QWidget *parent,
	QStringList days,
	std::vector<Series> series,
	bool line)
: RpWidget(parent)
, _days(std::move(days))
, _series(std::move(series))
, _line(line) {
	auto max = 0.;
	for (const auto &one : _series) {
		for (const auto &value : one.values) {
			if (value) {
				max = std::max(max, *value);
			}
		}
	}
	_ticks = NiceTicks(max);
	setMouseTracking(true);
}

int Chart::resizeGetHeight(int newWidth) {
	return style::ConvertScale(210);
}

QRectF Chart::plot() const {
	const auto font = st::normalFont->height;
	const auto top = font + style::ConvertScale(10);
	const auto bottom = font + style::ConvertScale(6);
	const auto left = style::ConvertScale(58);
	return QRectF(
		left,
		top,
		std::max(width() - left - style::ConvertScale(6), 1),
		std::max(height() - top - bottom, 1));
}

void Chart::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto rect = plot();
	const auto count = int(_days.size());
	const auto font = st::normalFont->height;
	const auto top = (_ticks.back() > 0) ? _ticks.back() : 1.;
	p.setFont(st::normalFont);
	for (const auto tick : _ticks) {
		const auto y = rect.y() + rect.height() * (1. - tick / top);
		p.fillRect(
			QRectF(rect.x(), y, rect.width(), st::lineWidth),
			st::windowBgOver->c);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRectF(0, y - font / 2., rect.x() - style::ConvertScale(8), font),
			Short(tick),
			QTextOption(Qt::AlignRight | Qt::AlignVCenter));
	}
	if (!count) {
		return;
	}
	const auto band = rect.width() / count;
	if (_line) {
		paintLine(p, rect, top, band);
	} else {
		paintBars(p, rect, top, band);
	}
	const auto fit = std::max(3., rect.width() / style::ConvertScale(64));
	const auto every = std::max(1, int(std::ceil(count / fit)));
	p.setPen(st::windowSubTextFg);
	for (auto i = 0; i != count; ++i) {
		if ((count - 1 - i) % every) {
			continue;
		}
		const auto x = rect.x() + band * (i + 0.5);
		const auto half = style::ConvertScale(30);
		p.drawText(
			QRectF(
				x - half,
				rect.y() + rect.height() + style::ConvertScale(4),
				half * 2,
				font),
			DayLabel(_days[i]),
			QTextOption(Qt::AlignHCenter | Qt::AlignTop));
	}
	paintCaption(p, rect);
}

void Chart::paintCaption(QPainter &p, const QRectF &rect) {
	const auto font = st::normalFont->height;
	if (_selected >= 0 && _selected < int(_days.size())) {
		auto parts = QStringList();
		for (const auto &one : _series) {
			const auto value = (_selected < int(one.values.size()))
				? one.values[_selected]
				: std::nullopt;
			parts.push_back(one.name
				+ u": "_q
				+ (value ? one.format(*value) : u"—"_q));
		}
		p.setPen(st::windowFg);
		p.drawText(
			QRectF(rect.x(), 0, rect.width(), font),
			DayLabel(_days[_selected]) + u" · "_q + parts.join(u" · "_q),
			QTextOption(Qt::AlignLeft | Qt::AlignTop));
		return;
	}
	auto x = rect.x();
	const auto square = style::ConvertScale(10);
	for (const auto &one : _series) {
		p.fillRect(
			QRectF(x, (font - square) / 2., square, square),
			(*one.color)->c);
		x += square + style::ConvertScale(6);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRectF(x, 0, rect.width(), font),
			one.name,
			QTextOption(Qt::AlignLeft | Qt::AlignTop));
		x += st::normalFont->width(one.name) + style::ConvertScale(16);
	}
}

void Chart::paintBars(
		QPainter &p,
		const QRectF &rect,
		double top,
		double band) {
	PainterHighQualityEnabler hq(p);
	const auto n = int(_series.size());
	const auto gap = double(style::ConvertScale(2));
	const auto barWidth = std::clamp(
		(band - gap * (n + 1)) / n,
		1.,
		double(style::ConvertScale(24)));
	const auto group = n * barWidth + (n - 1) * gap;
	const auto base = rect.y() + rect.height();
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != int(_days.size()); ++i) {
		for (auto j = 0; j != n; ++j) {
			const auto &values = _series[j].values;
			const auto value = (i < int(values.size()) && values[i])
				? *values[i]
				: 0.;
			const auto height = rect.height() * (value / top);
			if (height <= 0.) {
				continue;
			}
			auto color = (*_series[j].color)->c;
			if (_selected >= 0 && _selected != i) {
				color.setAlphaF(color.alphaF() * 0.55);
			}
			p.setBrush(color);
			const auto x = rect.x()
				+ band * i
				+ (band - group) / 2.
				+ j * (barWidth + gap);
			const auto radius = std::min({
				barWidth / 2.,
				height,
				double(style::ConvertScale(3)),
			});
			p.drawRoundedRect(
				QRectF(x, base - height, barWidth, height),
				radius,
				radius);
		}
	}
}

void Chart::paintLine(
		QPainter &p,
		const QRectF &rect,
		double top,
		double band) {
	PainterHighQualityEnabler hq(p);
	const auto &one = _series.front();
	const auto color = (*one.color)->c;
	const auto base = rect.y() + rect.height();
	const auto x = [&](int i) {
		return rect.x() + band * (i + 0.5);
	};
	const auto y = [&](double value) {
		return base - rect.height() * (value / top);
	};
	auto line = QPainterPath();
	auto area = QPainterPath();
	auto started = false;
	auto last = -1;
	for (auto i = 0; i != int(one.values.size()); ++i) {
		const auto value = one.values[i];
		if (!value) {
			if (started) {
				area.lineTo(x(last), base);
				area.closeSubpath();
				started = false;
			}
			continue;
		}
		if (!started) {
			line.moveTo(x(i), y(*value));
			area.moveTo(x(i), base);
			area.lineTo(x(i), y(*value));
			started = true;
		} else {
			line.lineTo(x(i), y(*value));
			area.lineTo(x(i), y(*value));
		}
		last = i;
	}
	if (started) {
		area.lineTo(x(last), base);
		area.closeSubpath();
	}
	if (last < 0) {
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect,
			u"Данных пока нет — они копятся раз в час."_q,
			QTextOption(Qt::AlignCenter));
		return;
	}
	auto fill = color;
	fill.setAlphaF(0.12);
	p.fillPath(area, fill);
	p.setPen(QPen(color, style::ConvertScale(2)));
	p.setBrush(Qt::NoBrush);
	p.drawPath(line);
	const auto dot = (_selected >= 0
		&& _selected < int(one.values.size())
		&& one.values[_selected])
		? _selected
		: last;
	const auto radius = double(style::ConvertScale(4));
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawEllipse(QPointF(x(dot), y(*one.values[dot])), radius, radius);
}

void Chart::mouseMoveEvent(QMouseEvent *e) {
	const auto rect = plot();
	const auto count = int(_days.size());
	auto selected = -1;
	if (count > 0) {
		const auto index = int(std::floor(
			(e->pos().x() - rect.x()) / (rect.width() / count)));
		selected = std::clamp(index, 0, count - 1);
	}
	if (_selected != selected) {
		_selected = selected;
		update();
	}
}

void Chart::leaveEventHook(QEvent *e) {
	if (_selected >= 0) {
		_selected = -1;
		update();
	}
}

class Panel final : public base::has_weak_ptr {
public:
	Panel();

	void activate();

private:
	struct Page {
		QString title;
		Fn<void()> show;
	};
	using Render = Fn<void(not_null<Layout*>, const QJsonObject&)>;

	void open(const QString &title, Fn<void()> show);
	void back();
	void home();
	void reload();
	void showTop(bool keepScroll);

	[[nodiscard]] not_null<Layout*> shell();
	void load(const QString &path, Render render);
	void post(
		const QString &path,
		const QJsonObject &body,
		Fn<void()> after = nullptr);
	void ask(const QString &text, const QString &button, Fn<void()> done);
	void toast(const QString &text);
	[[nodiscard]] std::optional<qint64> number(
		not_null<Ui::InputField*> field,
		qint64 min,
		qint64 max);

	void openPerson(const QString &id, const QString &name);
	void openGroup(const QString &id, const QString &title);
	void openTable(const QString &name);

	void renderHome(not_null<Layout*> content, const QJsonObject &data);
	void renderCharts(not_null<Layout*> content, const QJsonObject &data);
	void showPeople();
	void fillPeople(not_null<Layout*> list);
	void renderPerson(
		not_null<Layout*> content,
		const QString &id,
		const QJsonObject &data);
	void renderGroups(not_null<Layout*> content, const QJsonObject &data);
	void renderGroup(
		not_null<Layout*> content,
		const QString &id,
		const QJsonObject &data);
	void renderEconomy(not_null<Layout*> content, const QJsonObject &data);
	void renderServer(not_null<Layout*> content, const QJsonObject &data);
	void fillLog(not_null<Ui::FlatLabel*> label);
	void downloadDatabase();
	void renderTables(not_null<Layout*> content, const QJsonObject &data);
	void showTable(const QString &name);
	void fillTable(not_null<Layout*> list, const QString &name);

	std::unique_ptr<Ui::SeparatePanel> _panel;
	std::vector<Page> _pages;
	QPointer<Ui::ScrollArea> _scroll;
	int _generation = 0;
	int _listGeneration = 0;
	bool _keepScroll = false;

	int _days = 30;
	QString _flows = u"day"_q;
	int _logLines = 300;
	bool _logErrors = false;
	bool _everywhere = true;
	ListQuery _people;
	std::map<QString, ListQuery> _tables;

};

[[nodiscard]] Panel *&Instance() {
	static auto result = (Panel*)nullptr;
	return result;
}

Panel::Panel()
: _panel(std::make_unique<Ui::SeparatePanel>()) {
	_people.sort = u"messages"_q;
	_panel->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	_panel->setInnerSize(
		QSize(style::ConvertScale(600), style::ConvertScale(660)),
		true);
	_panel->closeRequests(
	) | rpl::on_next([=] {
		_panel->hideGetDuration();
	}, _panel->lifetime());
	_panel->closeEvents(
	) | rpl::on_next([=] {
		crl::on_main([] {
			delete base::take(Instance());
		});
	}, _panel->lifetime());
	_panel->backRequests(
	) | rpl::on_next([=] {
		back();
	}, _panel->lifetime());
	open(u"🐔 Админка Чикена"_q, [=] {
		load(u"overview"_q, [=](not_null<Layout*> content, const QJsonObject &data) {
			renderHome(content, data);
		});
	});
	_panel->showAndActivate();
}

void Panel::activate() {
	_panel->showAndActivate();
}

void Panel::open(const QString &title, Fn<void()> show) {
	_pages.push_back({ title, std::move(show) });
	showTop(false);
}

void Panel::back() {
	if (_pages.size() > 1) {
		_pages.pop_back();
		showTop(false);
	}
}

void Panel::home() {
	_pages.resize(1);
	showTop(false);
}

void Panel::reload() {
	showTop(true);
}

void Panel::showTop(bool keepScroll) {
	crl::on_main(this, [=] {
		if (_pages.empty()) {
			return;
		}
		const auto page = _pages.back();
		_panel->setTitle(rpl::single(page.title));
		_panel->setBackAllowed(_pages.size() > 1);
		_keepScroll = keepScroll;
		page.show();
	});
}

not_null<Layout*> Panel::shell() {
	auto inner = base::make_unique_q<Ui::RpWidget>(_panel.get());
	const auto raw = inner.get();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(raw, st::boxScroll);
	const auto content = scroll->setOwnedWidget(
		object_ptr<Layout>(scroll)).data();
	raw->sizeValue(
	) | rpl::on_next([=](QSize size) {
		scroll->setGeometry(QRect(QPoint(), size));
		content->resizeToWidth(size.width());
	}, raw->lifetime());
	_scroll = scroll;
	_panel->showInner(std::move(inner));
	return content;
}

void Panel::load(const QString &path, Render render) {
	const auto generation = ++_generation;
	const auto keep = base::take(_keepScroll);
	const auto top = (keep && _scroll) ? _scroll->scrollTop() : 0;
	if (!keep) {
		AddText(shell(), u"Загружаю…"_q, true);
	}
	Get(path, crl::guard(this, [=](QJsonObject data) {
		if (generation != _generation) {
			return;
		}
		const auto content = shell();
		render(content, data);
		Ui::AddSkip(content);
		content->resizeToWidth(_scroll->width());
		if (top > 0) {
			_scroll->scrollToY(top);
		}
	}), crl::guard(this, [=](QString error) {
		if (generation != _generation) {
			return;
		}
		const auto content = shell();
		AddText(content, u"Не получилось: "_q + error);
		AddButton(content, u"🔄 Повторить"_q, [=] { reload(); });
		content->resizeToWidth(_scroll->width());
	}));
}

void Panel::post(
		const QString &path,
		const QJsonObject &body,
		Fn<void()> after) {
	Post(path, body, crl::guard(this, [=](QJsonObject result) {
		const auto message = Str(result, "message");
		toast(message.isEmpty() ? u"Готово"_q : message);
		if (after) {
			after();
		} else {
			reload();
		}
	}), crl::guard(this, [=](QString error) {
		toast(error);
	}));
}

void Panel::ask(const QString &text, const QString &button, Fn<void()> done) {
	_panel->showBox(
		Ui::MakeConfirmBox({
			.text = text,
			.confirmed = [=](Fn<void()> close) {
				close();
				done();
			},
			.confirmText = button,
			.confirmStyle = &st::attentionBoxButton,
		}),
		Ui::LayerOption::KeepOther,
		anim::type::normal);
}

void Panel::toast(const QString &text) {
	_panel->showToast(text);
}

std::optional<qint64> Panel::number(
		not_null<Ui::InputField*> field,
		qint64 min,
		qint64 max) {
	auto ok = false;
	auto text = field->getLastText().trimmed();
	text.remove(QChar(u' ')).remove(QChar(0x00A0));
	const auto value = text.toLongLong(&ok);
	if (!ok || value < min || value > max) {
		field->showError();
		toast(u"Нужно целое число от %1 до %2."_q
			.arg(FmtNumber(min))
			.arg(FmtNumber(max)));
		return std::nullopt;
	}
	return value;
}

void Panel::openPerson(const QString &id, const QString &name) {
	open(name.isEmpty() ? id : name, [=] {
		load(u"user/"_q + id, [=](
				not_null<Layout*> content,
				const QJsonObject &data) {
			renderPerson(content, id, data);
		});
	});
}

void Panel::openGroup(const QString &id, const QString &title) {
	open(title.isEmpty() ? id : title, [=] {
		load(u"chat/"_q + id, [=](
				not_null<Layout*> content,
				const QJsonObject &data) {
			renderGroup(content, id, data);
		});
	});
}

void Panel::openTable(const QString &name) {
	open(u"🗄 "_q + name, [=] {
		showTable(name);
	});
}

void Panel::renderHome(not_null<Layout*> content, const QJsonObject &data) {
	const auto now = Num(At(data, "now"));
	const auto started = Num(At(data, "started"));
	const auto groups = Arr(data, "groups");
	const auto errors = Num(At(data, "errors"));
	AddText(
		content,
		u"Бот работает "_q
			+ Span(qint64(now - started))
			+ u" · данные на "_q
			+ Date(At(data, "now")),
		true);
	AddTitle(content, u"Разделы"_q);
	AddRow(content, u"👥 Люди"_q, Fmt(At(data, "people")), [=] {
		open(u"👥 Люди"_q, [=] { showPeople(); });
	});
	AddRow(content, u"💬 Группы"_q, FmtNumber(groups.size()), [=] {
		open(u"💬 Группы"_q, [=] {
			load(u"overview"_q, [=](
					not_null<Layout*> content,
					const QJsonObject &data) {
				renderGroups(content, data);
			});
		});
	});
	AddRow(content, u"📊 Графики"_q, u"сообщения, люди, деньги"_q, [=] {
		open(u"📊 Графики"_q, [=] {
			load(u"charts?days="_q + QString::number(_days), [=](
					not_null<Layout*> content,
					const QJsonObject &data) {
				renderCharts(content, data);
			});
		});
	});
	AddRow(content, u"💰 Экономика"_q, u"цены, промокоды, ивенты"_q, [=] {
		open(u"💰 Экономика"_q, [=] {
			load(u"economy"_q, [=](
					not_null<Layout*> content,
					const QJsonObject &data) {
				renderEconomy(content, data);
			});
		});
	});
	AddRow(
		content,
		u"🖥 Сервер"_q,
		errors ? u"ошибок за сутки: %1"_q.arg(FmtNumber(errors)) : u"всё спокойно"_q,
		[=] {
			open(u"🖥 Сервер"_q, [=] {
				load(u"server"_q, [=](
						not_null<Layout*> content,
						const QJsonObject &data) {
					renderServer(content, data);
				});
			});
		});
	AddRow(content, u"🗄 База данных"_q, u"только просмотр"_q, [=] {
		open(u"🗄 База данных"_q, [=] {
			load(u"tables"_q, [=](
					not_null<Layout*> content,
					const QJsonObject &data) {
				renderTables(content, data);
			});
		});
	});
	AddButton(content, u"🔄 Обновить"_q, [=] { reload(); });

	AddSection(content, u"Сводка"_q);
	AddRow(
		content,
		u"Сообщений сегодня"_q,
		Fmt(At(data, "messages_today"))
			+ u" · писали "_q
			+ Fmt(At(data, "active_today"))
			+ u" чел."_q);
	AddRow(
		content,
		u"Долларов на руках"_q,
		ShortValue(At(data, "usd"))
			+ u" $ · у "_q
			+ Fmt(At(data, "with_money"))
			+ u" чел."_q);
	AddRow(content, u"Евро на руках"_q, ShortValue(At(data, "eur")) + u" €"_q);
	AddRow(
		content,
		u"Крипта"_q,
		u"≈ "_q + ShortValue(At(data, "crypto_usd")) + u" $"_q);
	AddRow(content, u"VIP"_q, Fmt(At(data, "vip")));
	AddRow(content, u"🎟 Промокодов"_q, Fmt(At(data, "promos")));
	AddRow(content, u"🧊 Заморожено"_q, Fmt(At(data, "frozen")));
	AddRow(content, u"🙈 В игноре"_q, Fmt(At(data, "ignored")));
	const auto event = Obj(data, "event");
	AddText(
		content,
		event.isEmpty()
			? u"🎉 Ивента нет"_q
			: (u"🎉 Ивент "_q
				+ StripTags(Str(event, "text"))
				+ u" до "_q
				+ Date(At(event, "until"))),
		true);

	AddSection(content, u"Группы"_q);
	renderGroups(content, data);
}

void Panel::renderGroups(not_null<Layout*> content, const QJsonObject &data) {
	const auto groups = Arr(data, "groups");
	if (groups.isEmpty()) {
		AddText(content, u"Бот пока ни в одной группе."_q, true);
		return;
	}
	const auto now = Now();
	for (const auto &value : groups) {
		const auto group = value.toObject();
		const auto id = Str(group, "chat_id");
		const auto title = Str(group, "title").isEmpty()
			? id
			: Str(group, "title");
		const auto premium = Num(At(group, "premium_until"));
		AddRow(
			content,
			title,
			Fmt(At(group, "members"))
				+ u" уч. · "_q
				+ ((premium > now)
					? (u"🎩 "_q + VipText(At(group, "premium_until")))
					: u"без Premium"_q),
			[=] { openGroup(id, title); });
	}
}

void Panel::renderCharts(not_null<Layout*> content, const QJsonObject &data) {
	AddChoice(content, u"Период"_q, {
		{ u"7"_q, u"7 дней"_q },
		{ u"30"_q, u"30 дней"_q },
		{ u"90"_q, u"90 дней"_q },
	}, QString::number(_days), [=](QString key) {
		_days = key.toInt();
		reload();
	});
	auto days = QStringList();
	for (const auto &value : Arr(data, "days")) {
		days.push_back(value.toString());
	}
	const auto plain = [](double value) {
		return FmtNumber(value);
	};
	const auto usd = [](double value) {
		return FmtNumber(value) + u" $"_q;
	};
	const auto chart = [&](
			const QString &title,
			const QString &note,
			std::vector<Series> series,
			bool line) {
		AddSection(content, title);
		AddText(content, note, true);
		content->add(
			object_ptr<Chart>(content, days, std::move(series), line),
			RowPadding());
	};
	chart(u"Сообщения по дням"_q, u"во всех группах"_q, { {
		.name = u"Сообщений"_q,
		.values = Values(Arr(data, "messages")),
		.color = &st::windowActiveTextFg,
		.format = plain,
	} }, false);
	chart(u"Сколько людей писали"_q, u"разные люди за день"_q, { {
		.name = u"Людей"_q,
		.values = Values(Arr(data, "people")),
		.color = &st::windowActiveTextFg,
		.format = plain,
	} }, false);
	chart(
		u"Доллары: пришло и ушло"_q,
		u"пришло — бонусы, игры, выдача; ушло — покупки"_q,
		{
			{
				.name = u"Пришло"_q,
				.values = Values(Arr(data, "money_in")),
				.color = &st::windowActiveTextFg,
				.format = usd,
			},
			{
				.name = u"Ушло"_q,
				.values = Values(Arr(data, "money_out")),
				.color = &st::attentionButtonFg,
				.format = usd,
			},
		},
		false);
	chart(u"Участников в группах"_q, u"замер раз в час"_q, { {
		.name = u"Участников"_q,
		.values = Values(Arr(data, "population")),
		.color = &st::windowActiveTextFg,
		.format = plain,
	} }, true);
}

void Panel::showPeople() {
	++_generation;
	_keepScroll = false;
	const auto content = shell();
	const auto list = std::make_shared<QPointer<Layout>>();
	const auto refresh = [=] {
		if (const auto raw = list->data()) {
			_people.page = 0;
			fillPeople(raw);
		}
	};
	const auto search = AddField(
		content,
		u"Имя, @username или id"_q,
		_people.q);
	const auto timer = search->lifetime().make_state<base::Timer>([=] {
		_people.q = search->getLastText().trimmed();
		refresh();
	});
	search->changes(
	) | rpl::on_next([=] {
		timer->callOnce(kSearchDelay);
	}, search->lifetime());
	search->submits(
	) | rpl::on_next([=] {
		timer->cancel();
		_people.q = search->getLastText().trimmed();
		refresh();
	}, search->lifetime());
	AddChoice(content, u"Сортировка"_q, {
		{ u"messages"_q, u"по сообщениям"_q },
		{ u"grains"_q, u"по долларам"_q },
		{ u"coins"_q, u"по евро"_q },
		{ u"stars"_q, u"по звёздности"_q },
		{ u"last_seen"_q, u"по последнему визиту"_q },
		{ u"vip_until"_q, u"по VIP"_q },
		{ u"name"_q, u"по имени"_q },
	}, _people.sort, [=](QString key) {
		_people.sort = key;
		refresh();
	});
	AddChoice(content, u"Порядок"_q, {
		{ u"desc"_q, u"по убыванию"_q },
		{ u"asc"_q, u"по возрастанию"_q },
	}, _people.dir, [=](QString key) {
		_people.dir = key;
		refresh();
	});
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	*list = content->add(object_ptr<Layout>(content));
	fillPeople(list->data());
}

void Panel::fillPeople(not_null<Layout*> list) {
	const auto generation = ++_listGeneration;
	const auto query = Query({
		{ u"q"_q, _people.q },
		{ u"sort"_q, _people.sort },
		{ u"dir"_q, _people.dir },
		{ u"page"_q, QString::number(_people.page) },
	});
	Get(u"users?"_q + query, crl::guard(list, [=](QJsonObject data) {
		if (generation != _listGeneration) {
			return;
		}
		list->clear();
		const auto rows = Arr(data, "rows");
		if (rows.isEmpty()) {
			AddText(list, u"Никого не нашёл."_q, true);
		}
		for (const auto &value : rows) {
			const auto row = value.toObject();
			const auto id = Str(row, "user_id");
			const auto name = Str(row, "name").isEmpty()
				? id
				: Str(row, "name");
			auto marks = QString();
			if (Num(At(row, "vip_until")) > 0) {
				marks += u"💎"_q;
			}
			if (At(row, "frozen").toBool()) {
				marks += u"🧊"_q;
			}
			if (At(row, "ignored").toBool()) {
				marks += u"🙈"_q;
			}
			const auto username = Str(row, "username");
			AddRow(
				list,
				(marks.isEmpty() ? QString() : (marks + u" "_q))
					+ name
					+ (username.isEmpty() ? QString() : (u" @"_q + username)),
				Usd(At(row, "grains"))
					+ u" · "_q
					+ Fmt(At(row, "messages"))
					+ u" сообщ."_q,
				[=] { openPerson(id, name); });
		}
		const auto total = qint64(Num(At(data, "total")));
		const auto size = std::max(qint64(Num(At(data, "size"))), qint64(1));
		const auto pages = std::max((total + size - 1) / size, qint64(1));
		Ui::AddSkip(list);
		AddText(
			list,
			u"Стр. %1 из %2 · всего %3"_q
				.arg(_people.page + 1)
				.arg(pages)
				.arg(FmtNumber(total)),
			true);
		if (_people.page > 0) {
			AddButton(list, u"← Предыдущая страница"_q, [=] {
				--_people.page;
				fillPeople(list);
			});
		}
		if (_people.page + 1 < pages) {
			AddButton(list, u"Следующая страница →"_q, [=] {
				++_people.page;
				fillPeople(list);
			});
		}
		list->resizeToWidth(list->width());
	}), crl::guard(list, [=](QString error) {
		toast(error);
	}));
}

void Panel::renderPerson(
		not_null<Layout*> content,
		const QString &id,
		const QJsonObject &data) {
	const auto user = Obj(data, "user");
	const auto name = Str(user, "name").isEmpty() ? id : Str(user, "name");
	const auto path = u"user/"_q + id;
	const auto now = Now();
	const auto chats = Arr(data, "chats");
	auto cryptoUsd = 0.;
	for (const auto &value : Arr(data, "crypto")) {
		cryptoUsd += Num(value.toObject().value(u"usd"_q));
	}
	auto messages = 0.;
	auto vipUntil = 0.;
	for (const auto &value : chats) {
		const auto chat = value.toObject();
		messages += Num(At(chat, "messages"));
		const auto until = Num(At(chat, "vip_until"));
		if (until > now) {
			vipUntil = std::max(vipUntil, until);
		}
	}
	const auto frozen = Obj(data, "frozen");
	const auto ignored = Obj(data, "ignored");

	auto line = QStringList();
	if (!Str(user, "username").isEmpty()) {
		line.push_back(u"@"_q + Str(user, "username"));
	}
	line.push_back(u"id "_q + id);
	line.push_back(u"был(а): "_q + Ago(At(user, "last_seen")));
	AddText(content, line.join(u" · "_q), true);
	auto marks = QStringList();
	if (vipUntil > 0) {
		marks.push_back(u"💎 VIP "_q + VipText(vipUntil));
	}
	if (!frozen.isEmpty()) {
		marks.push_back(u"🧊 заморожен"_q);
	}
	if (!ignored.isEmpty()) {
		marks.push_back(u"🙈 в игноре"_q);
	}
	if (!marks.isEmpty()) {
		AddText(content, marks.join(u" · "_q));
	}
	Ui::AddSkip(content);
	AddRow(content, u"Доллары"_q, Usd(At(user, "grains")));
	AddRow(content, u"Евро"_q, Eur(At(user, "coins")));
	AddRow(content, u"Звёздность"_q, Fmt(At(user, "stars")));
	AddRow(
		content,
		u"Крипта"_q,
		u"≈ "_q + FmtNumber(std::round(cryptoUsd)) + u" $"_q);
	AddRow(content, u"Сообщений"_q, FmtNumber(messages));

	AddSection(content, u"💵 Деньги"_q);
	const auto currency = AddChoice(content, u"Валюта"_q, {
		{ u"grains"_q, u"Доллары $"_q },
		{ u"coins"_q, u"Евро €"_q },
	}, u"grains"_q);
	const auto amount = AddField(content, u"Сумма"_q, u"1000"_q);
	const auto send = [=](const QString &action) {
		const auto value = number(amount, 1, kMaxAmount);
		if (!value) {
			return;
		}
		const auto body = QJsonObject{
			{ u"action"_q, action },
			{ u"currency"_q, currency->current() },
			{ u"amount"_q, *value },
		};
		if (action == u"give"_q) {
			post(path, body);
		} else {
			ask(
				u"Забрать %1 у %2?"_q.arg(
					Money(double(*value), currency->current()),
					name),
				u"Забрать"_q,
				[=] { post(path, body); });
		}
	};
	AddButton(content, u"Выдать"_q, [=] { send(u"give"_q); });
	AddButton(content, u"Забрать"_q, [=] { send(u"take"_q); }, true);

	AddSection(content, u"💎 VIP"_q);
	auto groups = std::vector<Option>();
	for (const auto &value : Arr(data, "groups")) {
		const auto group = value.toObject();
		const auto groupId = Str(group, "chat_id");
		const auto title = Str(group, "title");
		groups.push_back({ groupId, title.isEmpty() ? groupId : title });
	}
	if (groups.empty()) {
		AddText(content, u"Бот пока ни в одной группе."_q, true);
	} else {
		const auto initial = chats.isEmpty()
			? groups.front().key
			: Str(chats.first().toObject(), "chat_id");
		const auto group = AddChoice(
			content,
			u"Группа"_q,
			groups,
			initial);
		const auto months = AddField(content, u"Сколько месяцев"_q, u"1"_q);
		const auto forever = AddCheck(content, u"Навсегда"_q, false);
		AddButton(content, u"Выдать VIP"_q, [=] {
			auto body = QJsonObject{
				{ u"action"_q, u"vip"_q },
				{ u"chat_id"_q, group->current().toLongLong() },
				{ u"forever"_q, forever->checked() },
			};
			if (!forever->checked()) {
				const auto value = number(months, 1, 120);
				if (!value) {
					return;
				}
				body.insert(u"months"_q, *value);
			}
			post(path, body);
		});
	}
	if (vipUntil > 0) {
		AddButton(content, u"Снять VIP во всех группах"_q, [=] {
			ask(u"Снять VIP у %1?"_q.arg(name), u"Снять"_q, [=] {
				post(path, QJsonObject{ { u"action"_q, u"unvip"_q } });
			});
		}, true);
	}

	const auto mark = [&](
			const QString &title,
			const QString &hint,
			const QJsonObject &state,
			const QString &action,
			const QString &on,
			const QString &off,
			const QString &status) {
		AddSection(content, title);
		AddText(content, hint, true);
		if (!state.isEmpty()) {
			const auto reason = Str(state, "reason");
			AddText(
				content,
				status
					+ u" "_q
					+ Date(At(state, "at"), false)
					+ (reason.isEmpty() ? QString() : (u" — "_q + reason)));
			AddButton(content, off, [=] {
				post(path, QJsonObject{
					{ u"action"_q, action },
					{ u"on"_q, false },
				});
			});
		} else {
			const auto reason = AddField(
				content,
				u"Причина (необязательно)"_q);
			AddButton(content, on, [=] {
				post(path, QJsonObject{
					{ u"action"_q, action },
					{ u"on"_q, true },
					{ u"reason"_q, reason->getLastText().trimmed() },
				});
			}, true);
		}
	};
	mark(
		u"🧊 Заморозка мешка"_q,
		u"Нельзя переводить, брать бонус, играть на деньги, торговать "
		"и вводить промокоды."_q,
		frozen,
		u"freeze"_q,
		u"Заморозить"_q,
		u"Разморозить"_q,
		u"Заморожен"_q);
	mark(
		u"🙈 Игнор"_q,
		u"Бот не отвечает ни на команды, ни на РП, ни на кнопки."_q,
		ignored,
		u"ignore"_q,
		u"В игнор"_q,
		u"Убрать из игнора"_q,
		u"В игноре с"_q);

	AddSection(content, u"🧹 Обнулить"_q);
	AddText(content, u"История переводов останется."_q, true);
	const auto scopes = std::vector<Option>{
		{ u"all"_q, u"всё"_q },
		{ u"money"_q, u"только балансы"_q },
		{ u"perks"_q, u"только плюшки"_q },
	};
	const auto scope = AddChoice(content, u"Что обнулить"_q, scopes, u"all"_q);
	AddButton(content, u"Обнулить"_q, [=] {
		const auto key = scope->current();
		auto what = key;
		for (const auto &option : scopes) {
			if (option.key == key) {
				what = option.title;
			}
		}
		ask(
			u"Обнулить (%1) у %2? Это не отменить."_q.arg(what, name),
			u"Обнулить"_q,
			[=] {
				post(path, QJsonObject{
					{ u"action"_q, u"reset"_q },
					{ u"scope"_q, key },
				});
			});
	}, true);

	AddSection(content, u"В группах"_q);
	if (chats.isEmpty()) {
		AddText(content, u"Ни в одной группе."_q, true);
	}
	for (const auto &value : chats) {
		const auto chat = value.toObject();
		const auto chatId = Str(chat, "chat_id");
		const auto title = Str(chat, "title").isEmpty()
			? chatId
			: Str(chat, "title");
		const auto left = Num(At(chat, "left_chat")) > 0;
		AddRow(
			content,
			title + (left ? u" (вышел)"_q : QString()),
			u"ранг "_q
				+ FmtNumber(Num(At(chat, "rank")))
				+ u" · "_q
				+ Str(chat, "rank_name"),
			[=] { openGroup(chatId, title); });
		auto details = QStringList();
		details.push_back(Fmt(At(chat, "messages")) + u" сообщ."_q);
		if (!Str(chat, "nickname").isEmpty()) {
			details.push_back(u"ник: "_q + Str(chat, "nickname"));
		}
		details.push_back(u"репутация "_q + FmtNumber(Num(At(chat, "rep"))));
		if (Num(At(chat, "vip_until")) > now) {
			details.push_back(u"VIP "_q + VipText(At(chat, "vip_until")));
		}
		const auto joined = Num(At(chat, "joined_at"))
			? At(chat, "joined_at")
			: At(chat, "first_seen");
		details.push_back(u"пришёл "_q + Date(joined, false));
		AddText(content, details.join(u" · "_q), true);
	}

	AddSection(content, u"Операции с деньгами"_q);
	for (const auto &value : Arr(data, "totals")) {
		const auto row = value.toObject();
		const auto currencyKey = Str(row, "currency");
		const auto granted = Num(At(row, "granted"));
		AddText(
			content,
			CurrencyName(currencyKey)
				+ u": пришло "_q
				+ Money(At(row, "got"), currencyKey)
				+ u", ушло "_q
				+ Money(At(row, "gave"), currencyKey)
				+ (granted
					? (u", из них от тебя "_q
						+ Money(At(row, "granted"), currencyKey))
					: QString()),
			true);
	}
	const auto transfers = Arr(data, "transfers");
	if (transfers.isEmpty()) {
		AddText(content, u"Операций нет."_q, true);
	}
	for (const auto &value : transfers) {
		const auto row = value.toObject();
		const auto incoming = At(row, "incoming").toBool();
		const auto other = At(row, "other").isNull()
			? QString()
			: Str(row, "other");
		const auto otherName = Str(row, "other_name");
		const auto note = Str(row, "note");
		auto text = (incoming ? u"+"_q : u"−"_q)
			+ Money(At(row, "amount"), Str(row, "currency"));
		if (!other.isEmpty()) {
			text += u" · "_q + (otherName.isEmpty() ? other : otherName);
		}
		text += u" · "_q + (note.isEmpty() ? u"—"_q : note);
		AddRow(
			content,
			text,
			Date(At(row, "at")),
			other.isEmpty()
				? Fn<void()>()
				: Fn<void()>([=] { openPerson(other, otherName); }));
	}

	AddSection(content, u"Крипта"_q);
	const auto crypto = Arr(data, "crypto");
	if (crypto.isEmpty()) {
		AddText(content, u"Крипты нет."_q, true);
	}
	for (const auto &value : crypto) {
		const auto row = value.toObject();
		AddRow(
			content,
			Str(row, "coin") + u" · "_q + Fmt(At(row, "amount")),
			At(row, "usd").isNull()
				? u"—"_q
				: (u"≈ "_q + FmtNumber(std::round(Num(At(row, "usd")))) + u" $"_q));
	}

	AddSection(content, u"Наказания"_q);
	const auto punishments = Arr(data, "punishments");
	if (punishments.isEmpty()) {
		AddText(content, u"Наказаний нет."_q, true);
	}
	for (const auto &value : punishments) {
		const auto row = value.toObject();
		auto parts = QStringList{
			Date(At(row, "created")),
			Str(row, "kind"),
		};
		if (!Str(row, "title").isEmpty()) {
			parts.push_back(Str(row, "title"));
		}
		if (!Str(row, "reason").isEmpty()) {
			parts.push_back(Str(row, "reason"));
		}
		parts.push_back(Num(At(row, "active")) ? u"активно"_q : u"снято"_q);
		AddText(content, parts.join(u" · "_q));
	}

	AddSection(content, u"Анкета и служебные поля"_q);
	const auto keys = {
		"user_id", "username", "gender", "city", "birthday", "about",
		"motto", "first_seen", "last_seen", "bag_stars", "last_bonus",
		"last_farm", "transfers_closed", "bag_open",
	};
	auto lines = QStringList();
	for (const auto key : keys) {
		const auto name = QString::fromLatin1(key);
		if (!user.contains(name)) {
			continue;
		}
		const auto value = user.value(name);
		lines.push_back(name
			+ u": "_q
			+ (value.isNull()
				? u"—"_q
				: LooksLikeTime(name, value)
				? Date(value)
				: Raw(value)));
	}
	AddText(content, lines.join(u"\n"_q), true);
}

void Panel::renderGroup(
		not_null<Layout*> content,
		const QString &id,
		const QJsonObject &data) {
	const auto chat = Obj(data, "chat");
	const auto title = Str(chat, "title").isEmpty() ? id : Str(chat, "title");
	const auto path = u"chat/"_q + id;
	const auto premiumOn = Num(At(data, "premium_until")) > Now();
	const auto botAdmin = At(data, "bot_admin").toBool();

	auto about = QStringList();
	if (!Str(chat, "username").isEmpty()) {
		about.push_back(u"@"_q + Str(chat, "username"));
	}
	about.push_back(u"код "_q
		+ (Str(chat, "code").isEmpty() ? u"—"_q : Str(chat, "code")));
	about.push_back(u"id "_q + id);
	about.push_back(u"бот здесь с "_q + Date(At(chat, "added_at"), false));
	AddText(content, about.join(u" · "_q), true);
	const auto link = Str(data, "link");
	if (link.startsWith(u"https://t.me/"_q)) {
		AddButton(content, u"🔗 "_q + link, [=] {
			UrlClickHandler::Open(link);
		});
	} else {
		AddText(
			content,
			botAdmin
				? u"🔒 Ссылки нет: бот не может приглашать"_q
				: u"🔒 Ссылки нет: бот не админ"_q,
			true);
	}
	AddText(
		content,
		botAdmin ? u"✅ Бот — админ группы"_q : u"⚠️ Бот не админ"_q,
		true);
	Ui::AddSkip(content);
	const auto counts = Obj(data, "counts");
	AddRow(
		content,
		u"Участников"_q,
		Fmt(At(counts, "members")) + u" · ушли "_q + Fmt(At(counts, "gone")));
	if (!At(data, "live_members").isNull()) {
		AddRow(content, u"По данным Telegram"_q, Fmt(At(data, "live_members")));
	}
	AddRow(content, u"Сообщений всего"_q, Fmt(At(counts, "messages")));
	AddRow(content, u"Копилка чата"_q, Usd(At(data, "piggy")));
	AddRow(content, u"Вложено евро"_q, Eur(At(data, "invested")));
	AddRow(
		content,
		u"Premium"_q,
		premiumOn ? VipText(At(data, "premium_until")) : u"нет"_q);

	AddSection(content, u"🎩 Premium"_q);
	AddText(
		content,
		premiumOn
			? (u"Активен "_q + VipText(At(data, "premium_until")))
			: u"Не активен"_q,
		true);
	const auto months = AddField(content, u"Сколько месяцев"_q, u"1"_q);
	const auto forever = AddCheck(content, u"Навсегда"_q, false);
	const auto notify = AddCheck(content, u"Объявить в группе"_q, true);
	AddButton(
		content,
		premiumOn ? u"Продлить Premium"_q : u"Выдать Premium"_q,
		[=] {
			auto body = QJsonObject{
				{ u"action"_q, u"premium"_q },
				{ u"forever"_q, forever->checked() },
				{ u"notify"_q, notify->checked() },
			};
			if (!forever->checked()) {
				const auto value = number(months, 1, 120);
				if (!value) {
					return;
				}
				body.insert(u"months"_q, *value);
			}
			post(path, body);
		});
	if (premiumOn) {
		AddButton(content, u"Снять Premium"_q, [=] {
			ask(u"Снять Premium с группы?"_q, u"Снять"_q, [=] {
				post(path, QJsonObject{ { u"action"_q, u"unpremium"_q } });
			});
		}, true);
	}

	AddSection(content, u"💲 Цены"_q);
	AddText(
		content,
		u"0 — выключить покупку. Плюсопад и минусит: цена и сколько "
		"штук за раз."_q,
		true);
	const auto everywhere = AddCheck(
		content,
		u"Сохранять во всех группах сразу (своя цена у групп сбросится)"_q,
		_everywhere);
	everywhere->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		_everywhere = checked;
	}, everywhere->lifetime());
	for (const auto &value : Arr(data, "prices")) {
		const auto price = value.toObject();
		const auto key = Str(price, "key");
		const auto name = Str(price, "title");
		const auto current = Num(At(price, "value"));
		const auto withAmount = At(price, "with_amount").toBool();
		const auto amount = Num(At(price, "amount"));
		const auto min = qint64(Num(At(price, "min")));
		const auto max = At(price, "max").isNull()
			? kMaxAmount
			: qint64(Num(At(price, "max")));
		const auto source = Str(price, "source");
		const auto sourceText = (source == u"group"_q)
			? u"своя у группы"_q
			: (source == u"common"_q)
			? u"общая"_q
			: u"по умолчанию"_q;
		const auto now = current
			? (FmtNumber(current)
				+ u" $"_q
				+ ((withAmount && amount)
					? (u" · "_q + FmtNumber(amount) + u" шт."_q)
					: QString()))
			: u"выключено"_q;
		Ui::AddSkip(content);
		AddRow(content, name, now + u" · "_q + sourceText);
		const auto field = AddField(
			content,
			u"Новая цена $ (от %1%2, 0 — выключить)"_q.arg(
				FmtNumber(min),
				(max < kMaxAmount) ? (u" до "_q + FmtNumber(max)) : QString()),
			QString::number(qint64(current)));
		const auto count = withAmount
			? AddField(
				content,
				u"Сколько штук за раз"_q,
				amount ? QString::number(qint64(amount)) : QString()).get()
			: nullptr;
		AddButton(content, u"Сохранить: "_q + name, [=] {
			const auto value = number(field, 0, max);
			if (!value) {
				return;
			}
			auto body = QJsonObject{
				{ u"action"_q, u"price"_q },
				{ u"key"_q, key },
				{ u"value"_q, *value },
				{ u"everywhere"_q, everywhere->checked() },
			};
			if (count && !count->getLastText().trimmed().isEmpty()) {
				const auto pieces = number(count, 1, 1000);
				if (!pieces) {
					return;
				}
				body.insert(u"amount"_q, *pieces);
			}
			post(path, body);
		});
	}

	AddSection(content, u"🚪 Выйти из группы"_q);
	AddText(
		content,
		u"Бот напишет комментарий в группу и выйдет. Вернуть его сможет "
		"только админ группы."_q,
		true);
	const auto farewell = AddField(
		content,
		u"Комментарий перед выходом (необязательно)"_q,
		QString(),
		true);
	AddButton(content, u"Покинуть группу"_q, [=] {
		ask(u"Бот выйдет из «%1». Точно?"_q.arg(title), u"Покинуть"_q, [=] {
			post(path, QJsonObject{
				{ u"action"_q, u"leave"_q },
				{ u"comment"_q, farewell->getLastText().trimmed() },
			}, [=] { home(); });
		});
	}, true);

	AddSection(content, u"Модераторы"_q);
	const auto staff = Arr(data, "staff");
	if (staff.isEmpty()) {
		AddText(content, u"Модераторов нет."_q, true);
	}
	for (const auto &value : staff) {
		const auto row = value.toObject();
		const auto userId = Str(row, "user_id");
		const auto shown = !Str(row, "nickname").isEmpty()
			? Str(row, "nickname")
			: !Str(row, "name").isEmpty()
			? Str(row, "name")
			: userId;
		const auto username = Str(row, "username");
		AddRow(
			content,
			shown + (username.isEmpty() ? QString() : (u" @"_q + username)),
			u"ранг "_q + Fmt(At(row, "rank")) + u" · "_q + Str(row, "rank_name"),
			[=] { openPerson(userId, shown); });
	}

	AddSection(content, u"Все настройки группы"_q);
	AddText(content, u"Только просмотр: так они лежат в базе."_q, true);
	auto settings = QStringList();
	for (const auto &value : Arr(data, "settings")) {
		const auto row = value.toObject();
		settings.push_back(Str(row, "key") + u" = "_q + Str(row, "value"));
	}
	AddText(
		content,
		settings.isEmpty() ? u"Пусто."_q : settings.join(u"\n"_q),
		true);
}

void Panel::renderEconomy(not_null<Layout*> content, const QJsonObject &data) {
	AddText(
		content,
		u"Курс евро на бирже: 1 € = "_q
			+ Locale().toString(Num(At(data, "rate")), 'f', 4)
			+ u" $"_q,
		true);

	AddTitle(content, u"🏆 Самые богатые"_q);
	const auto top = Arr(data, "top");
	if (top.isEmpty()) {
		AddText(content, u"Денег пока ни у кого нет."_q, true);
	}
	auto place = 0;
	for (const auto &value : top) {
		const auto row = value.toObject();
		const auto id = Str(row, "user_id");
		const auto name = Str(row, "name").isEmpty() ? id : Str(row, "name");
		AddRow(
			content,
			QString::number(++place) + u". "_q + name,
			Usd(At(row, "grains")) + u" · "_q + Eur(At(row, "coins")),
			[=] { openPerson(id, name); });
	}

	AddSection(content, u"🪙 Крипта у людей"_q);
	const auto holdings = Arr(data, "crypto");
	if (holdings.isEmpty()) {
		AddText(content, u"Крипты ни у кого нет."_q, true);
	}
	for (const auto &value : holdings) {
		const auto row = value.toObject();
		AddRow(
			content,
			Str(row, "coin")
				+ u" · "_q
				+ Fmt(At(row, "amount"))
				+ u" · у "_q
				+ Fmt(At(row, "people"))
				+ u" чел."_q,
			At(row, "usd").isNull() ? u"—"_q : (u"≈ "_q + Usd(At(row, "usd"))));
	}

	AddSection(content, u"📈 Цены крипты"_q);
	AddText(
		content,
		u"Цена в боте = рыночная × коэффициент. Действует везде: покупка, "
		"продажа, портфели, топ, уведомления и «крипта график». Плавное "
		"изменение выглядит на графике как настоящее движение."_q,
		true);
	auto coins = std::vector<Option>();
	for (const auto &value : Arr(data, "coins")) {
		const auto row = value.toObject();
		coins.push_back({
			Str(row, "coin"),
			Str(row, "symbol")
				+ u" "_q
				+ Str(row, "coin")
				+ u" — "_q
				+ Str(row, "name"),
		});
	}
	if (!coins.empty()) {
		const auto coin = AddChoice(
			content,
			u"Монета"_q,
			coins,
			coins.front().key);
		const auto mode = AddChoice(content, u"Что сделать"_q, {
			{ u"pump"_q, u"▲ поднять на %"_q },
			{ u"dump"_q, u"▼ опустить на %"_q },
			{ u"set"_q, u"🎯 поставить цену в $"_q },
			{ u"reset"_q, u"↩ вернуть к рынку"_q },
		}, u"pump"_q);
		const auto amount = AddField(
			content,
			u"Процент или цена в $ (для «вернуть к рынку» не нужно)"_q);
		const auto time = AddChoice(content, u"Как быстро"_q, {
			{ u"0"_q, u"сразу"_q },
			{ u"15"_q, u"за 15 минут"_q },
			{ u"60"_q, u"за 1 час"_q },
			{ u"180"_q, u"за 3 часа"_q },
			{ u"720"_q, u"за 12 часов"_q },
			{ u"1440"_q, u"за сутки"_q },
			{ u"4320"_q, u"за 3 дня"_q },
		}, u"0"_q);
		AddButton(content, u"Применить"_q, [=] {
			const auto value = amount->getLastText().trimmed();
			if (mode->current() != u"reset"_q && value.isEmpty()) {
				amount->showError();
				toast(u"Впиши процент или цену."_q);
				return;
			}
			post(u"economy"_q, QJsonObject{
				{ u"action"_q, u"crypto"_q },
				{ u"coin"_q, coin->current() },
				{ u"mode"_q, mode->current() },
				{ u"value"_q, value },
				{ u"minutes"_q, time->current().toLongLong() },
			});
		});
	}
	Ui::AddSkip(content);
	for (const auto &value : Arr(data, "coins")) {
		const auto row = value.toObject();
		const auto ticker = Str(row, "coin");
		const auto factor = Num(At(row, "factor"));
		const auto rampUntil = Num(At(row, "ramp_until"));
		const auto changed = (std::abs(factor - 1.) > 1e-9) || (rampUntil > 0);
		const auto price = [&](const char *key) {
			return At(row, key).isNull()
				? u"—"_q
				: (FmtNumber(Num(At(row, key))) + u" $"_q);
		};
		AddRow(
			content,
			Str(row, "symbol") + u" "_q + ticker,
			price("market")
				+ u" → "_q
				+ price("game")
				+ (changed ? (u" · "_q + Factor(factor)) : QString()),
			changed
				? Fn<void()>([=] {
					ask(
						u"Вернуть %1 к рыночной цене сразу?"_q.arg(ticker),
						u"К рынку"_q,
						[=] {
							post(u"economy"_q, QJsonObject{
								{ u"action"_q, u"crypto"_q },
								{ u"coin"_q, ticker },
								{ u"mode"_q, u"reset"_q },
								{ u"minutes"_q, 0 },
							});
						});
				})
				: Fn<void()>());
		if (rampUntil > 0) {
			AddText(
				content,
				u"меняется до "_q
					+ Date(At(row, "ramp_until"))
					+ u" к "_q
					+ Factor(Num(At(row, "ramp_target"))),
				true);
		}
	}
	AddText(content, u"Нажми на изменённую монету — вернёшь её к рынку."_q, true);

	AddSection(content, u"Откуда пришли и куда ушли деньги"_q);
	AddChoice(content, u"За какой срок"_q, {
		{ u"day"_q, u"сутки"_q },
		{ u"week"_q, u"неделя"_q },
	}, _flows, [=](QString key) {
		_flows = key;
		reload();
	});
	const auto flows = Obj(Obj(data, "flows"), _flows.toLatin1().constData());
	const auto flowList = [&](const QString &title, const char *key) {
		AddTitle(content, title);
		const auto rows = Arr(flows, key);
		if (rows.isEmpty()) {
			AddText(content, u"Ничего."_q, true);
		}
		for (const auto &value : rows) {
			const auto row = value.toObject();
			const auto note = Str(row, "note");
			AddRow(
				content,
				(note.isEmpty() ? u"—"_q : note),
				Money(At(row, "total"), Str(row, "currency"))
					+ u" · "_q
					+ Fmt(At(row, "times"))
					+ u" раз"_q);
		}
	};
	flowList(u"📥 Пришло"_q, "in");
	flowList(u"📤 Ушло"_q, "out");

	AddSection(content, u"🎟 Промокоды"_q);
	const auto code = AddField(content, u"Код"_q);
	const auto promoAmount = AddField(content, u"Сколько каждому"_q, u"1000"_q);
	const auto promoCurrency = AddChoice(content, u"Валюта"_q, {
		{ u"grains"_q, u"Доллары $"_q },
		{ u"coins"_q, u"Евро €"_q },
	}, u"grains"_q);
	const auto uses = AddField(content, u"На сколько человек"_q, u"20"_q);
	AddButton(content, u"Создать промокод"_q, [=] {
		const auto text = code->getLastText().trimmed().toUpper();
		if (text.isEmpty()) {
			code->showError();
			return;
		}
		const auto amount = number(promoAmount, 1, kMaxAmount);
		const auto people = amount ? number(uses, 1, 1'000'000) : std::nullopt;
		if (!amount || !people) {
			return;
		}
		post(u"economy"_q, QJsonObject{
			{ u"action"_q, u"promo"_q },
			{ u"code"_q, text },
			{ u"amount"_q, *amount },
			{ u"currency"_q, promoCurrency->current() },
			{ u"uses"_q, *people },
		});
	});
	Ui::AddSkip(content);
	const auto promos = Arr(data, "promos");
	AddText(
		content,
		promos.isEmpty()
			? u"Промокодов нет."_q
			: u"Нажми на промокод, чтобы удалить его."_q,
		true);
	for (const auto &value : promos) {
		const auto row = value.toObject();
		const auto name = Str(row, "code");
		AddRow(
			content,
			name
				+ u" — "_q
				+ Money(At(row, "amount"), Str(row, "currency"))
				+ u" каждому"_q,
			Fmt(At(row, "used")) + u" / "_q + Fmt(At(row, "uses")),
			[=] {
				ask(u"Удалить промокод %1?"_q.arg(name), u"Удалить"_q, [=] {
					post(u"economy"_q, QJsonObject{
						{ u"action"_q, u"unpromo"_q },
						{ u"code"_q, name },
					});
				});
			});
	}

	AddSection(content, u"🎉 Ивент"_q);
	const auto event = Obj(data, "event");
	if (!event.isEmpty()) {
		AddText(
			content,
			u"Идёт: "_q
				+ StripTags(Str(event, "text"))
				+ u" до "_q
				+ Date(At(event, "until")));
		AddButton(content, u"Остановить ивент"_q, [=] {
			ask(
				u"Остановить ивент? В группе объявлю окончание."_q,
				u"Остановить"_q,
				[=] {
					post(u"economy"_q, QJsonObject{
						{ u"action"_q, u"unevent"_q },
					});
				});
		}, true);
	} else {
		AddText(
			content,
			u"Сейчас ивента нет. Бот сам объявит начало и конец в группе."_q,
			true);
	}
	const auto mult = AddChoice(content, u"Множитель"_q, {
		{ u"1.5"_q, u"×1,5"_q },
		{ u"2"_q, u"×2"_q },
		{ u"3"_q, u"×3"_q },
		{ u"5"_q, u"×5"_q },
		{ u"10"_q, u"×10"_q },
	}, u"2"_q);
	const auto bonus = AddCheck(content, u"🎁 бонус"_q, true);
	const auto farm = AddCheck(content, u"🚜 ферма"_q, false);
	const auto fishing = AddCheck(content, u"🎣 рыбалка"_q, false);
	const auto hours = AddField(content, u"Сколько часов"_q, u"24"_q);
	AddButton(content, u"Запустить ивент"_q, [=] {
		const auto value = number(hours, 1, 720);
		if (!value) {
			return;
		}
		auto kinds = QJsonArray();
		if (bonus->checked()) {
			kinds.push_back(u"бонус"_q);
		}
		if (farm->checked()) {
			kinds.push_back(u"ферма"_q);
		}
		if (fishing->checked()) {
			kinds.push_back(u"рыбалка"_q);
		}
		post(u"economy"_q, QJsonObject{
			{ u"action"_q, u"event"_q },
			{ u"mult"_q, mult->current() },
			{ u"hours"_q, *value },
			{ u"kinds"_q, kinds },
		});
	});

	AddSection(content, u"🧹 Сброс у всех"_q);
	const auto reset = Obj(data, "reset");
	const auto summary = [&](const QString &title, const char *key) {
		auto lines = QStringList();
		for (const auto &value : Arr(reset, key)) {
			lines.push_back(StripTags(value.toString()));
		}
		AddText(content, title + u"\n"_q + lines.join(u"\n"_q), true);
	};
	summary(u"Балансы сейчас:"_q, "money");
	summary(u"Плюшки сейчас:"_q, "perks");
	const auto scope = AddChoice(content, u"Что обнулить"_q, {
		{ u"all"_q, u"балансы и плюшки"_q },
		{ u"money"_q, u"только балансы"_q },
		{ u"perks"_q, u"только плюшки"_q },
	}, u"all"_q);
	const auto word = AddField(content, u"Впиши ОБНУЛИТЬ"_q);
	AddButton(content, u"Обнулить у всех"_q, [=] {
		ask(
			u"Точно обнулить у всех? Перед сбросом сохраню копию базы."_q,
			u"Обнулить"_q,
			[=] {
				post(u"economy"_q, QJsonObject{
					{ u"action"_q, u"reset"_q },
					{ u"scope"_q, scope->current() },
					{ u"confirm"_q, word->getLastText().trimmed() },
				});
			});
	}, true);
	AddText(
		content,
		u"Ранги, ники, анкеты, браки, кланы и статистика сообщений "
		"не трогаются."_q,
		true);
}

void Panel::renderServer(not_null<Layout*> content, const QJsonObject &data) {
	const auto now = Num(At(data, "now"));
	const auto started = Num(At(data, "started"));
	AddText(
		content,
		u"Oracle Cloud · Python "_q
			+ Str(data, "python")
			+ u" · aiogram "_q
			+ Str(data, "aiogram"),
		true);
	AddRow(
		content,
		u"Работает"_q,
		Span(qint64(now - started)) + u" · с "_q + Date(At(data, "started")));
	AddRow(content, u"База"_q, Size(Num(At(data, "db_size"))));
	AddRow(
		content,
		u"Свободно на диске"_q,
		Size(Num(At(data, "disk_free")))
			+ u" из "_q
			+ Size(Num(At(data, "disk_total"))));
	const auto memory = Num(At(data, "memory_mb"));
	AddRow(
		content,
		u"Память бота"_q,
		memory ? (FmtNumber(std::round(memory)) + u" МБ"_q) : u"—"_q);
	auto load = QStringList();
	for (const auto &value : Arr(data, "load")) {
		load.push_back(Locale().toString(value.toDouble(), 'f', 2));
	}
	AddRow(
		content,
		u"Нагрузка · 1, 5, 15 мин"_q,
		load.isEmpty() ? u"—"_q : load.join(u" · "_q));
	AddRow(content, u"Ошибок за сутки"_q, Fmt(At(data, "errors")));

	AddSection(content, u"💾 Бэкап базы"_q);
	const auto autobackup = At(data, "autobackup").toBool(true);
	AddText(
		content,
		autobackup
			? u"Автобэкап включён · каждый день в %1:00"_q.arg(
				Str(data, "backup_hour"))
			: u"Автобэкап выключен"_q);
	AddText(
		content,
		u"Последний бэкап в личку: "_q + Date(At(data, "backup_at")),
		true);
	AddButton(content, u"📨 Прислать бэкап в личку"_q, [=] {
		post(u"server"_q, QJsonObject{ { u"action"_q, u"backup"_q } });
	});
	AddButton(content, u"⬇️ Скачать базу на компьютер"_q, [=] {
		downloadDatabase();
	});
	AddButton(
		content,
		autobackup ? u"Выключить автобэкап"_q : u"Включить автобэкап"_q,
		[=] {
			post(u"server"_q, QJsonObject{
				{ u"action"_q, u"autobackup"_q },
				{ u"on"_q, !autobackup },
			});
		},
		autobackup);

	AddSection(content, u"⚠️ Последние ошибки"_q);
	auto errors = QStringList();
	for (const auto &value : Arr(data, "last_errors")) {
		errors.push_back(value.toString());
	}
	AddText(
		content,
		errors.isEmpty() ? u"Ошибок нет."_q : errors.join(u"\n\n"_q),
		true);

	AddSection(content, u"📜 Лог бота"_q);
	const auto label = std::make_shared<QPointer<Ui::FlatLabel>>();
	const auto refresh = [=] {
		if (const auto raw = label->data()) {
			fillLog(raw);
		}
	};
	AddChoice(content, u"Сколько строк"_q, {
		{ u"100"_q, u"100"_q },
		{ u"300"_q, u"300"_q },
		{ u"1000"_q, u"1000"_q },
	}, QString::number(_logLines), [=](QString key) {
		_logLines = key.toInt();
		refresh();
	});
	const auto onlyErrors = AddCheck(
		content,
		u"Только ошибки и предупреждения"_q,
		_logErrors);
	onlyErrors->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		_logErrors = checked;
		refresh();
	}, onlyErrors->lifetime());
	AddButton(content, u"🔄 Обновить лог"_q, refresh);
	*label = AddText(content, u"Загружаю…"_q, true);
	refresh();
}

void Panel::fillLog(not_null<Ui::FlatLabel*> label) {
	const auto query = Query({
		{ u"lines"_q, QString::number(_logLines) },
		{ u"errors"_q, _logErrors ? u"1"_q : u"0"_q },
	});
	Get(u"logs?"_q + query, crl::guard(label, [=](QJsonObject data) {
		auto lines = QStringList();
		for (const auto &value : Arr(data, "lines")) {
			lines.push_back(value.toString());
		}
		label->setText(lines.isEmpty() ? u"Пусто."_q : lines.join(u"\n"_q));
	}), crl::guard(label, [=](QString error) {
		label->setText(error);
	}));
}

void Panel::downloadDatabase() {
	toast(u"Готовлю копию базы…"_q);
	Download(u"backup"_q, crl::guard(this, [=](
			QByteArray data,
			QString name) {
		const auto folder = QStandardPaths::writableLocation(
			QStandardPaths::DownloadLocation);
		FileDialog::GetWritePath(
			_panel.get(),
			u"Сохранить базу"_q,
			u"Архив базы (*.gz);;"_q + FileDialog::AllFilesFilter(),
			QDir(folder).filePath(name),
			crl::guard(this, [=](QString &&path) {
				auto file = QFile(path);
				if (file.open(QIODevice::WriteOnly)
					&& file.write(data) == data.size()) {
					toast(u"База сохранена: "_q
						+ QDir::toNativeSeparators(path));
				} else {
					toast(u"Не получилось сохранить файл."_q);
				}
			}));
	}), crl::guard(this, [=](QString error) {
		toast(error);
	}));
}

void Panel::renderTables(not_null<Layout*> content, const QJsonObject &data) {
	AddText(
		content,
		u"Только просмотр. Менять что-то — через разделы с кнопками."_q,
		true);
	for (const auto &value : Arr(data, "tables")) {
		const auto row = value.toObject();
		const auto name = Str(row, "name");
		AddRow(
			content,
			name,
			FmtNumber(Num(At(row, "rows"))) + u" строк"_q,
			[=] { openTable(name); });
	}
}

void Panel::showTable(const QString &name) {
	++_generation;
	_keepScroll = false;
	const auto content = shell();
	const auto list = std::make_shared<QPointer<Layout>>();
	const auto search = AddField(
		content,
		u"Поиск по всем колонкам"_q,
		_tables[name].q);
	const auto apply = [=] {
		auto &view = _tables[name];
		view.q = search->getLastText().trimmed();
		view.page = 0;
		if (const auto raw = list->data()) {
			fillTable(raw, name);
		}
	};
	const auto timer = search->lifetime().make_state<base::Timer>(apply);
	search->changes(
	) | rpl::on_next([=] {
		timer->callOnce(kSearchDelay);
	}, search->lifetime());
	search->submits(
	) | rpl::on_next([=] {
		timer->cancel();
		apply();
	}, search->lifetime());
	*list = content->add(object_ptr<Layout>(content));
	fillTable(list->data(), name);
}

void Panel::fillTable(not_null<Layout*> list, const QString &name) {
	const auto generation = ++_listGeneration;
	const auto &view = _tables[name];
	const auto query = Query({
		{ u"q"_q, view.q },
		{ u"sort"_q, view.sort },
		{ u"dir"_q, view.dir },
		{ u"page"_q, QString::number(view.page) },
	});
	const auto path = u"table/"_q
		+ QString::fromLatin1(QUrl::toPercentEncoding(name))
		+ u"?"_q
		+ query;
	Get(path, crl::guard(list, [=](QJsonObject data) {
		if (generation != _listGeneration) {
			return;
		}
		list->clear();
		auto &view = _tables[name];
		auto columns = QStringList();
		auto sorts = std::vector<Option>{
			{ QString(), u"как в базе (новые сверху)"_q },
		};
		for (const auto &value : Arr(data, "columns")) {
			columns.push_back(value.toString());
			sorts.push_back({ value.toString(), value.toString() });
		}
		AddChoice(list, u"Сортировка"_q, sorts, view.sort, [=](QString key) {
			auto &view = _tables[name];
			view.sort = key;
			view.page = 0;
			fillTable(list, name);
		});
		AddChoice(list, u"Порядок"_q, {
			{ u"desc"_q, u"по убыванию"_q },
			{ u"asc"_q, u"по возрастанию"_q },
		}, view.dir, [=](QString key) {
			auto &view = _tables[name];
			view.dir = key;
			view.page = 0;
			fillTable(list, name);
		});
		const auto rows = Arr(data, "rows");
		if (rows.isEmpty()) {
			AddText(list, u"Пусто."_q, true);
		}
		for (const auto &value : rows) {
			const auto row = value.toObject();
			auto lines = QStringList();
			for (const auto &column : columns) {
				const auto cell = row.value(column);
				auto text = cell.isNull() ? u"null"_q : Raw(cell);
				if (LooksLikeTime(column, cell)) {
					text += u" ("_q + Date(cell) + u")"_q;
				}
				lines.push_back(column + u": "_q + text);
			}
			Ui::AddDivider(list);
			AddText(list, lines.join(u"\n"_q));
		}
		const auto total = qint64(Num(At(data, "total")));
		const auto size = std::max(qint64(Num(At(data, "size"))), qint64(1));
		const auto pages = std::max((total + size - 1) / size, qint64(1));
		Ui::AddDivider(list);
		AddText(
			list,
			u"Стр. %1 из %2 · строк %3"_q
				.arg(view.page + 1)
				.arg(pages)
				.arg(FmtNumber(total)),
			true);
		if (view.page > 0) {
			AddButton(list, u"← Предыдущая страница"_q, [=] {
				--_tables[name].page;
				fillTable(list, name);
			});
		}
		if (view.page + 1 < pages) {
			AddButton(list, u"Следующая страница →"_q, [=] {
				++_tables[name].page;
				fillTable(list, name);
			});
		}
		list->resizeToWidth(list->width());
	}), crl::guard(list, [=](QString error) {
		toast(error);
	}));
}

} // namespace

void ShowWindow() {
	if (const auto panel = Instance()) {
		panel->activate();
	} else {
		Instance() = new Panel();
	}
}

} // namespace AyuFeatures::AdminPanel
