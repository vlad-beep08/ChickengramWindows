import colorsys
import io
import math
import random
import re
import sys
import tempfile
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
THEMES = HERE.parent
REPO = THEMES.parents[2]
PALETTE = REPO / "lib_ui" / "ui" / "colors.palette"
BASE_THEME = THEMES / "web-dark-orange.tdesktop-theme"
OUTPUT = THEMES / "collage.tdesktop-theme"
PREVIEW = Path(tempfile.gettempdir()) / "collage-wallpaper-preview.jpg"

TOKENS = {
    "bg": "#080808",
    "surface": "#141414",
    "surface2": "#1c1c1c",
    "border": "#2a2a2a",
    "paper": "#e8e1d1",
    "paper_ink": "#080808",
    "paper_muted": "#5a554d",
    "accent": "#c1121f",
    "accent_dark": "#780000",
    "accent_light": "#f05a5e",
    "accent_muted": "#f5dcd6",
    "text": "#e9e4d8",
    "text_muted": "#8a8580",
    "halftone": "#3a3a3a",
    "ok": "#6dbb63",
    "ok_on_paper": "#2f6b2a",
}

TILE = 1200
KEEP_PREFIXES = ("historyPeer", "msgFile2", "msgFile3", "msgFile4",
                 "mediaviewFile", "mediaviewTransparent", "statisticsChart")
WALLPAPER_SEED = 1937


def rgb(value):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def hex_of(color, alpha=None):
    result = "#" + "".join(f"{max(0, min(255, round(c))):02x}" for c in color)
    return result if alpha is None else result + f"{alpha:02x}"


def mix(a, b, amount):
    a, b = rgb(a), rgb(b)
    return hex_of(tuple(x + (y - x) * amount for x, y in zip(a, b)))


def alpha(color, value):
    return hex_of(rgb(color), value)


def luminance(color):
    def channel(c):
        c /= 255
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = rgb(color)
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


T = TOKENS
OVER = mix(T["surface"], T["text"], 0.05)
OVER2 = mix(T["surface2"], T["text"], 0.05)
RED_OVER = mix(T["accent"], T["text"], 0.08)
RED_TINT = mix(T["surface"], T["accent"], 0.14)
RED_TINT2 = mix(T["surface"], T["accent"], 0.22)
PAPER_SELECTED = mix(T["paper"], T["paper_ink"], 0.08)
RED_SELECTED = mix(T["accent"], T["accent_dark"], 0.45)
MUTED_BADGE = mix(T["surface2"], T["text_muted"], 0.35)

PEER_NAMES = ["#a4161a", "#2f6b2a", "#7a5b00", "#9a4a00",
              "#5b3aa8", "#a0174f", "#00665c", "#8a3f22"]

