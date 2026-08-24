import unittest

import spotify_bridge as bridge


class SpotifyBridgeTests(unittest.TestCase):
    def test_parse_lrc_supports_offsets_and_timestamp_precision(self):
        lyrics = bridge.parse_lrc(
            "[offset:+100]\n"
            "[00:01.25]First line\n"
            "[00:02.500][00:03.00]Repeated line"
        )
        self.assertEqual(
            lyrics,
            ((1350, "First line"), (2600, "Repeated line"), (3100, "Repeated line")),
        )

    def test_lyric_window_follows_playback_position(self):
        result = bridge.LyricsResult(
            "synced", ((1000, "One"), (2000, "Two"), (3000, "Three"))
        )
        self.assertEqual(bridge.lyric_window(result, 2500), ("One", "Two", "Three"))

    def test_protocol_sanitizes_delimiters_and_has_no_empty_fields(self):
        state = bridge.PlaybackState(
            True, True, 1250, 3000, "track", "A | title", "Artist", "Album"
        )
        self.assertEqual(
            bridge.encode_state(state),
            b"SPOT|1|1|1250|3000|track|A title|Artist\n",
        )
        message = bridge.encode_lyrics(bridge.LyricsResult("loading"), 0)
        self.assertEqual(len(message.decode().rstrip("\n").split("|")), 5)

    def test_gvariant_parser_accepts_apostrophe_and_both_quote_styles(self):
        raw = (
            "{'xesam:title': <\"Merry Christmas, Please Don't Call\">, "
            "'xesam:album': <'A Single-Quoted Album'>, "
            "'xesam:artist': <[\"Guns N' Roses\"]>}"
        )
        self.assertEqual(
            bridge.variant_string("xesam:title", raw),
            "Merry Christmas, Please Don't Call",
        )
        self.assertEqual(
            bridge.variant_string("xesam:album", raw), "A Single-Quoted Album"
        )
        self.assertEqual(
            bridge.variant_string("xesam:artist", raw, array=True), "Guns N' Roses"
        )


if __name__ == "__main__":
    unittest.main()
