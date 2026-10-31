#pragma once

namespace Ui {
class GenericBox;
class Show;
} // namespace Ui

namespace AyuFeatures::Ai {

enum class Provider {
	Groq,
	OpenAI,
	Claude,
	OpenRouter,
	Custom,
};

struct Config {
	Provider provider = Provider::Groq;
	QString model;
	QString url;
	QString key;
};

struct Request {
	QString system;
	QString user;
	int maxTokens = 4096;
};

[[nodiscard]] Config Current();
void Save(const Config &config);
[[nodiscard]] bool Ready();

[[nodiscard]] std::vector<Provider> Providers();
[[nodiscard]] QString ProviderName(Provider provider);
[[nodiscard]] QString DefaultModel(Provider provider);
[[nodiscard]] QString ModelOf(const Config &config);

void Ask(
	const Config &config,
	Request request,
	Fn<void(QString)> done,
	Fn<void(QString)> fail);
void Ask(Request request, Fn<void(QString)> done, Fn<void(QString)> fail);

void SettingsBox(not_null<Ui::GenericBox*> box);
void ShowSettings(std::shared_ptr<Ui::Show> show);

} // namespace AyuFeatures::Ai