MAPPING = {
    "windowBg": T["surface"],
    "windowFg": T["text"],
    "windowBgOver": OVER,
    "windowBgRipple": T["border"],
    "windowFgOver": T["text"],
    "windowSubTextFg": T["text_muted"],
    "windowSubTextFgOver": T["text_muted"],
    "windowBoldFg": T["text"],
    "windowBoldFgOver": T["text"],
    "windowBgActive": T["accent"],
    "windowFgActive": T["text"],
    "windowActiveTextFg": T["accent_light"],
    "windowShadowFg": "#000000",
    "windowShadowFgFallback": T["bg"],
    "shadowFg": "#00000066",
    "slideFadeOutBg": "#00000050",
    "activeButtonBg": T["accent"],
    "activeButtonBgOver": RED_OVER,
    "activeButtonBgRipple": T["accent_dark"],
    "activeButtonFg": T["text"],
    "activeButtonFgOver": T["text"],
    "activeButtonSecondaryFg": T["accent_muted"],
    "activeLineFg": T["accent_light"],
    "activeLineFgError": T["accent_light"],
    "lightButtonBg": T["surface"],
    "lightButtonBgOver": RED_TINT,
    "lightButtonBgRipple": RED_TINT2,
    "lightButtonFg": T["accent_light"],
    "lightButtonFgOver": T["accent_light"],
    "attentionButtonFg": T["accent_light"],
    "attentionButtonFgOver": T["accent_light"],
    "attentionButtonBgOver": RED_TINT,
    "attentionButtonBgRipple": RED_TINT2,
    "menuBg": T["surface2"],
    "menuBgOver": OVER2,
    "menuBgRipple": T["border"],
    "menuIconFg": T["text_muted"],
    "menuIconFgOver": T["text"],
    "menuSubmenuArrowFg": T["text_muted"],
    "menuFgDisabled": mix(T["surface2"], T["text_muted"], 0.6),
    "menuSeparatorFg": T["border"],
    "scrollBarBg": alpha(T["text"], 0x4d),
    "scrollBarBgOver": alpha(T["text"], 0x7a),
    "scrollBg": alpha(T["text"], 0x14),
    "scrollBgOver": alpha(T["text"], 0x26),
    "smallCloseIconFg": T["text_muted"],
    "smallCloseIconFgOver": T["text"],
    "radialBg": "#00000056",
    "placeholderFg": T["text_muted"],
    "placeholderFgActive": mix(T["surface2"], T["text_muted"], 0.75),
    "inputBorderFg": T["halftone"],
    "filterInputBorderFg": T["accent_light"],
    "filterInputInactiveBg": T["surface2"],
    "filterInputActiveBg": T["surface2"],
    "checkboxFg": T["text_muted"],
    "sliderBgInactive": T["border"],
    "sliderBgActive": T["accent"],
    "tooltipBg": T["paper"],
    "tooltipFg": T["paper_ink"],
    "tooltipBorderFg": T["paper_ink"],
    "titleShadow": "#00000000",
    "titleBg": T["bg"],
    "titleBgActive": T["bg"],
    "titleButtonFg": T["text_muted"],
    "titleButtonBgOver": T["surface2"],
    "titleButtonFgOver": T["text"],
    "titleButtonBgActive": T["bg"],
    "titleButtonBgActiveOver": T["surface2"],
    "titleButtonCloseBgOver": T["accent"],
    "titleButtonCloseBgActive": T["bg"],
    "titleButtonCloseBgActiveOver": T["accent"],
    "titleFg": T["text_muted"],
    "titleFgActive": T["text"],
    "trayCounterBg": T["accent"],
    "trayCounterBgMute": MUTED_BADGE,
    "trayCounterFg": T["text"],
    "layerBg": "#000000a6",
    "cancelIconFg": T["text_muted"],
    "cancelIconFgOver": T["text"],
    "boxBg": T["surface2"],
    "boxTextFg": T["text"],
    "boxTextFgGood": T["ok"],
    "boxTextFgError": T["accent_light"],
    "boxTitleFg": T["text"],
    "boxSearchBg": T["surface2"],
    "boxTitleAdditionalFg": T["text_muted"],
    "boxTitleCloseFg": T["text_muted"],
    "boxTitleCloseFgOver": T["text"],
    "membersAboutLimitFg": T["text_muted"],
    "contactsBg": T["surface2"],
    "contactsBgOver": OVER2,
    "contactsNameFg": T["text"],
    "contactsStatusFg": T["text_muted"],
    "contactsStatusFgOver": T["text_muted"],
    "contactsStatusFgOnline": T["accent_light"],
    "photoCropPointFg": alpha(T["text"], 0x7f),
    "callArrowFg": T["ok"],
    "callArrowMissedFg": T["accent_light"],
    "introTitleFg": T["text"],
    "introDescriptionFg": T["text_muted"],
    "introCoverTopBg": T["accent_dark"],
    "introCoverBottomBg": T["accent"],
    "introCoverIconsFg": mix(T["accent"], T["text"], 0.2),
    "introCoverPlaneTrace": mix(T["accent"], T["text"], 0.12),
    "introCoverPlaneInner": T["paper"],
    "introCoverPlaneOuter": mix(T["paper"], T["paper_muted"], 0.35),
    "introCoverPlaneTop": T["paper"],
    "dialogsMenuIconFg": T["text_muted"],
    "dialogsMenuIconFgOver": T["text"],
    "dialogsBg": T["surface"],
    "dialogsNameFg": T["text"],
    "dialogsChatIconFg": T["text"],
    "dialogsDateFg": T["text_muted"],
    "dialogsTextFg": T["text_muted"],
    "dialogsTextFgService": T["text"],
    "dialogsDraftFg": T["accent_light"],
    "dialogsVerifiedIconBg": T["accent"],
    "dialogsVerifiedIconFg": T["text"],
    "dialogsSendingIconFg": T["text_muted"],
    "dialogsSentIconFg": T["accent_light"],
    "dialogsUnreadBg": T["accent"],
    "dialogsUnreadBgMuted": MUTED_BADGE,
    "dialogsUnreadFg": T["text"],
    "dialogsBgOver": OVER,
    "dialogsOnlineBadgeFg": T["ok"],
    "dialogsNameFgOver": T["text"],
    "dialogsChatIconFgOver": T["text"],
    "dialogsDateFgOver": T["text_muted"],
    "dialogsTextFgOver": T["text_muted"],
    "dialogsTextFgServiceOver": T["text"],
    "dialogsDraftFgOver": T["accent_light"],
    "dialogsVerifiedIconBgOver": T["accent"],
    "dialogsVerifiedIconFgOver": T["text"],
    "dialogsSendingIconFgOver": T["text_muted"],
    "dialogsSentIconFgOver": T["accent_light"],
    "dialogsUnreadBgOver": T["accent"],
    "dialogsUnreadBgMutedOver": MUTED_BADGE,
    "dialogsUnreadFgOver": T["text"],
    "dialogsBgActive": T["accent"],
    "dialogsNameFgActive": T["text"],
    "dialogsChatIconFgActive": T["text"],
    "dialogsDateFgActive": T["accent_muted"],
    "dialogsTextFgActive": T["accent_muted"],
    "dialogsTextFgServiceActive": T["text"],
    "dialogsDraftFgActive": T["text"],
    "dialogsVerifiedIconBgActive": T["text"],
    "dialogsVerifiedIconFgActive": T["accent"],
    "dialogsSendingIconFgActive": T["accent_muted"],
    "dialogsSentIconFgActive": T["text"],
    "dialogsUnreadBgActive": T["text"],
    "dialogsUnreadBgMutedActive": T["accent_muted"],
    "dialogsUnreadFgActive": T["accent"],
    "dialogsOnlineBadgeFgActive": T["text"],
    "dialogsRippleBg": T["border"],
    "dialogsRippleBgActive": T["accent_dark"],
    "dialogsArchiveFg": T["text_muted"],
    "dialogsArchiveFgOver": T["text_muted"],
    "dialogsScamFg": T["accent_light"],
    "dialogsScamFgOver": T["accent_light"],
    "dialogsScamFgActive": T["text"],
    "dialogsMentionIconFg": T["accent_light"],
    "dialogsReactionIconFg": T["accent_light"],
    "searchedBarBg": T["surface2"],
    "searchedBarFg": T["text_muted"],
    "topBarBg": T["surface"],
    "emojiPanBg": T["surface2"],
    "emojiPanCategories": OVER2,
    "emojiPanHeaderFg": T["text_muted"],
    "emojiPanHeaderBg": alpha(T["surface2"], 0xf2),
    "emojiIconFg": T["text_muted"],
    "emojiSubIconFgActive": T["accent_light"],
    "historyTextInFg": T["paper_ink"],
    "historyTextInFgSelected": T["paper_ink"],
    "historyTextOutFg": T["text"],
    "historyTextOutFgSelected": T["text"],
    "historyLinkInFg": T["accent"],
    "historyLinkInFgSelected": T["accent_dark"],
    "historyLinkOutFg": T["text"],
    "historyLinkOutFgSelected": T["text"],
    "historyFileNameInFg": T["paper_ink"],
    "historyFileNameInFgSelected": T["paper_ink"],
    "historyFileNameOutFg": T["text"],
    "historyFileNameOutFgSelected": T["text"],
    "historyOutIconFg": T["text"],
    "historyOutIconFgSelected": T["text"],
    "historyIconFgInverted": alpha(T["text"], 0xe5),
    "historySendingOutIconFg": T["accent_muted"],
    "historySendingInIconFg": T["paper_muted"],
    "historySendingInvertedIconFg": alpha(T["text"], 0xc8),
    "historyCallArrowInFg": T["ok_on_paper"],
    "historyCallArrowInFgSelected": T["ok_on_paper"],
    "historyCallArrowMissedInFgSelected": T["accent"],
    "historyCallArrowOutFg": T["text"],
    "historyCallArrowOutFgSelected": T["text"],
    "historyUnreadBarBg": T["surface2"],
    "historyUnreadBarBorder": "#00000000",
    "historyUnreadBarFg": T["text"],
    "historyScrollBarBg": alpha(T["text"], 0x4d),
    "historyScrollBarBgOver": alpha(T["text"], 0x7a),
    "historyScrollBg": alpha(T["text"], 0x14),
    "historyScrollBgOver": alpha(T["text"], 0x26),
    "msgInBg": T["paper"],
    "msgInBgSelected": PAPER_SELECTED,
    "msgOutBg": T["accent"],
    "msgOutBgSelected": RED_SELECTED,
    "msgSelectOverlay": alpha(T["accent"], 0x33),
    "msgStickerOverlay": alpha(T["accent"], 0x66),
    "msgInServiceFg": T["accent"],
    "msgInServiceFgSelected": T["accent_dark"],
    "msgOutServiceFg": T["text"],
    "msgOutServiceFgSelected": T["text"],
    "msgInShadow": "#000000",
    "msgInShadowSelected": "#000000",
    "msgOutShadow": "#000000",
    "msgOutShadowSelected": "#000000",
    "msgInDateFg": T["paper_muted"],
    "msgInDateFgSelected": T["paper_muted"],
    "msgOutDateFg": T["accent_muted"],
    "msgOutDateFgSelected": T["accent_muted"],
    "msgServiceFg": T["paper_ink"],
    "msgServiceBg": T["paper"],
    "msgServiceBgSelected": PAPER_SELECTED,
    "msgInReplyBarColor": T["accent"],
    "msgInReplyBarSelColor": T["accent_dark"],
    "msgOutReplyBarColor": T["text"],
    "msgOutReplyBarSelColor": T["text"],
    "msgInMonoFg": T["accent_dark"],
    "msgOutMonoFg": T["accent_muted"],
    "msgInMonoFgSelected": T["accent_dark"],
    "msgOutMonoFgSelected": T["accent_muted"],
    "msgDateImgFg": T["text"],
    "msgDateImgBg": alpha(T["bg"], 0xa0),
    "msgDateImgBgOver": alpha(T["bg"], 0xc0),
    "msgDateImgBgSelected": alpha(T["accent_dark"], 0xb0),
    "msgFileThumbLinkInFg": T["accent"],
    "msgFileThumbLinkInFgSelected": T["accent_dark"],
    "msgFileThumbLinkOutFg": T["accent_muted"],
    "msgFileThumbLinkOutFgSelected": T["text"],
    "msgFileInBg": T["accent"],
    "msgFileInBgOver": RED_SELECTED,
    "msgFileInBgSelected": T["accent_dark"],
    "msgFileOutBg": T["text"],
    "msgFileOutBgSelected": T["accent_muted"],
    "historyFileInIconFg": T["text"],
    "historyFileInIconFgSelected": T["text"],
    "historyFileInRadialFg": T["text"],
    "historyFileOutIconFg": T["accent"],
    "historyFileOutIconFgSelected": T["accent_dark"],
    "historyFileOutRadialFg": T["accent"],
    "historyFileOutRadialFgSelected": T["accent_dark"],
    "historyFileThumbIconFg": T["paper"],
    "historyFileThumbIconFgSelected": T["paper"],
    "historyFileThumbRadialFgSelected": T["paper"],
    "msgWaveformInActive": T["accent"],
    "msgWaveformInActiveSelected": T["accent_dark"],
    "msgWaveformInInactive": mix(T["paper"], T["paper_muted"], 0.35),
    "msgWaveformInInactiveSelected": mix(T["paper"], T["paper_muted"], 0.45),
    "msgWaveformOutActive": T["text"],
    "msgWaveformOutActiveSelected": T["text"],
    "msgWaveformOutInactive": mix(T["accent"], T["text"], 0.35),
    "msgWaveformOutInactiveSelected": mix(T["accent"], T["text"], 0.3),
    "msgBotKbOverBgAdd": alpha(T["text"], 0x0f),
    "msgBotKbRippleBg": alpha(T["text"], 0x0b),
    "youtubePlayIconBg": alpha(T["accent"], 0xc8),
    "videoPlayIconBg": alpha(T["bg"], 0xa0),
    "videoPlayIconFg": T["text"],
    "toastBg": alpha(T["surface2"], 0xf2),
    "toastFg": T["text"],
    "historyToDownBg": T["surface2"],
    "historyToDownBgOver": OVER2,
    "historyToDownBgRipple": T["border"],
    "historyToDownFg": T["text_muted"],
    "historyToDownFgOver": T["text"],
    "historyToDownShadow": "#00000060",
    "historyComposeAreaBg": T["surface2"],
    "historyComposeAreaFg": T["text"],
    "historyComposeAreaFgService": T["text_muted"],
    "historyComposeIconFg": T["text_muted"],
    "historyComposeIconFgOver": T["accent_light"],
    "historySendIconFg": T["accent_light"],
    "historySendIconFgOver": T["accent_light"],
    "historyPinnedBg": T["surface"],
    "historyReplyBg": T["surface2"],
    "historyReplyIconFg": T["accent_light"],
    "historyReplyCancelFg": T["text_muted"],
    "historyReplyCancelFgOver": T["text"],
    "historyComposeButtonBg": T["surface2"],
    "historyComposeButtonBgOver": OVER2,
    "historyComposeButtonBgRipple": T["border"],
    "overviewCheckBg": "#00000060",
    "overviewCheckFgActive": T["text"],
    "overviewCheckBorder": T["paper"],
    "overviewPhotoSelectOverlay": alpha(T["accent"], 0x33),
    "profileStatusFgOver": T["text_muted"],
    "profileVerifiedCheckFg": T["text"],
    "profileAdminStartFg": T["accent_light"],
    "notificationSampleCloseFg": T["text_muted"],
    "notificationSampleTextFg": T["text"],
    "notificationSampleNameFg": T["text_muted"],
    "mainMenuBg": T["surface2"],
    "mainMenuCoverBg": T["accent"],
    "mainMenuCloudBg": T["accent_dark"],
    "mediaPlayerBg": T["surface2"],
    "mediaPlayerActiveFg": T["accent_light"],
    "mediaPlayerInactiveFg": T["border"],
    "mediaPlayerDisabledFg": mix(T["surface2"], T["accent"], 0.4),
    "mediaviewFileBlueCornerFg": T["accent"],
    "mediaviewMenuBg": T["surface2"],
    "mediaviewMenuBgOver": T["border"],
    "mediaviewMenuBgRipple": T["halftone"],
    "mediaviewBg": alpha(T["bg"], 0xeb),
    "mediaviewCaptionBg": alpha(T["bg"], 0xa0),
    "mediaviewTextLinkFg": T["accent_light"],
    "mediaviewPlaybackActive": T["paper"],
    "mediaviewPlaybackInactive": T["border"],
    "mediaviewPlaybackActiveOver": T["text"],
    "mediaviewPlaybackInactiveOver": T["halftone"],
    "mediaviewPlaybackProgressFg": alpha(T["text"], 0xc7),
    "callBg": alpha(T["bg"], 0xf5),
    "callNameFg": T["text"],
    "callStatusFg": T["text_muted"],
    "callIconFg": T["text"],
    "callAnswerBg": "#2e7d32",
    "callAnswerRipple": "#286c2c",
    "callAnswerBgOuter": "#2e7d3226",
    "callHangupBg": T["accent"],
    "callHangupRipple": T["accent_dark"],
    "callBarBg": T["accent"],
    "callBarMuteRipple": RED_OVER,
    "callBarBgMuted": T["halftone"],
    "importantTooltipFgLink": T["accent_light"],
    "botKbBg": T["surface2"],
    "botKbDownBg": OVER2,
    "sideBarBg": T["bg"],
    "sideBarBgActive": T["surface2"],
    "sideBarBgRipple": alpha(T["text"], 0x26),
    "sideBarTextFg": T["text_muted"],
    "sideBarTextFgActive": T["text"],
    "sideBarIconFg": T["text_muted"],
    "sideBarIconFgActive": T["accent_light"],
    "sideBarBadgeBg": T["accent"],
    "sideBarBadgeBgMuted": MUTED_BADGE,
    "sideBarBadgeFg": T["text"],
    "boxDividerBg": T["bg"],
    "boxDividerFg": T["border"],
    "searchedTextMatchBg": T["accent_light"],
    "searchedTextMatchFg": T["paper_ink"],
    "searchedTextCurrentMatchBg": T["accent"],
    "searchedTextCurrentMatchFg": T["text"],
    "spellUnderline": alpha(T["accent_light"], 0x88),
}

