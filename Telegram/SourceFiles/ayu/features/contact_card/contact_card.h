// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class History;
class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace AyuFeatures::ContactCard {

void RememberSent(not_null<History*> history);
void Show(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void StartBirthdayReminders();

} // namespace AyuFeatures::ContactCard
