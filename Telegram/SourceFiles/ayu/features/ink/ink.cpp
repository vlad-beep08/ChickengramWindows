#include "ayu/features/ink/ink.h"

#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QCryptographicHash>

namespace AyuFeatures::Ink {
namespace {

constexpr auto kZeroBit = char16_t(0x200C);
constexpr auto kOneBit = char16_t(0x200D);
constexpr auto kMagic = uchar(0xC4);
constexpr auto kChecksumSalt = uchar(0x3C);
constexpr auto kCompressedFlag = uchar(0x01);
constexpr auto kHeaderBytes = 4;
constexpr auto kTrailerBytes = 1;
constexpr auto kBitsPerByte = 8;
constexpr auto kMaxMarkChars = 3000;
constexpr auto kMaxBodyBytes = kMaxMarkChars / kBitsPerByte
	- kHeaderBytes
	- kTrailerBytes;
constexpr auto kMessageLimit = 4096;
constexpr auto kArmTimeout = crl::time(15000);

struct Armed {
	QString secret;
	crl::time when = 0;
};

[[nodiscard]] Armed &Current() {
	static auto result = Armed();
	return result;
}

[[nodiscard]] bool IsMarkChar(QChar ch) {
	return (ch.unicode() == kZeroBit) || (ch.unicode() == kOneBit);
}

[[nodiscard]] QByteArray Keystream(int size) {
	auto result = QByteArray();
	result.reserve(size + 32);
	auto counter = 0;
	while (result.size() < size) {
		auto seed = QByteArray("chickengram-invisible-ink:");
		seed.append(QByteArray::number(counter++));
		result.append(QCryptographicHash::hash(
			seed,
			QCryptographicHash::Sha256));
	}
	result.resize(size);
	return result;
}

[[nodiscard]] QByteArray Mix(QByteArray data) {
	const auto stream = Keystream(data.size());
	for (auto i = 0; i != data.size(); ++i) {
		data[i] = char(uchar(data[i]) ^ uchar(stream[i]));
	}
	return data;
}

[[nodiscard]] uchar Checksum(const QByteArray &plain) {
	auto result = kChecksumSalt;
	for (const auto byte : plain) {
		result ^= uchar(byte);
	}
	return result;
}

[[nodiscard]] QByteArray Body(const QString &secret, uchar &flags) {
	const auto raw = secret.toUtf8();
	const auto packed = qCompress(raw, 9);
	if (packed.size() < raw.size()) {
		flags |= kCompressedFlag;
		return packed;
	}
	return raw;
}

[[nodiscard]] QString EncodeBytes(const QByteArray &bytes) {
	auto result = QString();
	result.reserve(bytes.size() * kBitsPerByte);
	for (const auto byte : bytes) {
		for (auto bit = kBitsPerByte - 1; bit >= 0; --bit) {
			result.append(QChar(((uchar(byte) >> bit) & 1)
				? kOneBit
				: kZeroBit));
		}
	}
	return result;
}

[[nodiscard]] std::optional<QString> Encode(const QString &secret) {
	auto flags = uchar(0);
	const auto plain = Body(secret, flags);
	if (plain.isEmpty() || plain.size() > kMaxBodyBytes) {
		return std::nullopt;
	}
	auto bytes = QByteArray();
	bytes.append(char(kMagic));
	bytes.append(char(flags));
	bytes.append(char((plain.size() >> 8) & 0xFF));
	bytes.append(char(plain.size() & 0xFF));
	bytes.append(Mix(plain));
	bytes.append(char(Checksum(plain)));
	return EncodeBytes(bytes);
}

[[nodiscard]] uchar ReadByte(QStringView bits, int index) {
	auto result = uchar(0);
	for (auto i = 0; i != kBitsPerByte; ++i) {
		const auto one = (bits[index * kBitsPerByte + i].unicode() == kOneBit);
		result = uchar((result << 1) | (one ? 1 : 0));
	}
	return result;
}

[[nodiscard]] std::optional<QString> Decode(QStringView bits) {
	const auto available = int(bits.size()) / kBitsPerByte;
	if (available < kHeaderBytes + kTrailerBytes
		|| ReadByte(bits, 0) != kMagic) {
		return std::nullopt;
	}
	const auto flags = ReadByte(bits, 1);
	const auto length = (int(ReadByte(bits, 2)) << 8) | int(ReadByte(bits, 3));
	if (!length
		|| length > kMaxBodyBytes
		|| available < kHeaderBytes + length + kTrailerBytes) {
		return std::nullopt;
	}
	auto mixed = QByteArray();
	mixed.reserve(length);
	for (auto i = 0; i != length; ++i) {
		mixed.append(char(ReadByte(bits, kHeaderBytes + i)));
	}
	const auto plain = Mix(mixed);
	if (Checksum(plain) != ReadByte(bits, kHeaderBytes + length)) {
		return std::nullopt;
	}
	const auto raw = (flags & kCompressedFlag) ? qUncompress(plain) : plain;
	const auto result = QString::fromUtf8(raw).trimmed();
	return result.isEmpty() ? std::nullopt : std::make_optional(result);
}

[[nodiscard]] int InsertPosition(const QString &text) {
	const auto space = text.indexOf(QChar(' '));
	return (space >= 0) ? int(space + 1) : int(text.size());
}

} // namespace

void Arm(const QString &secret) {
	Current() = Armed{ secret.trimmed(), crl::now() };
}

QString Take() {
	const auto armed = base::take(Current());
	return (armed.when && crl::now() - armed.when <= kArmTimeout)
		? armed.secret
		: QString();
}

bool Apply(TextWithTags &text, const QString &secret) {
	if (text.text.trimmed().isEmpty() || secret.trimmed().isEmpty()) {
		return false;
	}
	const auto mark = Encode(secret.trimmed());
	if (!mark || (text.text.size() + mark->size() > kMessageLimit)) {
		return false;
	}
	const auto position = InsertPosition(text.text);
	const auto length = int(mark->size());
	text.text.insert(position, *mark);
	for (auto &tag : text.tags) {
		if (tag.offset >= position) {
			tag.offset += length;
		} else if (tag.offset + tag.length > position) {
			tag.length += length;
		}
	}
	return true;
}

std::optional<QString> Find(const QString &text) {
	const auto size = int(text.size());
	for (auto i = 0; i < size;) {
		if (!IsMarkChar(text[i])) {
			++i;
			continue;
		}
		auto end = i;
		while (end < size && IsMarkChar(text[end])) {
			++end;
		}
		for (auto start = i; start + kBitsPerByte <= end; ++start) {
			const auto bits = QStringView(text).mid(start, end - start);
			if (const auto result = Decode(bits)) {
				return result;
			}
		}
		i = end;
	}
	return std::nullopt;
}

TextWithEntities Reveal(TextWithEntities text) {
	const auto secret = Find(text.text);
	if (!secret) {
		return text;
	}
	text.text.append(u"\n\n"_q);
	text.text.append(QString::fromUtf8("\xF0\x9F\xAB\xA5 "));
	const auto offset = int(text.text.size());
	text.text.append(*secret);
	text.entities.push_back(EntityInText(
		EntityType::Spoiler,
		offset,
		int(secret->size())));
	return text;
}

void AskSecret(std::shared_ptr<Ui::Show> show, Fn<void(QString)> done) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(
			QString::fromUtf8("\xF0\x9F\xAB\xA5 ") + u"Невидимые чернила"_q));
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(u"Видимая часть — то, что уже написано в поле ввода. Скрытый текст увидят только друзья с Чикенграмом: у них он появится под сообщением, под спойлером. В обычном Telegram его не видно."_q),
			st::boxLabel));
		box->addSkip(st::boxOptionListSkip / 2);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			rpl::single(u"Скрытый текст"_q),
			QString()));
		const auto counter = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			st::boxDividerLabel));
		const auto refreshCounter = [=] {
			auto flags = uchar(0);
			const auto text = field->getLastText();
			const auto used = text.isEmpty()
				? 0
				: int(Body(text, flags).size());
			counter->setText((used <= kMaxBodyBytes)
				? u"Осталось места: %1 из %2"_q
					.arg(kMaxBodyBytes - used)
					.arg(kMaxBodyBytes)
				: u"Слишком длинно — сократи текст"_q);
		};
		refreshCounter();
		field->changes() | rpl::on_next(refreshCounter, field->lifetime());
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto submit = [=] {
			const auto secret = field->getLastText().trimmed();
			if (secret.isEmpty()) {
				field->showError();
				return;
			} else if (!Encode(secret)) {
				box->showToast(u"Скрытый текст слишком длинный."_q);
				return;
			}
			box->closeBox();
			done(secret);
		};
		field->submits(
		) | rpl::on_next([=](Qt::KeyboardModifiers) {
			submit();
		}, field->lifetime());
		box->addButton(rpl::single(u"Отправить"_q), submit);
		box->addButton(rpl::single(u"Отмена"_q), [=] {
			box->closeBox();
		});
	}));
}

} // namespace AyuFeatures::Ink