for index, color in enumerate(PEER_NAMES, start=1):
    MAPPING[f"historyPeer{index}NameFg"] = color
    MAPPING[f"historyPeer{index}NameFgSelected"] = color

TEXT_PAIRS = [
    ("windowFg", "windowBg"), ("windowSubTextFg", "windowBg"),
    ("windowActiveTextFg", "windowBg"), ("boxTextFg", "boxBg"),
    ("boxTitleAdditionalFg", "boxBg"), ("boxTextFgError", "boxBg"),
    ("dialogsNameFg", "dialogsBg"), ("dialogsTextFg", "dialogsBg"),
    ("dialogsDraftFg", "dialogsBg"), ("dialogsNameFgActive", "dialogsBgActive"),
    ("dialogsTextFgActive", "dialogsBgActive"),
    ("dialogsDateFgActive", "dialogsBgActive"),
    ("dialogsUnreadFg", "dialogsUnreadBg"),
    ("dialogsUnreadFgActive", "dialogsUnreadBgActive"),
    ("historyTextInFg", "msgInBg"), ("historyTextOutFg", "msgOutBg"),
    ("historyLinkInFg", "msgInBg"), ("historyLinkOutFg", "msgOutBg"),
    ("msgInDateFg", "msgInBg"), ("msgOutDateFg", "msgOutBg"),
    ("msgInServiceFg", "msgInBg"), ("msgOutServiceFg", "msgOutBg"),
    ("msgInMonoFg", "msgInBg"), ("msgOutMonoFg", "msgOutBg"),
    ("msgServiceFg", "msgServiceBg"), ("historyTextInFg", "msgInBgSelected"),
    ("historyTextOutFg", "msgOutBgSelected"),
    ("msgOutDateFg", "msgOutBgSelected"),
    ("activeButtonFg", "activeButtonBg"), ("lightButtonFg", "lightButtonBg"),
    ("menuIconFg", "menuBg"), ("placeholderFg", "historyComposeAreaBg"),
    ("historyComposeAreaFg", "historyComposeAreaBg"),
    ("tooltipFg", "tooltipBg"), ("toastFg", "surface2"),
    ("sideBarTextFg", "sideBarBg"), ("sideBarTextFgActive", "sideBarBgActive"),
    ("contactsStatusFgOnline", "contactsBg"),
    ("searchedTextMatchFg", "searchedTextMatchBg"),
    ("searchedTextCurrentMatchFg", "searchedTextCurrentMatchBg"),
] + [(f"historyPeer{i}NameFg", "msgInBg") for i in range(1, 9)]


