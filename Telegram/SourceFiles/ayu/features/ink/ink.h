#pragma once

struct TextWithTags;
struct TextWithEntities;

namespace Ui {
class Show;
} // namespace Ui

namespace AyuFeatures::Ink {

void Arm(const QString &secret);
[[nodiscard]] QString Take();

[[nodiscard]] bool Apply(TextWithTags &text, const QString &secret);
[[nodiscard]] std::optional<QString> Find(const QString &text);
[[nodiscard]] TextWithEntities Reveal(TextWithEntities text);

void AskSecret(std::shared_ptr<Ui::Show> show, Fn<void(QString)> done);

} // namespace AyuFeatures::Ink