def parse(text):
    return [(k, v.strip()) for k, v in
            re.findall(r"^([A-Za-z0-9_]+)\s*:\s*([^;]+);", text, re.M)]


def recolor(value):
    color = value[:7]
    tail = value[7:]
    r, g, b = (c / 255 for c in rgb(color))
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    if s < 0.18 or max(r, g, b) - min(r, g, b) < 0.08:
        ramp = [T["bg"], T["surface"], T["surface2"], T["border"],
                T["halftone"], T["text_muted"], T["text"]]
        best = min(ramp, key=lambda c: abs(luminance(c) - luminance(color)))
        return best + tail
    if 0.02 < h < 0.13:
        target = T["accent_light"] if l > 0.55 else T["accent"]
        if l < 0.35:
            target = T["accent_dark"]
        return target + tail
    return value


def build_palette():
    keys = [k for k, _ in parse(PALETTE.read_text(encoding="utf-8"))]
    with zipfile.ZipFile(BASE_THEME) as archive:
        base = dict(parse(archive.read("colors.tdesktop-theme").decode()))
    unknown = sorted(set(MAPPING) - set(keys))
    if unknown:
        sys.exit(f"Unknown palette keys: {unknown}")
    lines = []
    fallback = []
    for key in keys:
        if key in MAPPING:
            value = MAPPING[key]
        elif key in base:
            value = base[key]
            if value.startswith("#") and not key.startswith(KEEP_PREFIXES):
                value = recolor(value)
                fallback.append(key)
        else:
            continue
        lines.append(f"{key}: {value};")
    resolved = {}
    for line in lines:
        key, value = line[:-1].split(": ")
        resolved[key] = value
    resolved["surface2"] = T["surface2"]

    def solid(key):
        value = resolved[key]
        while not value.startswith("#"):
            value = resolved[value]
        return value[:7]
    failed = []
    for fg, bg in TEXT_PAIRS:
        ratio = contrast(solid(fg), solid(bg))
        if ratio < 4.5:
            failed.append(f"{fg} on {bg}: {ratio:.2f}")
    if failed:
        sys.exit("WCAG AA failed:\n" + "\n".join(failed))
    print(f"palette: {len(lines)} keys, {len(fallback)} recolored from web")
    return "\n".join(lines) + "\n"


def torn_polygon(rng, w, h, angle, roughness):
    points = []
    for side in range(4):
        steps = max(4, int((w if side % 2 == 0 else h) / 14))
        for i in range(steps):
            t = i / steps
            if side == 0:
                x, y = -w / 2 + w * t, -h / 2
            elif side == 1:
                x, y = w / 2, -h / 2 + h * t
            elif side == 2:
                x, y = w / 2 - w * t, h / 2
            else:
                x, y = -w / 2, h / 2 - h * t
            jitter = rng.uniform(-roughness, roughness)
            if side % 2 == 0:
                y += jitter
            else:
                x += jitter
            points.append((x, y))
    return rotate(points, angle)


def rotate(points, angle):
    cos, sin = math.cos(angle), math.sin(angle)
    return [(x * cos - y * sin, x * sin + y * cos) for x, y in points]


def place(points, x0, y0):
    return [(x0 + x, y0 + y) for x, y in points]


def wrapped(cx, cy, paint):
    for dx in (-TILE, 0, TILE):
        for dy in (-TILE, 0, TILE):
            paint(cx + dx, cy + dy)


def layer():
    return Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))


def halftone_layer(rng):
    result = layer()
    draw = ImageDraw.Draw(result)
    color = rgb(T["halftone"]) + (120,)
    step = 11
    for _ in range(7):
        cx, cy = rng.uniform(0, TILE), rng.uniform(0, TILE)
        radius = rng.uniform(140, 260)
        dots = []
        for gx in range(int(-radius), int(radius), step):
            for gy in range(int(-radius), int(radius), step):
                d = math.hypot(gx, gy) / radius
                if d < 1:
                    dots.append((gx, gy, (1 - d) * step * 0.42))

        def paint(x0, y0):
            for gx, gy, r in dots:
                x, y = x0 + gx, y0 + gy
                draw.ellipse((x - r, y - r, x + r, y + r), fill=color)
        wrapped(cx, cy, paint)
    return result


def scraps_layer(rng):
    result = layer()
    draw = ImageDraw.Draw(result)
    shades = [mix(T["bg"], T["paper"], v) for v in (0.05, 0.065, 0.08)]
    ink = rgb(mix(T["bg"], T["paper"], 0.12)) + (255,)
    for _ in range(18):
        cx, cy = rng.uniform(0, TILE), rng.uniform(0, TILE)
        w, h = rng.uniform(150, 320), rng.uniform(110, 260)
        angle = math.radians(rng.uniform(-14, 14))
        shade = rgb(rng.choice(shades)) + (235,)
        shadow = [(x + 6, y + 7) for x, y in torn_polygon(rng, w, h, angle, 5)]
        body = torn_polygon(rng, w, h, angle, 5)
        bars = []
        top = -h / 2 + 18
        if rng.random() < 0.35:
            bars.append(rotate([(-w / 2 + 16, top), (w / 2 - 30, top),
                                (w / 2 - 30, top + 14), (-w / 2 + 16, top + 14)],
                               angle))
            top += 28
        lines = []
        if rng.random() < 0.8:
            columns = 2 if w > 220 else 1
            col_w = (w - 32 - (columns - 1) * 12) / columns
            for column in range(columns):
                left = -w / 2 + 16 + column * (col_w + 12)
                y = top
                while y < h / 2 - 14:
                    length = col_w * rng.uniform(0.72, 1.0)
                    lines.append(rotate([(left, y), (left + length, y)], angle))
                    y += 9

        def paint(x0, y0):
            draw.polygon(place(shadow, x0, y0), fill=(0, 0, 0, 120))
            draw.polygon(place(body, x0, y0), fill=shade)
            for bar in bars:
                draw.polygon(place(bar, x0, y0), fill=ink)
            for line in lines:
                draw.line(place(line, x0, y0), fill=ink, width=3)
        wrapped(cx, cy, paint)
    return result


def reds_layer(rng):
    result = layer()
    draw = ImageDraw.Draw(result)
    for _ in range(7):
        cx, cy = rng.uniform(0, TILE), rng.uniform(0, TILE)
        kind = rng.choice(["strip", "circle", "triangle"])
        size = rng.uniform(60, 150)
        angle = math.radians(rng.uniform(0, 360))
        color = rgb(mix(T["bg"], T["accent"], rng.uniform(0.17, 0.23))) + (255,)
        if kind == "strip":
            shape = torn_polygon(rng, size * 2.2, size * 0.35, angle, 3)
        elif kind == "triangle":
            shape = [(math.cos(angle + k * 2.094) * size * 0.6,
                      math.sin(angle + k * 2.094) * size * 0.6)
                     for k in range(3)]
        else:
            shape = None

        def paint(x0, y0):
            if shape is None:
                draw.ellipse((x0 - size / 2, y0 - size / 2,
                              x0 + size / 2, y0 + size / 2), fill=color)
            else:
                draw.polygon(place(shape, x0, y0), fill=color)
        wrapped(cx, cy, paint)
    return result


def tape_layer(rng):
    result = layer()
    draw = ImageDraw.Draw(result)
    color = rgb(T["paper"]) + (14,)
    for _ in range(10):
        cx, cy = rng.uniform(0, TILE), rng.uniform(0, TILE)
        shape = torn_polygon(rng, 110, 30, math.radians(rng.uniform(-40, 40)), 2)
        wrapped(cx, cy, lambda x0, y0: draw.polygon(place(shape, x0, y0),
                                                    fill=color))
    return result


def grain_layer(rng):
    noise = Image.effect_noise((TILE, TILE), 28).convert("L")
    pixels = noise.load()
    for x in range(TILE):
        pixels[x, TILE - 1] = pixels[x, 0]
    for y in range(TILE):
        pixels[TILE - 1, y] = pixels[0, y]
    return Image.merge("RGBA", (noise, noise, noise,
                                Image.new("L", (TILE, TILE), 10)))


def wallpaper():
    rng = random.Random(WALLPAPER_SEED)
    image = Image.new("RGBA", (TILE, TILE), rgb(T["bg"]) + (255,))
    for make in (halftone_layer, scraps_layer, reds_layer, tape_layer,
                 grain_layer):
        image.alpha_composite(make(rng))
    result = image.convert("RGB")
    buffer = io.BytesIO()
    result.save(buffer, "JPEG", quality=86, optimize=True)
    preview = Image.new("RGB", (TILE * 2, TILE * 2))
    for x in (0, TILE):
        for y in (0, TILE):
            preview.paste(result, (x, y))
    preview.resize((TILE, TILE)).save(PREVIEW, "JPEG", quality=80)
    print(f"wallpaper: {TILE}x{TILE}, {len(buffer.getvalue()) // 1024} KB")
    return buffer.getvalue()


def main():
    palette = build_palette()
    background = wallpaper()
    with zipfile.ZipFile(OUTPUT, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("colors.tdesktop-theme", palette)
        archive.writestr("tiled.jpg", background)
    print(f"written {OUTPUT.relative_to(REPO.parent)}")


if __name__ == "__main__":
    main()
